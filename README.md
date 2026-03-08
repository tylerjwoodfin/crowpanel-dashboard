# CrowPanel Hello

Firmware for the **Elecrow CrowPanel 5.79" ESP32-S3 E-Paper Display**. Shows alternating trash duty and a random quote from ZenQuotes, refreshing every 15 minutes.

[Amazon Link](https://www.amazon.com/dp/B0FX4PDW6M)

## Device

- **Name:** CrowPanel 5.79" ESP32 E-Paper HMI Display
- **Vendor:** Elecrow
- **Display:** 5.79" e-ink, 792×272 pixels, black & white
- **MCU:** ESP32-S3-WROOM-1 (8 MB flash, 8 MB PSRAM)
- **Driver:** GDEY0579T93 (SSD1683)

## Features

- **Trash duty:** Alternates between two names every other Thursday (epoch-week based)
- **Random quote:** Fetched from [ZenQuotes API](https://zenquotes.io/) at each refresh
- **Last updated:** Pacific time (PST/PDT), NTP-synced
- **Auto-refresh:** Every 15 minutes

## Setup

### 1. WiFi credentials

Edit `include/config.h` and set your network:

```c
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"
```

Consider adding `include/config.h` to `.gitignore` if it contains secrets.

### 2. Build & upload

```bash
# Install dependencies (first time)
pio pkg install

# Build and upload
pio run -t upload -e crowpanel
```

If `pio` is not in your PATH, use the full path to your PlatformIO installation (e.g. `~/.platformio/penv/bin/pio`).

### 3. Serial monitor (optional)

Open the serial monitor at 115200 baud to see boot logs and quote fetches:

```bash
pio device monitor -b 115200
```

## Environments

| Environment | Purpose |
|-------------|---------|
| `boot_test` | Minimal Serial-only test. Use to verify the ESP32 boots before display code. |
| `crowpanel` | Full display with WiFi, quote fetch, and trash duty. |

Build for a specific env:

```bash
pio run -e boot_test    # Boot test only
pio run -e crowpanel    # Full display
```

## Pinout

| Function | GPIO |
|----------|------|
| CS | 45 |
| DC | 46 |
| RST | 47 |
| BUSY | 48 |
| SCK | 12 |
| MOSI | 11 |
| Power | 7 |

## Trash duty logic

Trash week alternates every other Thursday, calculated from the Unix epoch week (weeks since Jan 1, 1970). Even weeks = one person, odd weeks = the other.

## Timezone

Configured for Pacific Time (PST/PDT) with automatic daylight saving. Change the timezone string in `setup()` if needed (e.g. `EST5EDT,M3.2.0,M11.1.0` for Eastern).

## Dependencies

- [GxEPD2](https://github.com/ZinggJM/GxEPD2)
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
