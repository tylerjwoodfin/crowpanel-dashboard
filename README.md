# CrowPanel Hello

Firmware for the **Elecrow CrowPanel 5.79" ESP32-S3 E-Paper Display**. Shows alternating trash duty, a random quote from ZenQuotes, and the incomplete Reminders Grocery list, refreshing every 15 minutes.

[Amazon Link](https://www.amazon.com/dp/B0FX4PDW6M)

## Device

- **Name:** CrowPanel 5.79" ESP32 E-Paper HMI Display
- **Vendor:** Elecrow
- **Display:** 5.79" e-ink, 792×272 pixels, black & white
- **MCU:** ESP32-S3-WROOM-1 (8 MB flash, 8 MB PSRAM)
- **Driver:** GDEY0579T93 (SSD1683)

## Features

- **Trash duty:** Alternates between two names every other Thursday (epoch-week based)
- **Random quote:** Fetched from [ZenQuotes API](https://zenquotes.io/) at each refresh, wrapped to the left of the grocery column
- **Grocery list:** Incomplete items from the Reminders list named Grocery, drawn in the right column
- **Last updated:** Pacific time (PST/PDT), NTP-synced
- **Auto-refresh:** Every 15 minutes
- **Auto-reboot:** Reboots if WiFi is disconnected at a refresh interval, or after 5 consecutive quote-fetch failures
- **GitHub updates:** Every 15 minutes, installs the latest firmware release when it was built from a newer commit

## Grocery list

ice (`192.168.1.102`) serves the incomplete items from the Reminders list named Grocery at `http://192.168.1.102:8765/grocery`. The panel fetches that URL on each refresh and draws the titles to the right of the quote. A missing server shows "offline" in that column and does not count toward the quote-fetch reboot.

The server runs `server/GroceryList.app`, a small EventKit helper. It only reads incomplete titles. It does not complete, edit, or delete reminders. macOS blocks that read until CrowPanel Grocery is allowed under System Settings → Privacy & Security → Reminders. If it is not listed, add `server/GroceryList.app` with the + button.

```bash
~/git/crowpanel-dashboard/server/build_helper.sh
```

```bash
ln -sf ~/git/crowpanel-dashboard/launchd/com.tyler.crowpanel-grocery.plist ~/Library/LaunchAgents/
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.tyler.crowpanel-grocery.plist
```

`server/grocery.py --check` prints how many incomplete items it found.

## Setup

### 1. WiFi credentials

Edit `include/config.h` and set your network:

```c
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"
```

`include/config.h` is gitignored. The GitHub build does not contain these credentials.

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

## Updates

Every 15 minutes the device asks GitHub for the [latest firmware release](https://github.com/tylerjwoodfin/crowpanel-dashboard/releases/latest). The release tag is `fw-` plus the commit SHA that built `firmware.bin`. GitHub rejects a tag that is only the 40-character SHA. The device installs that file only when the tag is a newer commit than the one baked into the running firmware. The same commit, or an older release, is left alone.

The panel cannot compile source. [`.github/workflows/firmware.yml`](.github/workflows/firmware.yml) builds the binary on GitHub Actions when a push to `main` changes `src/`, `include/`, `platformio.ini`, `git_version.py`, or the workflow itself. The short commit on the bottom of the screen is the running build.

Flash once over USB so the updater is on the device. That build copies `include/config.h` into the ESP32's NVS partition. Later GitHub builds leave the WiFi fields empty, so the public `firmware.bin` does not contain the password, and the device keeps using the credentials already stored. Changing WiFi means flashing over USB again.

Until the first release exists, the update check finds nothing and the current firmware keeps running.

## Dependencies

- [GxEPD2](https://github.com/ZinggJM/GxEPD2)
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
