#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <RTClib.h>
#include <ESP32-VirtualMatrixPanel-I2S-DMA.h>
#include <time.h>
#include <ArduinoOTA.h>

// Matrix configuration
#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1

MatrixPanel_I2S_DMA *display = nullptr;
RTC_DS3231 rtc;

// Colors
uint16_t myWHITE;
uint16_t myRED;
uint16_t myBLUE;
uint16_t myBLACK;

// Sync interval
unsigned long lastSync = 0;
const unsigned long syncInterval = 6L * 60L * 60L * 1000L; // every 6 hours

// Blink and update timing
unsigned long lastBlink = 0;
unsigned long lastTimeUpdate = 0;
const int updateInterval = 100; // Update physics every 100ms

// State variables
bool colonBlink = true;
bool isNight = false;
int lastDisplayedHour = -1;
int lastDisplayedMinute = -1;

void syncRTCFromNTP() {
  configTime(3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("⏳ Waiting for NTP");

  while (time(nullptr) < 100000) {
    delay(500); // This is still a problem, but less frequent. Consider a non-blocking state machine.
    Serial.print(".");
  }

  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  rtc.adjust(DateTime(
    timeinfo->tm_year + 1900,
    timeinfo->tm_mon + 1,
    timeinfo->tm_mday,
    timeinfo->tm_hour,
    timeinfo->tm_min,
    timeinfo->tm_sec
  ));

  Serial.println("\n✅ RTC synced from NTP");
}

void setup() {
  Serial.begin(115200);

  // WiFi Config
  WiFiManager wm;
  wm.setTimeout(180);
  if (!wm.autoConnect("MediBoard_Clock")) {
    Serial.println("❌ WiFi failed, restarting...");
    ESP.restart();
  }
  Serial.println("✅ WiFi connected!");

  // OTA Config
  ArduinoOTA.setHostname("esp32-clock");
  ArduinoOTA.begin();
  Serial.println("🔄 OTA Ready");

  // I2C for RTC
  Wire.begin(21, 22);

  if (!rtc.begin()) {
    Serial.println("❌ Couldn't find RTC");
    while (1);
  }

  if (rtc.lostPower()) {
    Serial.println("⚠️ RTC lost power, syncing from NTP");
    syncRTCFromNTP();
  }

  // Matrix config
  HUB75_I2S_CFG mxconfig(
    PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN
  );
  mxconfig.gpio.r1 = 32;
  mxconfig.gpio.g1 = 33;
  mxconfig.gpio.b1 = 23;
  mxconfig.gpio.r2 = 19;
  mxconfig.gpio.g2 = 25;
  mxconfig.gpio.b2 = 18;
  mxconfig.gpio.a  = 5;
  mxconfig.gpio.b  = 27;
  mxconfig.gpio.c  = 17;
  mxconfig.gpio.d  = 14;
  mxconfig.gpio.e  = 26;
  mxconfig.gpio.lat = 12;
  mxconfig.gpio.oe  = 4;
  mxconfig.gpio.clk = 16;
  mxconfig.driver = HUB75_I2S_CFG::FM6124;

  display = new MatrixPanel_I2S_DMA(mxconfig);
  if (!display->begin()) {
    Serial.println("❌ Matrix initialization failed!");
    while (1);
  }

  // Colors
  myWHITE = display->color565(255, 255, 255);
  myRED   = display->color565(255, 0, 0);
  myBLUE  = display->color565(0, 0, 255);
  myBLACK = display->color565(0, 0, 0);

  display->setBrightness8(255); // Start at full brightness
  display->fillScreen(myBLACK);
  display->setTextColor(myWHITE);
  display->setBrightness8(255);
  display->setTextSize(1);
  display->print("SONVISAGE");
  display->setCursor(3, 17);
  display->print("MEDIBOARDS");
  // Initial sync
  syncRTCFromNTP();
  lastSync = millis();
}

void loop() {
  ArduinoOTA.handle();

  // Re-sync time every 6 hours (WARNING: This is a blocking call!)
  if (millis() - lastSync >= syncInterval) {
    syncRTCFromNTP();
    lastSync = millis();
  }

  // Update logic at a fixed interval, WITHOUT delaying the main loop
  if (millis() - lastTimeUpdate >= updateInterval) {
    lastTimeUpdate = millis();

    // Handle colon blinking
    if (millis() - lastBlink >= 1000) {
      colonBlink = !colonBlink;
      lastBlink = millis();
    }

    DateTime now = rtc.now();
    int hour = now.hour();
    int displayHour = (hour == 0) ? 12 : (hour > 12 ? hour - 12 : hour);
    int minute = now.minute();

    // Check if night mode state changed
    bool newNightState = (hour >= 19 || hour < 6);
    if (newNightState != isNight) {
      isNight = newNightState;
      display->setBrightness8(isNight ? 40 : 255); // ONLY change brightness when needed
      Serial.println(isNight ? "🌙 Night mode ON" : "☀️ Day mode ON");
    }

    // --- SMART REDRAW: Only update what has changed --- //

    // 1. Check if the numbers have changed
    if (displayHour != lastDisplayedHour || minute != lastDisplayedMinute) {
      // If the numbers changed, we need to redraw everything
      display->fillScreen(myBLACK); // Only clear if necessary

      display->setTextColor(myWHITE);
      display->setTextSize(2);

      // Draw hours
      display->setCursor(5, 8);
      if (displayHour < 10) {
        display->print("0"); // Add leading zero for consistent spacing
      }
      display->print(displayHour);

      // Draw colon (state will be handled below)
      display->setCursor(29, 8);
      display->print(":");

      // Draw minutes
      display->setCursor(38, 8);
      if (minute < 10) {
        display->print("0");
      }
      display->print(minute);

      // Store the last drawn values
      lastDisplayedHour = displayHour;
      lastDisplayedMinute = minute;

    } else {
      // 2. If only the colon state might have changed, just update the colon
      display->setTextSize(2);
      display->setCursor(29, 8);
      display->setTextColor(colonBlink ? myWHITE : myBLACK);
      display->print(":");
    }
  }
  // End of update logic

  // The loop now runs incredibly fast, allowing the DMA library to work uninterrupted.
  // A small yield() is better than a long delay().
  yield();
}