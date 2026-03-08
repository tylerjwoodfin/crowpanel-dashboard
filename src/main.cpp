/**
 * CrowPanel 5.79" - trash duty + quote display
 *
 * Displays trash duty and a random quote from ZenQuotes.
 * Refreshes every 15 minutes.
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
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <GxEPD2_BW.h>
#include <gdey/GxEPD2_579_GDEY0579T93.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#include "config.h"

#define EPD_BUSY 48
#define EPD_RST  47
#define EPD_DC   46
#define EPD_CS   45
#define EPD_SCK  12
#define EPD_MOSI 11
#define EPD_PWR  7

#define QUOTE_API_URL "https://zenquotes.io/api/random"
#define REFRESH_MS    (15 * 60 * 1000)

GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> display(
    GxEPD2_579_GDEY0579T93(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

const char* getTrashWeekOwner() {
    time_t now = time(nullptr);
    if (now < 0) return "Huy";

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
        } else {
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

    char updated[64];
    time_t now = time(nullptr);
    // 946684800 = Jan 1, 2000 UTC - reject epoch (1969-12-31) or unsynced time
    struct tm* t = (now > 946684800) ? localtime(&now) : nullptr;
    if (t) {
        snprintf(updated, sizeof(updated), "Updated %04d-%02d-%02d %02d:%02d",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min);
    } else {
        snprintf(updated, sizeof(updated), "Updated (no NTP)");
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

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("CrowPanel Hello");

    pinMode(EPD_PWR, OUTPUT);
    digitalWrite(EPD_PWR, HIGH);
    delay(10);

    SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
    display.init(115200, true);
    display.setRotation(2);  // 180° - upside down

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("WiFi connecting");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
        delay(500);
        Serial.print(".");
        tries++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        // Pacific: setenv+tzset for localtime(), configTzTime for NTP sync
        setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
        tzset();
        configTzTime("PST8PDT,M3.2.0,M11.1.0", "pool.ntp.org");
        for (int i = 0; i < 40; i++) {  // Wait up to 20 sec for valid NTP (reject 1969-12-31)
            if (time(nullptr) > 946684800) break;  // Jan 1, 2000
            delay(500);
        }
    }

    updateDisplay();
}

void loop() {
    delay(REFRESH_MS);
    updateDisplay();
}

#endif
