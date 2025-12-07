#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "time.h"
#include <NimBLEDevice.h> 
#include <IRremote.hpp>

// ===================================================================================
// ⚙️ USER CONFIGURATION (EDIT THIS SECTION)
// ===================================================================================

// 1. Wi-Fi Credentials
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASS     = "YOUR_WIFI_PASSWORD";

// 2. Google Apps Script URL (Must end with /exec)
// Follow setup guide to deploy the script.
const char* GOOGLE_URL    = "https://script.google.com/macros/s/YOUR_SCRIPT_ID/exec"; 

// 3. Presence Detection (iBeacon UUID)
// Use "Beacon Simulator" app on Android/iOS to broadcast this ID.
// This should be the first 16+ chars of the beacon's HEX data.
const String MY_BEACON_ID = "YOUR_BEACON_ID"; 

// 4. Hardware Pins
const int IR_SEND_PIN     = 4;    // ESP32 Pin connected to IR LED (+ resistor)

// 5. IR Remote Codes (Protocol: NEC)
// You might need to change these to match your LED controller
const uint16_t IR_ADDRESS = 0x00; 
const uint8_t CMD_ON      = 0x3;
const uint8_t CMD_OFF     = 0x2;
const uint8_t CMD_RED     = 0x4;
const uint8_t CMD_GREEN   = 0x5;
const uint8_t CMD_YELLOW  = 0x14;
const uint8_t CMD_FADE    = 0x13; 

// 6. Timings & Logic
const int SCAN_DURATION   = 3;    // Bluetooth scan duration (seconds)
const int AWAY_MINUTES    = 15;   // Time before switching to "Yellow"
const int SLEEP_HOURS     = 4;    // Time before switching to "Sleep Mode"
const int GOOGLE_INTERVAL = 60;   // How often to check calendar (seconds)

// ===================================================================================

// Global Variables
unsigned long lastSeenTimestamp = 0;   
unsigned long lastGoogleCheck   = 0;   
String calendarStatus = "FREE";        
String currentMode    = "STARTUP";     

NimBLEScan* pBLEScan;

// Function Prototypes
void scanForBeacon();
void checkGoogleCalendar();
void updateTrafficLight();
void connectWiFi();
void sendIR(uint8_t command);

void setup() {
  Serial.begin(115200);
  Serial.println("\n>>> SYSTEM STARTUP");

  // Initialize IR Sender
  IrSender.begin(IR_SEND_PIN);

  // Connect WiFi & Sync Time (Required for SSL)
  connectWiFi();
  configTime(0, 0, "pool.ntp.org"); 
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("[TIME] Failed to sync time");
  } else {
    Serial.println("[TIME] Synced");
  }

  // Initialize Bluetooth (NimBLE)
  NimBLEDevice::init("");
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setActiveScan(true); 
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  
  Serial.printf("[SYSTEM] Free RAM: %d KB\n", ESP.getFreeHeap() / 1024);

  // Initialize timer to "long absent" state
  lastSeenTimestamp = millis() - (SLEEP_HOURS * 3600 * 1000) - 1000;
}

void loop() {
  // 1. Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  // 2. Scan for presence (Blocking function)
  scanForBeacon();

  // 3. Check Google Calendar (Periodic)
  if (millis() - lastGoogleCheck > (GOOGLE_INTERVAL * 1000)) {
    checkGoogleCalendar();
    lastGoogleCheck = millis();
  }

  // 4. Apply logic
  updateTrafficLight();
}

// -----------------------------------------------------------------------------------
// 🔍 BEACON SCANNER
// -----------------------------------------------------------------------------------
void scanForBeacon() {
  // Start scanning
  pBLEScan->start(0, false);
  
  // Manual delay to ensure stable scanning window
  delay(SCAN_DURATION * 1000);
  
  pBLEScan->stop();
  
  NimBLEScanResults results = pBLEScan->getResults();

  for (int i = 0; i < results.getCount(); i++) {
    const NimBLEAdvertisedDevice* device = results.getDevice(i);
    
    if (device->haveManufacturerData()) {
      std::string md = device->getManufacturerData();
      String hexStr = "";
      for (int j = 0; j < md.length(); j++) {
        char hexBuff[3];
        sprintf(hexBuff, "%02x", (unsigned char)md[j]);
        hexStr += hexBuff;
      }

      // Check against user Beacon ID
      if (hexStr.startsWith(MY_BEACON_ID) && device->getRSSI() > -90) {
         lastSeenTimestamp = millis(); 
         Serial.printf("✅ BEACON DETECTED! RSSI: %d\n", device->getRSSI());
         break; 
      }
    }
  }
  pBLEScan->clearResults(); 
}

// -----------------------------------------------------------------------------------
// 📅 GOOGLE CALENDAR SYNC
// -----------------------------------------------------------------------------------
void checkGoogleCalendar() {
  Serial.print("[GOOGLE] Sync... ");
  
  // Dynamic client allocation to save RAM
  WiFiClientSecure *client = new WiFiClientSecure;
  if(client) {
    client->setInsecure(); // Skip certificate validation
    client->setTimeout(10000); 

    HTTPClient https;
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (https.begin(*client, GOOGLE_URL)) {
      int code = https.GET();
      if (code > 0) {
        String payload = https.getString();
        payload.trim();
        if (payload == "BUSY" || payload == "FREE") {
          calendarStatus = payload;
          Serial.println(calendarStatus);
        } else {
          Serial.println("Unknown response: " + payload);
        }
      } else {
        Serial.printf("Error: %d\n", code);
      }
      https.end();
    } else {
      Serial.println("Connect Failed");
    }
    delete client; 
  } else {
    Serial.println("Low RAM!");
  }
}

// -----------------------------------------------------------------------------------
// 🚦 STATE MACHINE & IR CONTROL
// -----------------------------------------------------------------------------------
void updateTrafficLight() {
  unsigned long timeGone = millis() - lastSeenTimestamp; 
  String nextMode = "OFF"; 

  unsigned long t15min = AWAY_MINUTES * 60 * 1000;
  unsigned long tSleep = SLEEP_HOURS * 3600 * 1000;
  bool isHere = (timeGone < 60000); 

  // Priority Logic
  if (timeGone > tSleep)             nextMode = "FADE";   // Long absence
  else if (calendarStatus == "BUSY") nextMode = "RED";    // Meeting now
  else if (isHere)                   nextMode = "GREEN";  // Available
  else if (timeGone < t15min)        nextMode = "YELLOW"; // Short break
  else                               nextMode = "RED";    // Away for lunch

  // Apply changes only if mode switched
  if (currentMode != nextMode) {
    currentMode = nextMode;
    Serial.println("\n>>> MODE: " + currentMode + " <<<");
    
    // Always ensure strip is ON before changing color (unless mode is OFF)
    if (nextMode != "OFF") {
       sendIR(CMD_ON);
       delay(200); 
    }

    if (nextMode == "RED")         sendIR(CMD_RED);
    else if (nextMode == "GREEN")  sendIR(CMD_GREEN);
    else if (nextMode == "YELLOW") sendIR(CMD_YELLOW);
    else if (nextMode == "FADE")   sendIR(CMD_FADE);
    else if (nextMode == "OFF")    sendIR(CMD_OFF);
  }
}

void sendIR(uint8_t command) {
  // Send NEC protocol command
  IrSender.sendNEC(IR_ADDRESS, command, 0);
  delay(100); 
}

void connectWiFi() {
  Serial.print("Connecting WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println(" OK!");
}
