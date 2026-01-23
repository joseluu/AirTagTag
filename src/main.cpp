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
#include "../network_creds.h"

// OLED display for WiFi Kit (SSD1306 on I2C pins 4 and 5)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ 16, /* clock=*/ 15, /* data=*/ 4);

BLEScan* pBLEScan;
AsyncWebServer server(80);

// Store detected AirTags and counts
std::unordered_map<std::string, int> airtagCounts; // To store AirTag addresses and detection counts
std::unordered_map<std::string, int> airtagRSSI;   // To store AirTag RSSI values
std::unordered_map<std::string, String> airtagTrend; // To store AirTag trends (increasing, decreasing, stable)
std::unordered_map<std::string, String> airtagDetails; // To store additional AirTag details
std::unordered_map<std::string, float> airtagDistance; // To store AirTag distances
bool isScanning = false; // Flag to indicate scanning state
unsigned long lastDisplayUpdate = 0; // For updating OLED periodically
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000; // Update display every second

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

// Function to update OLED display with BLE devices summary
void updateOLEDDisplay() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  
  int deviceCount = airtagCounts.size();
  
  if (deviceCount == 0) {
    u8g2.drawStr(0, 15, "Scanning...");
  } else {
    char buffer[128];
    int displayCount = 0;
    int yPos = 12;
    for (const auto& entry : airtagCounts) {
      if (yPos > 64) break;
      
      int rssi = airtagRSSI[entry.first];
      float distance = airtagDistance[entry.first];
      String addr = String(entry.first.c_str());
      addr = addr.substring(addr.length() - 5); // Show last 5 chars of MAC
      
      // Determine distance trend indicator
      char trendChar = ' ';
      if (airtagTrend[entry.first] == "red") {
        trendChar = '+'; // Distance narrowing (RSSI increasing)
      } else if (airtagTrend[entry.first] == "blue") {
        trendChar = '-'; // Distance lengthening (RSSI decreasing)
      }
      
      snprintf(buffer, sizeof(buffer), "%s %d dBm %.1f m%c", addr.c_str(), rssi, distance, trendChar);
      u8g2.drawStr(0, yPos, buffer);
      yPos += 12;
      displayCount++;
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
  if (!MDNS.begin("airtag-scanner")) {
    Serial.println("ERROR: Failed to start mDNS responder");
    return;
  }
  
  MDNS.addService("http", "tcp", 80);
  Serial.println("\n=== mDNS Service Advertised ===");
  Serial.println("Service: http");
  Serial.println("Hostname: airtag-scanner.local");
  Serial.printf("Access via: http://airtag-scanner.local\n");
  Serial.println("================================\n");
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  char buffer[50];
  snprintf(buffer, sizeof(buffer), "mDNS: airtag-scanner.local");
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
  
  // Connect to WiFi router
  connectToWiFiRouter();
  
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
    for (const auto& entry : airtagCounts) {
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
}