/**
 * CrowPanel 5.79" - trash duty + quote display
 *
 * Displays trash duty and a random quote from ZenQuotes.
 * Refreshes every 15 minutes, and installs a GitHub firmware release
 * when that release was built from a different commit.
 */

#include <Arduino.h>

#ifdef BOOT_TEST
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== CrowPanel Boot Test ===");
}

void loop() {
    Serial.printf("Loop %lu\n", (unsigned long)millis() / 2000);
    delay(2000);
}
#else

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include <GxEPD2_BW.h>
#include <gdey/GxEPD2_579_GDEY0579T93.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#include "config.h"

#ifndef FW_GIT_SHA
#define FW_GIT_SHA "unknown"
#endif
#ifndef FW_GIT_UNIX
#define FW_GIT_UNIX 0
#endif

#define EPD_BUSY 48
#define EPD_RST  47
#define EPD_DC   46
#define EPD_CS   45
#define EPD_SCK  12
#define EPD_MOSI 11
#define EPD_PWR  7

#define QUOTE_API_URL "https://zenquotes.io/api/random"
#define GITHUB_LATEST_RELEASE \
    "https://api.github.com/repos/tylerjwoodfin/crowpanel-dashboard/releases/latest"
#define REFRESH_MS    (15 * 60 * 1000)
#define MAX_FETCH_FAILURES 5

// Tracks consecutive quote-fetch failures so we can reboot after MAX_FETCH_FAILURES.
static int consecutiveFetchFailures = 0;

void rebootDevice(const char* reason) {
    Serial.printf("Rebooting: %s\n", reason);
    Serial.flush();
    delay(100);
    ESP.restart();
}

GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> display(
    GxEPD2_579_GDEY0579T93(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

const char* getTrashWeekOwner() {
    time_t now = time(nullptr);
    // Require a real (NTP-synced) clock; otherwise the week can't be trusted.
    if (now < 946684800) return "Huy";

    struct tm* t = localtime(&now);
    int wday = t->tm_wday;
    int daysSinceThursday = (wday - 4 + 7) % 7;
    time_t lastThu = now - (long)daysSinceThursday * 86400;
    unsigned long epochWeek = (unsigned long)(lastThu / 86400) / 7;
    return (epochWeek % 2 == 1) ? "Huy" : "Tyler";
}

bool fetchQuote(String& quote, String& author) {
    HTTPClient http;
    http.begin(QUOTE_API_URL);
    http.setTimeout(10000);
    int code = http.GET();

    if (code != HTTP_CODE_OK) {
        Serial.printf("HTTP error %d\n", code);
        return false;
    }

    String payload = http.getString();
    http.end();

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("JSON error: %s\n", err.c_str());
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) return false;

    quote = arr[0]["q"].as<String>();
    author = arr[0]["a"].as<String>();
    return true;
}

int16_t wrapAndDraw(const String& s, int16_t x, int16_t y, int16_t maxW) {
    int16_t curY = y;
    const int lineHeight = 36;  // Double spacing
    int pos = 0;

    while (pos < (int)s.length()) {
        String line;
        int16_t x1, y1;
        uint16_t w, h;

        while (pos < (int)s.length()) {
            int spacePos = s.indexOf(' ', pos);
            if (spacePos < 0) spacePos = s.length();
            String word = s.substring(pos, spacePos);
            String testLine = line.length() ? line + " " + word : word;
            display.getTextBounds(testLine.c_str(), 0, 0, &x1, &y1, &w, &h);
            if (w > (uint16_t)maxW && line.length() > 0) {
                break;
            }
            line = testLine;
            pos = spacePos + 1;
            if (spacePos >= (int)s.length()) break;
        }
        if (line.length() > 0) {
            display.setCursor(x, curY);
            display.print(line);
            curY += lineHeight;
        }
    }
    return curY;
}

void updateDisplay() {
    display.init(0, false);  // Wake from hibernate

    String quote, author;
    if (WiFi.status() == WL_CONNECTED) {
        if (fetchQuote(quote, author)) {
            Serial.println("Quote: " + quote);
            consecutiveFetchFailures = 0;
        } else {
            consecutiveFetchFailures++;
            Serial.printf("Quote fetch failed (%d in a row)\n", consecutiveFetchFailures);
            quote = "Error fetching quote.";
            author = "";
        }
    } else {
        quote = "No WiFi";
        author = "";
    }

    const char* trashOwner = getTrashWeekOwner();
    char header[32];
    snprintf(header, sizeof(header), "%s's Trash Week", trashOwner);

    char updated[80];
    time_t now = time(nullptr);
    // 946684800 = Jan 1, 2000 UTC - reject epoch (1969-12-31) or unsynced time
    struct tm* t = (now > 946684800) ? localtime(&now) : nullptr;
    if (t) {
        int hour12 = t->tm_hour % 12;
        if (hour12 == 0) hour12 = 12;
        snprintf(updated, sizeof(updated), "Updated %04d-%02d-%02d %d:%02d %s  %.7s",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, hour12, t->tm_min,
                 t->tm_hour >= 12 ? "PM" : "AM", FW_GIT_SHA);
    } else {
        snprintf(updated, sizeof(updated), "Updated (no NTP)  %.7s", FW_GIT_SHA);
    }

    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold24pt7b);
    display.setCursor(20, 55);
    display.print(header);

    display.setFont(&FreeSans12pt7b);
    String quotedText = "\"" + quote + "\"";
    int16_t quoteEndY = wrapAndDraw(quotedText, 20, 95, 634);

    if (author.length() > 0) {
        display.setCursor(60, quoteEndY + 2);
        display.print("- ");
        display.print(author);
    }

    display.setCursor(20, 250);
    display.print(updated);

    display.display(false);
    display.hibernate();
    Serial.println("Done.");
}

bool isGitSha(const String& s) {
    if (s.length() != 40) return false;
    for (unsigned i = 0; i < s.length(); i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

// Days from civil UTC (Howard Hinnant). Month is 1-12.
long daysFromCivil(int year, int month, int day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (long)era * 146097 + (long)doe - 719468;
}

// "2026-10-16T04:48:00Z" -> unix time. Returns false if the string is not that form.
bool parseGithubTime(const String& s, time_t* out) {
    int year, month, day, hour, min, sec;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%dZ",
               &year, &month, &day, &hour, &min, &sec) != 6) {
        return false;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60) {
        return false;
    }
    *out = (time_t)daysFromCivil(year, month, day) * 86400 +
           hour * 3600 + min * 60 + sec;
    return true;
}

// Latest release tag is the commit SHA that GitHub Actions built.
bool fetchLatestRelease(String& sha, time_t* published) {
    HTTPClient http;
    if (!http.begin(GITHUB_LATEST_RELEASE)) return false;
    http.setTimeout(15000);
    http.setUserAgent("crowpanel-dashboard");
    http.addHeader("Accept", "application/vnd.github+json");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("Release check HTTP %d\n", code);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument filter;
    filter["tag_name"] = true;
    filter["published_at"] = true;
    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, payload, DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("Release JSON: %s\n", err.c_str());
        return false;
    }

    sha = doc["tag_name"].as<String>();
    sha.trim();
    sha.toLowerCase();
    if (!isGitSha(sha)) {
        Serial.printf("Latest release tag is not a commit: %s\n", sha.c_str());
        return false;
    }
    String publishedAt = doc["published_at"].as<String>();
    if (!parseGithubTime(publishedAt, published)) {
        Serial.printf("Bad release time: %s\n", publishedAt.c_str());
        return false;
    }
    return true;
}

void showStatus(const char* line) {
    display.init(0, false);
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold24pt7b);
    display.setCursor(20, 150);
    display.print(line);
    display.display(false);
    display.hibernate();
}

void installFirmware(const String& sha) {
    String url = "https://github.com/tylerjwoodfin/crowpanel-dashboard/releases/download/";
    url += sha;
    url += "/firmware.bin";
    Serial.printf("Installing %s\n", url.c_str());
    showStatus("Installing update");

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(60);

    HTTPUpdate updater(120000);
    updater.rebootOnUpdate(true);
    updater.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    HTTPUpdateResult result = updater.update(client, url, FW_GIT_SHA, [](HTTPClient* http) {
        http->addHeader("Accept", "application/octet-stream");
    });
    Serial.printf("Update failed (%d): %s\n", (int)result, updater.getLastErrorString().c_str());
}

void maybeInstallFirmware() {
    String remote;
    time_t published = 0;
    if (!fetchLatestRelease(remote, &published)) return;

    String local = FW_GIT_SHA;
    local.toLowerCase();
    if (local == remote) {
        Serial.println("Firmware matches GitHub");
        return;
    }
    // Skip a release built from an older commit so a USB flash of newer local
    // code is not replaced by the previous GitHub build.
    if (FW_GIT_UNIX != 0 && published <= (time_t)FW_GIT_UNIX) {
        Serial.printf("GitHub %s is older than this build; skipping\n", remote.c_str());
        return;
    }

    Serial.printf("Firmware %s differs from GitHub %s\n", local.c_str(), remote.c_str());
    installFirmware(remote);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.printf("CrowPanel Hello %s\n", FW_GIT_SHA);

    pinMode(EPD_PWR, OUTPUT);
    digitalWrite(EPD_PWR, HIGH);
    delay(10);

    SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
    display.init(115200, true);
    display.setRotation(2);  // 180° - upside down

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    // USB builds copy config.h into NVS. GitHub builds leave these empty so the
    // public firmware.bin does not contain the WiFi password.
    Preferences prefs;
    prefs.begin("wifi", false);
    if (WIFI_SSID[0] != '\0') {
        prefs.putString("ssid", WIFI_SSID);
        prefs.putString("pass", WIFI_PASS);
    }
    String wifiSsid = prefs.getString("ssid", "");
    String wifiPass = prefs.getString("pass", "");
    prefs.end();

    if (wifiSsid.length() == 0) {
        Serial.println("No WiFi credentials stored");
    } else {
        WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
        Serial.print("WiFi connecting");
        int tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries < 30) {
            delay(500);
            Serial.print(".");
            tries++;
        }
        Serial.println();
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi connected: IP %s, RSSI %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        Serial.printf("WiFi FAILED, status=%d\n", WiFi.status());
    }

    if (WiFi.status() == WL_CONNECTED) {
        // Pacific: setenv+tzset for localtime(), configTzTime for NTP sync
        setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
        tzset();
        configTzTime("PST8PDT,M3.2.0,M11.1.0",
                     "pool.ntp.org", "time.google.com", "time.cloudflare.com");
        Serial.print("NTP syncing");
        for (int i = 0; i < 120; i++) {  // Wait up to 60 sec for valid NTP (reject 1969-12-31)
            if (time(nullptr) > 946684800) break;  // Jan 1, 2000
            delay(500);
            Serial.print(".");
        }
        Serial.println();
        time_t now = time(nullptr);
        Serial.printf("NTP time=%lld synced=%s\n",
                      (long long)now, (now > 946684800) ? "YES" : "NO");
    }

    updateDisplay();
}

void loop() {
    delay(REFRESH_MS);

    // Reboot if WiFi has dropped by the time we want to refresh.
    if (WiFi.status() != WL_CONNECTED) {
        rebootDevice("WiFi disconnected at refresh interval");
    }

    maybeInstallFirmware();
    updateDisplay();

    // Reboot after too many consecutive quote-fetch failures.
    if (consecutiveFetchFailures >= MAX_FETCH_FAILURES) {
        rebootDevice("Quote fetch failed too many times in a row");
    }
}

#endif
