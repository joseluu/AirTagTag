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
// Cat customization
#include "../include/airtag_customisation.h"
//const char* ssid     = "wifi_name";
//const char* password = "wifi_passwd";

// OLED display configuration - SSD1306 on I2C pins 4 and 5
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ 16, /* clock=*/ 15, /* data=*/ 4);

// TTGO backlight pin (GPIO 4)
const int TTGO_BACKLIGHT_PIN = 4;

BLEScan* pBLEScan;
AsyncWebServer server(80);

// Cat tracking: maps MAC address to {lastLost time, lastReacquired time}
std::unordered_map<std::string, unsigned long> catLastLost;
std::unordered_map<std::string, unsigned long> catLastReacquired;

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
  
  Serial.printf("Syncing NTP time (Timezone offset: UTC+%d)...\n", GMT_OFFSET_SEC / 3600);
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, ntpServer);
  
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
  int catLineIndex = 0;
  
  // Display each configured cat first
  for (const auto& cat : CATS) {
    if (catLineIndex >= 2) break; // Only display first 2 cats (lines 1 and 2)
    
    if (airtagCounts.find(cat.macAddress) != airtagCounts.end()) {
      unsigned long timeSinceSeen = millis() - airtagLastSeen[cat.macAddress];
      if (timeSinceSeen < cat.timeoutMs) {
        int rssi = airtagRSSI[cat.macAddress];
        float distance = airtagDistance[cat.macAddress];
        
        char trendChar = ' ';
        if (airtagTrend[cat.macAddress] == "red") {
          trendChar = '+';
        } else if (airtagTrend[cat.macAddress] == "blue") {
          trendChar = '-';
        }
        
        snprintf(buffer, sizeof(buffer), "%s %d dBm %.1f m%c", 
                 cat.displayName.c_str(), rssi, distance, trendChar);
      } else {
        time_t lostTime = time(nullptr) - (timeSinceSeen / 1000);
        snprintf(buffer, sizeof(buffer), "%s out: %s", 
                 cat.displayName.c_str(), formatTime(lostTime).c_str());
      }
    } else {
      snprintf(buffer, sizeof(buffer), "%s not found", cat.displayName.c_str());
    }
    
    u8g2.drawStr(0, yPos, buffer);
    yPos += 12;
    
    // Second line: Reserved for reacquired time of this cat
    // Leave blank if no reacquisition yet, never use for other devices
    if (catLastReacquired.find(cat.macAddress) != catLastReacquired.end() && 
        catLastReacquired[cat.macAddress] > 0) {
      snprintf(buffer, sizeof(buffer), "in: %s", 
               formatTime(catLastReacquired[cat.macAddress]).c_str());
      u8g2.drawStr(0, yPos, buffer);
    }
    yPos += 12; // Always skip this line (blank if no reacquisition)
    catLineIndex++;
  }
  
  // Other devices start from line 3 onwards
  for (const auto& entry : airtagCounts) {
    // Skip configured cats, they are already displayed
    bool isCat = false;
    for (const auto& cat : CATS) {
      if (entry.first == cat.macAddress) {
        isCat = true;
        break;
      }
    }
    if (isCat) continue;
    
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
  std::string hostname = getMDNSHostname();
  if (!MDNS.begin(hostname.c_str())) {
    Serial.println("ERROR: Failed to start mDNS responder");
    return;
  }
  
  MDNS.addService("http", "tcp", 80);
  Serial.println("\n=== mDNS Service Advertised ===");
  Serial.println("Service: http");
  Serial.printf("Hostname: %s.local\n", hostname.c_str());
  Serial.printf("Access via: http://%s.local\n", hostname.c_str());
  Serial.println("================================\n");
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  char buffer[50];
  snprintf(buffer, sizeof(buffer), "mDNS: %s.local", hostname.c_str());
  u8g2.drawStr(0, 15, buffer);
  u8g2.sendBuffer();
  delay(2000);
}

// Custom callback to handle detected devices
class CustomAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // Legacy tracker detection workaround: check manufacturer data for known legacy patterns
    std::string manufacturerData = advertisedDevice.getManufacturerData();
    // TODO: For full support, patch BLE library to expose raw advertisement data and flags
    if (manufacturerData.length() > 0) {
      // Example: check for known legacy tracker signature (customize as needed)
      if (manufacturerData.find("tkr") != std::string::npos) {
        Serial.printf("TrackR tracker detected (manufacturer data): %s\n", advertisedDevice.getAddress().toString().c_str());
        // Optionally add to display or tracking logic here
      }
    }
    // Existing AirTag and BLE device logic below...

    std::string address = advertisedDevice.getAddress().toString();
    String details = "";

    Serial.printf("Address Detected - Address: %s \n", 
                      address.c_str());
    // Check for AirTag using manufacturer data
    if (advertisedDevice.haveManufacturerData()) {
      std::string manufacturerData = advertisedDevice.getManufacturerData();
          Serial.printf("Address with manufacturer data - Address: %s md: %d %d\n", 
                      address.c_str(), (uint8_t)manufacturerData[0], (uint8_t)manufacturerData[1]);
      if (manufacturerData.size() > 2 && 
          (manufacturerData[0] == 0x4C || // apple
            manufacturerData[0] == 0x00) && // TrackR bravo
            manufacturerData[1] == 0x00) {
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
        
        // Track cat reacquired/initial acquisition time
        const CatConfig* cat = getCatByMAC(address);
        if (cat) {
          // On initial acquisition (first time seeing this cat)
          if (airtagCounts.find(address) == airtagCounts.end()) {
            catLastReacquired[address] = time(nullptr); // Set on first detection
            Serial.printf("%s signal initially acquired at: %s\n", 
                         cat->displayName.c_str(), formatTime(catLastReacquired[address]).c_str());
          }
          // On reacquisition (after being lost)
          else if (catLastLost.find(address) != catLastLost.end() && 
                   catLastLost[address] > 0 && 
                   catLastReacquired[address] < catLastLost[address]) {
            catLastReacquired[address] = time(nullptr); // Update when signal was reacquired
            Serial.printf("%s signal reacquired at: %s\n", 
                         cat->displayName.c_str(), formatTime(catLastReacquired[address]).c_str());
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
    
    // Display each configured cat first
    for (const auto& cat : CATS) {
      if (airtagCounts.find(cat.macAddress) != airtagCounts.end()) {
        unsigned long timeSinceSeen = millis() - airtagLastSeen[cat.macAddress];
        html += "<li><strong>" + String(cat.webName.c_str()) + "</strong>: ";
        if (timeSinceSeen < cat.timeoutMs) {
          html += "RSSI: " + String(airtagRSSI[cat.macAddress]) + " dBm | ";
          html += "Distance: " + String(airtagDistance[cat.macAddress]) + " m | ";
          html += "Trend: " + airtagTrend[cat.macAddress];
        } else {
          time_t lostTime = time(nullptr) - (timeSinceSeen / 1000);
          html += "out: " + formatTime(lostTime);
        }
        html += "</li>";
        
        // Show reacquired time for this cat
        if (catLastReacquired.find(cat.macAddress) != catLastReacquired.end() && 
            catLastReacquired[cat.macAddress] > 0) {
          html += "<li><strong>Reacquired:</strong> in: " + 
                  formatTime(catLastReacquired[cat.macAddress]) + "</li>";
        }
      }
    }
    
    // Display other devices
    for (const auto& entry : airtagCounts) {
      // Skip configured cats, already displayed
      bool isCat = false;
      for (const auto& cat : CATS) {
        if (entry.first == cat.macAddress) {
          isCat = true;
          break;
        }
      }
      if (isCat) continue;
      
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
    catLastLost.clear();
    catLastReacquired.clear();
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
  
  // Track when cat signals are lost
  for (const auto& cat : CATS) {
    if (airtagCounts.find(cat.macAddress) != airtagCounts.end()) {
      unsigned long timeSinceSeen = millis() - airtagLastSeen[cat.macAddress];
      if (timeSinceSeen >= cat.timeoutMs && 
          (catLastLost.find(cat.macAddress) == catLastLost.end() || catLastLost[cat.macAddress] == 0)) {
        catLastLost[cat.macAddress] = time(nullptr); // Mark when signal was lost
        Serial.printf("%s signal lost at: %s\n", 
                     cat.displayName.c_str(), formatTime(catLastLost[cat.macAddress]).c_str());
      }
    }
  }
}