#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ESP32-VirtualMatrixPanel-I2S-DMA.h>
#include <ArduinoOTA.h>  // ✅ OTA library

// ===== Display config =====
#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 4
int x = 0;
MatrixPanel_I2S_DMA *dma_display = nullptr;
uint16_t myBLACK, myBLUE, myRED;

int scrollX = 0;
String scrollMessage = "";

// ===== API config =====
const char* host = "api.mediboards.io";
const String path = "/api/public/hospitals/687f47fe-2429-4465-8b59-18432a3195fe/latest-patient";
unsigned long lastFetch = 0;
const unsigned long fetchInterval = 30000;

// ===== Fetch data from API =====
void fetchAndUpdateMessage() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String("https://") + host + path;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    payload.trim();
    Serial.println("API Response:");
    Serial.println(payload);

    if (payload.length() > 0) {
      scrollMessage = "NEW PATIENT AT COUCH " + payload + "   ";
    } else {
      scrollMessage = "WELCOME TO NHA A&E DEPT   ";
    }
  } else {
    Serial.printf("HTTP Error: %d\n", httpCode);
  }

  http.end();
}

// ===== Setup OTA =====
void setupOTA() {
  ArduinoOTA
    .onStart([]() {
      Serial.println("OTA Start");
    })
    .onEnd([]() {
      Serial.println("\nOTA End");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("OTA Progress: %u%%\r", (progress * 100) / total);
    })
    .onError([](ota_error_t error) {
      Serial.printf("OTA Error [%u]\n", error);
    });

  ArduinoOTA.begin();
  Serial.println("✅ OTA Ready");
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);

  // ===== Initialize WiFi using WiFiManager =====
  WiFiManager wm;
  wm.setTimeout(180);
  bool res = wm.autoConnect("Juth_MediBoard");

  if (!res) {
    Serial.println("❌ Failed to connect. Restarting...");
    ESP.restart();
    delay(1000);
  }

  Serial.println("✅ Connected to WiFi!");

  // ===== OTA Setup =====
  setupOTA();

  // ===== LED Matrix Setup =====
  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
  mxconfig.gpio.r1 = 25;
  mxconfig.gpio.g1 = 26;
  mxconfig.gpio.b1 = 27;
  mxconfig.gpio.r2 = 14;
  mxconfig.gpio.g2 = 12;
  mxconfig.gpio.b2 = 13;
  mxconfig.gpio.a  = 23;
  mxconfig.gpio.b  = 19;
  mxconfig.gpio.c  = 5;
  mxconfig.gpio.d  = 17;
  mxconfig.gpio.e  = 32;
  mxconfig.gpio.lat = 4;
  mxconfig.gpio.oe  = 15;
  mxconfig.gpio.clk = 16;
  mxconfig.clkphase = false;
  mxconfig.driver = HUB75_I2S_CFG::FM6124;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  if (!dma_display->begin()) {
    Serial.println("❌ Matrix init failed!");
    while (1);
  }

  dma_display->setBrightness8(90);
  myBLACK = dma_display->color565(0, 0, 0);
  myRED   = dma_display->color565(255, 0, 0);
  myBLUE  = dma_display->color565(0, 0, 255);

  dma_display->setTextSize(2);
  dma_display->setTextColor(myRED);
  dma_display->setCursor(3, 10);
  dma_display->print(" SONVISAGE MEDIBOARD ");

  delay(10000);
  scrollX = dma_display->width();

  fetchAndUpdateMessage();
  lastFetch = millis();
}

// ===== Loop =====
void loop() {
  ArduinoOTA.handle();  // ✅ Handle OTA requests

  // Clear screen
  dma_display->fillScreen(myBLACK);

  // Static header
  dma_display->setTextSize(2);
  dma_display->setTextColor(myRED);
  dma_display->setCursor(10, 1);
  dma_display->print("NHA EMERGENCY UNIT");

  // Scrolling message
  dma_display->setTextSize(2);
  dma_display->setTextColor(myRED);
  dma_display->setCursor(scrollX, 17);
  dma_display->print(scrollMessage);

  scrollX--;
  int16_t textWidth = scrollMessage.length() * 12;
  if (scrollX < -textWidth) {
    scrollX = dma_display->width();
    fetchAndUpdateMessage();
  }

  delay(50);
}

