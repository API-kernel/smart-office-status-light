# 🚦 Smart Office Status Light

An automated "Do Not Disturb" light for your workspace powered by **ESP32**.
It automatically changes the color of an LED strip based on your **Google Calendar** status and your physical **Presence** (detected via Bluetooth Low Energy).

![Platform](https://img.shields.io/badge/Platform-ESP32-blue)
![License](https://img.shields.io/badge/License-MIT-green)
![Status](https://img.shields.io/badge/Status-Active-success)

## ✨ Features

*   **Fully Autonomous:** No external servers (Home Assistant/MQTT) required. Runs entirely on the ESP32.
*   **Presence Detection:** Uses your smartphone as an **iBeacon**. The light reacts instantly when you walk in or out.
*   **Google Calendar Sync:** Checks your schedule in real-time. If you have a meeting, the light turns Red.
*   **Non-Invasive:** Controls standard 12V RGB LED strips via **Infrared (IR)**. No need to cut wires or solder into the strip controller.
*   **Smart Logic:**
    *   🟢 **Green:** You are present and **Free** (no events).
    *   🔴 **Red:** You are **Busy** (meeting) OR you have been away for > 15 mins.
    *   🟡 **Yellow:** You stepped away for a short break (< 15 mins).
    *   🌈 **Fade / Off:** End of the work day (absent > 4 hours).

## 🛠 Hardware Required

1.  **ESP32 Development Board** (e.g., ESP32 DevKit V1, NodeMCU-32S).
2.  **IR LED** (940nm, 5mm recommended).
3.  **Resistor** (100 Ohm or 220 Ohm).
4.  Standard RGB LED Strip with an IR Remote.
5.  Android/iOS Smartphone.

### Wiring
A simple circuit to emulate the remote control:
*   **ESP32 GPIO 4** (or any digital pin) --> **Resistor** --> **IR LED Anode (+)**
*   **ESP32 GND** --> **IR LED Cathode (-)**

---

## 🚀 Installation & Setup

### 1. Google Apps Script (The API)
To let the ESP32 check your calendar securely without complex OAuth, we use a simple Google Script proxy.

1.  Go to [script.google.com](https://script.google.com/) and create a **New Project**.
2.  Paste the following code:
    ```javascript
    function doGet(e) {
      var cal = CalendarApp.getDefaultCalendar();
      var now = new Date();
      var events = cal.getEventsForDay(now);
      var status = "FREE";
      
      for (var i = 0; i < events.length; i++) {
        var start = events[i].getStartTime();
        var end = events[i].getEndTime();
        // Check if current time is within an event
        if (now >= start && now <= end) {
          status = "BUSY";
          break;
        }
      }
      return ContentService.createTextOutput(status);
    }
    ```
3.  Click **Deploy** -> **New Deployment**.
4.  Select type: **Web app**.
5.  **Important:** Set "Who has access" to **"Anyone"**.
6.  Click **Deploy** and copy the generated `Web App URL` (ends with `/exec`).

### 2. Smartphone Setup (The Beacon)
1.  Install a Beacon app (e.g., **"Beacon Simulator"** for Android).
2.  Create a new **iBeacon**.
3.  Enable broadcasting.
4.  Note down the UUID or use the scanner sketch provided in this repo to find your unique ID.

### 3. ESP32 Firmware
1.  Install **Arduino IDE**.
2.  Install required libraries via Library Manager:
    *   `NimBLE-Arduino` (Crucial for memory optimization with SSL).
    *   `IRremote` (By Armin Joachimsmeyer).
3.  Open `main.ino`.
4.  **Edit the Configuration Section** at the top:
    *   Wi-Fi Credentials.
    *   Google Script URL.
    *   Your Beacon ID.
    *   IR Codes (if your remote differs from the NEC standard used here).
5.  Select Board: **ESP32 Dev Module**.
6.  Select Partition Scheme: **Huge APP (3MB No OTA)** (Required to fit SSL + BLE).
7.  Upload!

---

## 🔧 Troubleshooting

*   **Google Error -1:** This usually means "Out of Memory". Ensure you are using the `NimBLE` library instead of the standard `BLEDevice`, and that you selected the "Huge APP" partition scheme.
*   **Light not reacting:** Check the IR LED polarity and ensure it points towards the LED strip controller. You may need to update the IR HEX codes in the sketch (use the `IRrecvDump` example from the IRremote library to decode your specific remote).
*   **Compilation Error:** Ensure you are using ESP32 Board version **2.0.17** or compatible. Versions 3.0+ might require syntax adjustments.

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
