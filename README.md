# PocketOS

A tiny wearable OS for the ESP32-C6, built on a 128x64 OLED display with 3 buttons.
Clock, Stopwatch, Timer, WiFi Scanner, Tetris and System Info — all in your pocket.

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-C6 Mini (Zero) |
| Display | SSD1306 128x64 OLED (7-pin, SW SPI) |
| Buttons | 3x push buttons |
| WiFi | Built-in (NTP time sync) |

---

## Wiring

### OLED (7-pin SW SPI)

| OLED Pin | ESP32-C6 GPIO |
|----------|---------------|
| GND | GND |
| VCC | 3.3V |
| D0 (CLK) | GPIO 0 |
| D1 (DATA) | GPIO 1 |
| RES | GPIO 2 |
| DC | GPIO 3 |
| CS | GPIO 4 |

### Buttons

| Button | GPIO | Other leg |
|--------|------|-----------|
| UP | GPIO 15 | GND |
| DOWN | GPIO 19 | GND |
| SELECT | GPIO 22 | GND |

No resistors needed — INPUT_PULLUP is used in firmware.

---

## Apps

| App | Description |
|-----|-------------|
| Clock | Real-time clock via NTP WiFi sync. Shows date and day. |
| Stopwatch | Start, pause, lap (3 laps), reset. |
| Timer | Countdown timer with +1 min increments. Flashes on done. |
| WiFi Scan | Scans nearby networks, shows SSID and signal strength. |
| Tetris | Full Tetris with 7 pieces, scoring, levels, next piece preview. |
| System | Uptime, free RAM, WiFi IP, CPU MHz. |

---

## Controls

| Button | Short Press | Long Press |
|--------|-------------|------------|
| UP | Scroll up / context action | Move left (Tetris) |
| DOWN | Scroll down / context action | - |
| SELECT | Enter app / confirm / pause Tetris | Back to menu (always) |

### Per-app controls

| App | UP | DOWN | SELECT |
|-----|-----|------|--------|
| Clock | Toggle seconds | - | - |
| Stopwatch | Lap / Start | Pause / Reset | - |
| Timer | +1 minute | Start / Stop | - |
| WiFi Scan | Scroll up | Scroll down | Rescan |
| Tetris | Rotate | Move right | Pause |
| System | - | - | - |

---

## Setup

### 1. Install Arduino IDE

Download from https://www.arduino.cc/en/software

### 2. Add ESP32 board support

In Arduino IDE go to File -> Preferences and add this URL to Additional Boards Manager URLs:

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Then go to Tools -> Board -> Boards Manager, search for `esp32` and install `esp32 by Espressif Systems`.

### 3. Install libraries

In Tools -> Manage Libraries, install:

- `Adafruit SSD1306` by Adafruit
- `Adafruit GFX Library` by Adafruit
- `NTPClient` by Fabrice Weinberg

### 4. Configure WiFi

Open `PocketOS.ino` and edit these two lines:

```cpp
#define WIFI_SSID  "YOUR_SSID"
#define WIFI_PASS  "YOUR_PASSWORD"
```

Change the timezone offset if needed (default is IST UTC+5:30):

```cpp
#define TZ_OFFSET  19800
```

Common offsets:
- IST (India) = 19800
- UTC = 0
- EST (US East) = -18000
- PST (US West) = -28800
- CET (Europe) = 3600

### 5. Upload

- Tools -> Board -> ESP32C6 Dev Module
- Tools -> Port -> select your port
- Click Upload

---

## Project Structure

```
PocketOS/
  PocketOS.ino     main firmware
  README.md        this file
```

---

## License

MIT License - free to use, modify and share.

---

## Author

Built with ESP32-C6 + OLED + 3 buttons.
Inspired by retro terminal aesthetics and the idea that useful software
does not need a big screen.
