#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <U8g2lib.h>
#include <unordered_map>
#include <map>
#include <time.h>
// Place for your network credentials
#include "../network_creds.h"
//const char* ssid     = "wifi_name";
//const char* password = "wifi_passwd";

// OLED display configuration - SSD1306 on I2C pins 4 and 5
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ 16, /* clock=*/ 15, /* data=*/ 4);

// TTGO backlight pin (GPIO 4)
const int TTGO_BACKLIGHT_PIN = 4;

BLEScan* pBLEScan;
AsyncWebServer server(80);

// Sato device MAC address
const std::string SATO_MAC = "d6:e8:3b:00:e1:4f";
const unsigned long SATO_TIMEOUT_MS = 300000; // 5 minutes
unsigned long satoLastLost = 0; // When Sato signal was lost
unsigned long satoLastReacquired = 0; // When Sato signal was reacquired

// Store detected AirTags and counts
std::unordered_map<std::string, int> airtagCounts; // To store AirTag addresses and detection counts
std::unordered_map<std::string, int> airtagRSSI;   // To store AirTag RSSI values
std::unordered_map<std::string, String> airtagTrend; // To store AirTag trends (increasing, decreasing, stable)
std::unordered_map<std::string, String> airtagDetails; // To store additional AirTag details
std::unordered_map<std::string, float> airtagDistance; // To store AirTag distances
std::unordered_map<std::string, unsigned long> airtagLastSeen; // Track last seen time for each device
bool isScanning = false; // Flag to indicate scanning state
unsigned long lastDisplayUpdate = 0; // For updating OLED periodically
unsigned long lastNTPSync = 0; // Track last NTP sync time
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000; // Update display every second
const unsigned long NTP_SYNC_INTERVAL = 86400000; // Sync NTP daily (24 hours)

// Function to estimate distance based on RSSI
float calculateDistance(int rssi) {
  int txPower = -59; // Typical value for 1 meter
  if (rssi == 0) {
    return -1.0; // If we cannot determine distance
  }
  float ratio = rssi * 1.0 / txPower;
  if (ratio < 1.0) {
    return pow(ratio, 10);
  } else {
    return (0.89976) * pow(ratio, 7.7095) + 0.111;
  }
}

// Function to format time as HH:MM:SS
String formatTime(time_t t) {
  struct tm* timeinfo = localtime(&t);
  char buffer[10];
  strftime(buffer, sizeof(buffer), "%H:%M:%S", timeinfo);
  return String(buffer);
}

// Function to sync NTP time
void syncNTP() {
  const char* ntpServer = "pool.ntp.org";
  const long gmtOffset_sec = 3600; // CET is UTC+1
  const int daylightOffset_sec = 3600; // DST adds another hour
  
  Serial.println("Syncing NTP time (CET timezone)...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  time_t now = time(nullptr);
  int attempts = 0;
  while (now < 24 * 3600 && attempts < 30) {
    delay(500);
    now = time(nullptr);
    attempts++;
  }
  
  if (now > 24 * 3600) {
    Serial.printf("NTP Time synced: %s\n", formatTime(now).c_str());
    lastNTPSync = millis();
  } else {
    Serial.println("NTP time sync failed");
  }
}

// Function to update OLED display with BLE devices summary
void updateOLEDDisplay() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  
  char buffer[128];
  int yPos = 12;
  
  // First line: Sato device if available
  if (airtagCounts.find(SATO_MAC) != airtagCounts.end()) {
    unsigned long timeSinceSeen = millis() - airtagLastSeen[SATO_MAC];
    if (timeSinceSeen < SATO_TIMEOUT_MS) {
      int rssi = airtagRSSI[SATO_MAC];
      float distance = airtagDistance[SATO_MAC];
      
      char trendChar = ' ';
      if (airtagTrend[SATO_MAC] == "red") {
        trendChar = '+';
      } else if (airtagTrend[SATO_MAC] == "blue") {
        trendChar = '-';
      }
      
      snprintf(buffer, sizeof(buffer), "Sato %d dBm %.1f m%c", rssi, distance, trendChar);
    } else {
      time_t lostTime = time(nullptr) - (timeSinceSeen / 1000);
      snprintf(buffer, sizeof(buffer), "Sato out: %s", formatTime(lostTime).c_str());
    }
  } else {
    snprintf(buffer, sizeof(buffer), "Sato not found");
  }
  u8g2.drawStr(0, yPos, buffer);
  yPos += 12;
  
  // Second line: Always reserved for Sato reacquired time
  // Leave blank if no reacquisition yet, never use for other devices
  if (satoLastReacquired > 0) {
    snprintf(buffer, sizeof(buffer), "in: %s", formatTime(satoLastReacquired).c_str());
    u8g2.drawStr(0, yPos, buffer);
  }
  yPos += 12; // Always skip this line (blank if no reacquisition)
  
  // Other devices start from line 3
  int deviceCount = airtagCounts.size();
  
  if (deviceCount > 1 || (deviceCount == 1 && airtagCounts.find(SATO_MAC) == airtagCounts.end())) {
    for (const auto& entry : airtagCounts) {
      if (entry.first == SATO_MAC) continue; // Skip Sato, already displayed
      if (yPos > 128) break;
      
      int rssi = airtagRSSI[entry.first];
      float distance = airtagDistance[entry.first];
      String addr = String(entry.first.c_str());
      addr = addr.substring(addr.length() - 5);
      
      char trendChar = ' ';
      if (airtagTrend[entry.first] == "red") {
        trendChar = '+';
      } else if (airtagTrend[entry.first] == "blue") {
        trendChar = '-';
      }
      
      snprintf(buffer, sizeof(buffer), "%s %d dBm %.1f m%c", addr.c_str(), rssi, distance, trendChar);
      u8g2.drawStr(0, yPos, buffer);
      yPos += 12;
    }
  }
  
  u8g2.sendBuffer();
}

// Function to connect to WiFi router with logging and error handling
void connectToWiFiRouter() {
  Serial.println("\n=== WiFi Router Connection ===");
  Serial.printf("Attempting to connect to SSID: %s\n", ssid);
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 15, "Connecting to WiFi...");
  u8g2.sendBuffer();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  const int maxAttempts = 20; // ~10 seconds timeout (20 * 500ms)
  
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("SUCCESS: WiFi connection established!");
    Serial.printf("Connected to: %s\n", ssid);
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Signal Strength (RSSI): %d dBm\n", WiFi.RSSI());
    Serial.println("===============================\n");
    
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 15, "WiFi Connected!");
    u8g2.sendBuffer();
    delay(2000);
  } else {
    Serial.println("\nFAILURE: WiFi connection failed!");
    Serial.printf("SSID: %s\n", ssid);
    Serial.printf("Status Code: %d\n", WiFi.status());
    Serial.println("Possible causes:");
    Serial.println("- Invalid SSID or password");
    Serial.println("- Router not found or out of range");
    Serial.println("- Connection timeout");
    Serial.println("Restarting microcontroller in 5 seconds...");
    Serial.println("===============================\n");
    
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 15, "WiFi Failed!");
    u8g2.drawStr(0, 30, "Restarting in 5s...");
    u8g2.sendBuffer();
    
    delay(5000);
    ESP.restart();
  }
}

// Function to initialize mDNS service
void setupMDNS() {
  if (!MDNS.begin("mysato")) {
    Serial.println("ERROR: Failed to start mDNS responder");
    return;
  }
  
  MDNS.addService("http", "tcp", 80);
  Serial.println("\n=== mDNS Service Advertised ===");
  Serial.println("Service: http");
  Serial.println("Hostname: mysato.local");
  Serial.printf("Access via: http://mysato.local\n");
  Serial.println("================================\n");
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  char buffer[50];
  snprintf(buffer, sizeof(buffer), "mDNS: mysato.local");
  u8g2.drawStr(0, 15, buffer);
  u8g2.sendBuffer();
  delay(2000);
}

// Custom callback to handle detected devices
class CustomAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    std::string address = advertisedDevice.getAddress().toString();
    String details = "";

    // Check for AirTag using manufacturer data
    if (advertisedDevice.haveManufacturerData()) {
      std::string manufacturerData = advertisedDevice.getManufacturerData();
      if (manufacturerData.size() > 2 && manufacturerData[0] == 0x4C && manufacturerData[1] == 0x00) {
        int rssi = advertisedDevice.getRSSI();
        float distance = calculateDistance(rssi);

        // Update trend based on previous RSSI
        if (airtagRSSI.find(address) != airtagRSSI.end()) {
          if (rssi > airtagRSSI[address]) {
            airtagTrend[address] = "red"; // Increasing
          } else if (rssi < airtagRSSI[address]) {
            airtagTrend[address] = "blue"; // Decreasing
          } else {
            airtagTrend[address] = "green"; // Stable
          }
        } else {
          airtagTrend[address] = "green"; // Default to stable for new devices
        }

        // Update RSSI and counts
        airtagRSSI[address] = rssi;
        airtagDistance[address] = distance;
        airtagLastSeen[address] = millis(); // Track when device was last seen
        
        // Track Sato reacquired time if it was previously lost
        if (address == SATO_MAC) {
          if (satoLastLost > 0 && satoLastReacquired < satoLastLost) {
            satoLastReacquired = time(nullptr); // Update when signal was reacquired
            Serial.printf("Sato signal reacquired at: %s\n", formatTime(satoLastReacquired).c_str());
          }
        }
        
        if (airtagCounts.find(address) == airtagCounts.end()) {
          airtagCounts[address] = 1;
        } else {
          airtagCounts[address]++;
        }

        // Construct details
        details += "RSSI: " + String(rssi) + " | Distance: " + String(distance) + " meters";
        if (advertisedDevice.haveName()) {
          details += " | Name: " + String(advertisedDevice.getName().c_str());
        }
        if (advertisedDevice.haveServiceUUID()) {
          details += " | Service UUID: " + String(advertisedDevice.getServiceUUID().toString().c_str());
        }
        airtagDetails[address] = details;

        // Print AirTag details
        Serial.printf("AirTag Detected - Address: %s | %s | Count: %d | Trend: %s\n", 
                      address.c_str(), details.c_str(), airtagCounts[address], airtagTrend[address].c_str());
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE Scanner with OLED Display...");
  
  // Initialize OLED display
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 15, "Initializing...");
  u8g2.sendBuffer();
  
  // Activate TTGO backlight
  pinMode(TTGO_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(TTGO_BACKLIGHT_PIN, HIGH);
  Serial.println("TTGO Backlight activated on GPIO 14");
  
  // Connect to WiFi router
  connectToWiFiRouter();
  
  // Sync NTP time
  syncNTP();
  
  BLEDevice::init("ESP32_BLE_Scanner");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new CustomAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); // Active scan for more data
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(50);

  // Setup mDNS service
  setupMDNS();

  // Define Web Server Routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<html><head>";
    html += "<title>AirTag Scanner</title>";
    html += "<meta http-equiv=\"refresh\" content=\"15\">"; // Auto-refresh every 15 seconds
    html += "</head><body><h1>BLE AirTag Scanner</h1>";
    html += "<h2>AirTags Detected:</h2><ul>";
    
    // Display Satoshi (Sato) device first
    if (airtagCounts.find(SATO_MAC) != airtagCounts.end()) {
      unsigned long timeSinceSeen = millis() - airtagLastSeen[SATO_MAC];
      html += "<li><strong>Satoshi (Sato)</strong>: ";
      if (timeSinceSeen < SATO_TIMEOUT_MS) {
        html += "RSSI: " + String(airtagRSSI[SATO_MAC]) + " dBm | ";
        html += "Distance: " + String(airtagDistance[SATO_MAC]) + " m | ";
        html += "Trend: " + airtagTrend[SATO_MAC];
      } else {
        time_t lostTime = time(nullptr) - (timeSinceSeen / 1000);
        html += "out: " + formatTime(lostTime);
      }
      html += "</li>";
      
      // Show reacquired time on second line for Satoshi
      if (satoLastReacquired > 0) {
        html += "<li><strong>Reacquired:</strong> in: " + formatTime(satoLastReacquired) + "</li>";
      }
    }
    
    // Display other devices
    for (const auto& entry : airtagCounts) {
      if (entry.first == SATO_MAC) continue; // Skip Satoshi, already displayed
      String trendColor = airtagTrend[entry.first].c_str();
      html += "<li><span style='color:" + trendColor + "'>";
      html += "Address: " + String(entry.first.c_str()) + " | Count: " + String(entry.second);
      html += " | " + airtagDetails[entry.first];
      html += "</span></li>";
    }
    html += "</ul><button onclick=\"location.href='/clear'\">Clear</button></body></html>";
    request->send(200, "text/html", html);
  });

  server.on("/clear", HTTP_GET, [](AsyncWebServerRequest *request) {
    airtagCounts.clear();
    airtagRSSI.clear();
    airtagTrend.clear();
    airtagDetails.clear();
    airtagDistance.clear();
    airtagLastSeen.clear();
    satoLastLost = 0;
    satoLastReacquired = 0;
    request->redirect("/");
  });

  server.begin();

  // Start continuous scanning
  isScanning = true;
  xTaskCreatePinnedToCore(
    [] (void* pvParameters) {
      while (true) {
        pBLEScan->start(5, false);
        delay(100);
      }
    },
    "BLEScanTask",
    4096,
    nullptr,
    1,
    nullptr,
    0
  );
}

void loop() {
  // Update OLED display periodically
  unsigned long currentTime = millis();
  if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    updateOLEDDisplay();
    lastDisplayUpdate = currentTime;
  }
  
  // Sync NTP time daily
  if (currentTime - lastNTPSync >= NTP_SYNC_INTERVAL) {
    syncNTP();
  }
  
  // Track when Sato signal is lost
  if (airtagCounts.find(SATO_MAC) != airtagCounts.end()) {
    unsigned long timeSinceSeen = millis() - airtagLastSeen[SATO_MAC];
    if (timeSinceSeen >= SATO_TIMEOUT_MS && satoLastLost == 0) {
      satoLastLost = time(nullptr); // Mark when signal was lost
      Serial.printf("Sato signal lost at: %s\n", formatTime(satoLastLost).c_str());
    }
  }
}