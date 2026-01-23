   Summary - Task 4: mDNS Service Advertising

   I've successfully implemented task 4 with the following changes:

   Changes Made:

     - Added mDNS Library - Included <ESPmDNS.h> header
     - Removed Hard-coded WiFi AP - Removed the WiFi.softAP() configuration that was forcing IP 192.168.11.29
     - Added setupMDNS() Function that:
       - Initializes mDNS responder with hostname airtag-scanner
       - Advertises HTTP service on port 80
       - Logs mDNS details to serial console
       - Shows mDNS hostname on OLED display
     - Uses DHCP IP from Router - The device now automatically gets IP from the router's DHCP server when connected to WiFi
     - Added Auto-Refresh to Web Page - Added meta refresh tag (<meta http-equiv="refresh" content="15">) to auto-refresh the web page every 15 seconds
     - Updated Web Server Route - Now uses the dynamically assigned DHCP IP instead of hard-coded address

   How It Works:

     - Device connects to your WiFi router (credentials from network_creds.h)
     - Automatically receives IP from DHCP
     - mDNS service advertises as airtag-scanner.local
     - Web page accessible at: http://airtag-scanner.local
     - Web page auto-refreshes every 15 seconds to show updated AirTag data

      Summary - Task 5: TTGO Device Support

   I've successfully implemented task 5 with support for TTGO devices:

   Changes Made:

     - Added TTGO Backlight Pin Definition - Added constant TTGO_BACKLIGHT_PIN = 14 for GPIO 14 where the LED backlight control signal is located
     - Initialized Backlight in setup() - Added code to:
       - Set GPIO 14 as an OUTPUT pin
       - Set it HIGH to activate the backlight
       - Log the activation to serial console

   How It Works:

     - The TTGO device has an LED backlight controlled via GPIO 14
     - On startup, the code now automatically activates the backlight by setting GPIO 14 HIGH
     - This ensures the OLED display is visible on TTGO devices
     - Standard WiFi Kit boards are also compatible as GPIO 14 is set as an output with no impact

   Code Changes:

     // Added constant
     const int TTGO_BACKLIGHT_PIN = 14;

     // Added in setup() function
     pinMode(TTGO_BACKLIGHT_PIN, OUTPUT);
     digitalWrite(TTGO_BACKLIGHT_PIN, HIGH);
     Serial.println("TTGO Backlight activated on GPIO 14");

Change 6

Implementation Summary

   Added NTP Time Sync:

     - Imported <time.h> for time functions
     - Created syncNTP() function that syncs time with NTP server daily
     - Calls syncNTP() during setup and then every 24 hours in the loop

   Tracking and Display for Sato Device:

     - Added SATO_MAC constant with the device's MAC address (d6:e8:3b:00:e1:4f)
     - Added SATO_TIMEOUT_MS constant set to 5 minutes (300000 ms)
     - Added airtagLastSeen map to track when each device was last detected

   OLED Display Changes:

     - Reserves the first line for the Sato device, always showing "Sato" instead of MAC address
     - Displays RSSI/distance/trend when detected within 5 minutes
     - Shows "Sato out: HH:MM:SS" when not detected for 5 minutes

   Web Page Changes:

     - Displays "Satoshi (Sato)" at the top with special formatting
     - Shows same timeout behavior (out: HH:MM:SS) after 5 minutes

   Time Formatting:

     - Created formatTime() helper function to display time as HH:MM:SS
     - Used for showing the last time Sato signal was lost

   The implementation preserves all existing functionality while adding the special handling for the Sato device with time-based tracking and NTP
   synchronization.

   The "Signal sato" feature is now running on your ESP32 with:

     - NTP time synchronization daily
     - Sato device (d6:e8:3b:00:e1:4f) displayed on the first line of the OLED
     - 5-minute timeout showing "out: HH:MM:SS" when not detected
     - Web page displays "Satoshi (Sato)" with the same timeout behavior

Change 7

        Summary of Changes:

   1. CET Timezone Configuration

     - Updated NTP sync to use CET (UTC+1) with DST support (UTC+2)
     - Changed gmtOffset_sec from 0 to 3600 (UTC+1)
     - Added daylightOffset_sec of 3600 for automatic DST handling

   2. Sato Lost/Reacquired Time Tracking

     - Added satoLastLost variable to track when Sato signal is lost
     - Added satoLastReacquired variable to track when Sato signal is reacquired
     - Detects when Sato goes offline for 5+ minutes and sets lost time
     - Detects when Sato comes back online and updates reacquired time

   3. OLED Display Updates

     - Second line now shows "in: HH:MM:SS" with the time Sato signal was reacquired
     - Both times now display in CET instead of UTC

   4. Web Page Updates

     - Added second line for Satoshi showing reacquired time
     - Format: "in: HH:MM:SS" on a separate list item

   5. mDNS Hostname Change

     - Changed from "airtag-scanner.local" to "mysato.local"
     - Updated display and serial output to reflect new hostname

   6. Enhanced Loop Logic

     - Added detection for when Sato signal is lost (after 5-minute timeout)
     - Logs signal loss time to serial console

Change 8
   Display Layout Fix

     - Line 1: Sato device information (RSSI/distance when online, "out: HH:MM:SS" when offline)
     - Line 2: Exclusively reserved for Sato reacquisition time
       - Shows "in: HH:MM:SS" when Sato signal is reacquired
       - Blank if no reacquisition yet (no other devices shown here)
     - Line 3+: Other detected devices (never uses line 2)

   Key Implementation Details:

     - Second line is always reserved for Sato reacquisition time
     - yPos is incremented by 12 regardless of whether reacquisition info exists
     - Other devices only start displaying from line 3 onwards
     - When no reacquisition has occurred, line 2 remains blank

Change 9

   Initial Acquisition Handling

     - When Sato is first detected (initial acquisition), satoLastReacquired is now set immediately
     - This causes the second line to display "in: HH:MM:SS" right from the first detection, just like when it's reacquired after being lost
     - Added serial logging for initial acquisition: "Sato signal initially acquired at: HH:MM:SS"

   Logic Flow:

     - First Detection (initial acquisition):
       - Check if airtagCounts[SATO_MAC] doesn't exist yet
       - Set satoLastReacquired = time(nullptr)
       - Display "in: HH:MM:SS" on second line
     - Reacquisition (after being lost):
       - Check if satoLastLost > 0 and satoLastReacquired < satoLastLost
       - Set satoLastReacquired = time(nullptr)
       - Display "in: HH:MM:SS" on second line
     - Display Behavior:
       - Line 1: Sato status (RSSI/distance or "out: HH:MM:SS")
       - Line 2: "in: HH:MM:SS" (from first detection onwards)
       - Line 3+: Other devices

