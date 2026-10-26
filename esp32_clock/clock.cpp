#include <Arduino.h>
// Wi-Fi/NTP clock with weather for the Waveshare ESP32-C6-LCD-1.47 (172x320 ST7789), shown in landscape.
//
// Layout (320x172):
//   top left:   time, AM/PM, date
//   top right:  current conditions icon, temperature, label, today's high/low
//   bottom:     8-hour forecast strip (hour, icon, temperature, rain chance)
//
// Everything is drawn into an off-screen canvas and pushed to the panel in one go, so there's no flicker.

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Arduino_GFX_Library.h>
#include "fonts/FreeSans9pt7b.h"
#include "fonts/FreeSansBold9pt7b.h"
#include "fonts/FreeSansBold18pt7b.h"
#include "fonts/FreeSansBold24pt7b.h"
#include "secrets.h"  // defines WIFI_SSID and WIFI_PASSWORD
#include "display.h"
#include "ros_page.h"
#include "govee_page.h"

// POSIX TZ string; US Eastern with daylight saving.
#define TIMEZONE "EST5EDT,M3.2.0,M11.1.0"
#define USE_24H false

// Weather location (Arlington, MA) and refresh interval. Data from open-meteo.com, no key needed.
#define WEATHER_LAT "42.4188"
#define WEATHER_LON "-71.1557"
#define WEATHER_REFRESH_MS (15 * 60 * 1000UL)
#define WEATHER_RETRY_MS (60 * 1000UL)
#define FORECAST_HOURS 8

// Pages advance on their own this often; a BOOT press advances immediately and restarts the timer.
#define PAGE_ROTATE_MS (10 * 1000UL)
#define SHOW_DASHBOARD false  // the combined page; kept but out of the rotation for now

#define LCD_MOSI 6
#define LCD_SCK 7
#define LCD_CS 14
#define LCD_DC 15
#define LCD_RST 21
#define LCD_BL 22

#define RGB_LED_PIN 8
#define BOOT_BUTTON 9  // low when pressed; flips between pages

Arduino_DataBus *bus = new Arduino_HWSPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, GFX_NOT_DEFINED);
// The 172-pixel-wide panel sits 34 columns into the ST7789's 240-column RAM.
Arduino_GFX *panel = new Arduino_ST7789(bus, LCD_RST, 1 /* rotation: landscape */, true /* IPS */,
                                        172, 320, 34, 0, 34, 0);
Arduino_Canvas *gfx = new Arduino_Canvas(320, 172, panel);

struct Weather {
  bool valid = false;
  int temp, code, high, low;
  int hour[FORECAST_HOURS], hourTemp[FORECAST_HOURS], hourCode[FORECAST_HOURS], hourRain[FORECAST_HOURS];
};
Weather weather;

int drawText(const char *text, int x, int y, const GFXfont *font, uint16_t color, Align align) {
  int16_t x1, y1;
  uint16_t w, h;
  gfx->setFont(font);
  gfx->setTextColor(color);
  gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  if (align == CENTER) x -= x1 + w / 2;
  if (align == RIGHT) x -= x1 + w;
  gfx->setCursor(x, y);
  gfx->print(text);
  return x + x1 + w;
}

void drawSmallText(const char *text, int x, int y, uint16_t color) {
  gfx->setFont(NULL);
  gfx->setTextSize(1);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(text);
}

void drawDegree(int x, int top, int r, uint16_t color) {
  gfx->drawCircle(x + r, top + r, r, color);
  if (r > 2) gfx->drawCircle(x + r, top + r, r - 1, color);
}

uint16_t tempColor(int f) {
  if (f < 32) return 0x9EDF;
  if (f < 50) return 0x5F1F;
  if (f < 65) return 0x87F0;
  if (f < 80) return 0xFEA0;
  if (f < 90) return 0xFC60;
  return 0xF8A3;
}

// Short label for a WMO weather code, as returned by Open-Meteo.
const char *weatherLabel(int code) {
  if (code == 0) return "Clear";
  if (code <= 2) return "Partly cloudy";
  if (code == 3) return "Cloudy";
  if (code <= 48) return "Fog";
  if (code <= 57) return "Drizzle";
  if (code <= 67) return "Rain";
  if (code <= 77) return "Snow";
  if (code <= 82) return "Showers";
  if (code <= 86) return "Snow showers";
  return "Thunderstorm";
}

void drawSun(int cx, int cy, int r) {
  gfx->fillCircle(cx, cy, r, COLOR_SUN);
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    float c = cosf(a), s = sinf(a);
    gfx->drawLine(cx + c * (r + 2), cy + s * (r + 2), cx + c * (r + 2 + r * 2 / 3), cy + s * (r + 2 + r * 2 / 3),
                  COLOR_SUN);
  }
}

// A cloud w pixels wide whose flat bottom is at y = bottom.
void drawCloud(int x, int bottom, int w, uint16_t color) {
  int r = w / 5;
  gfx->fillRoundRect(x, bottom - 2 * r, w, 2 * r, r, color);
  gfx->fillCircle(x + w * 2 / 5, bottom - 2 * r, r * 3 / 2, color);
  gfx->fillCircle(x + w * 7 / 10, bottom - 2 * r, r, color);
}

// Weather icon for a WMO code, fitted into a w x w box at (x, y).
void drawIcon(int x, int y, int w, int code) {
  if (code == 0) {
    drawSun(x + w / 2, y + w / 2, w / 5);
    return;
  }
  if (code <= 2) {
    drawSun(x + w * 3 / 8, y + w * 3 / 8, w / 6);
    drawCloud(x + w / 4, y + w * 7 / 8, w * 3 / 4, COLOR_CLOUD);
    return;
  }
  if (code == 3) {
    drawCloud(x, y + w * 3 / 4, w, COLOR_CLOUD);
    return;
  }
  if (code <= 48) {
    for (int i = 0; i < 4; i++) {
      int ly = y + w / 4 + i * w / 6;
      gfx->drawFastHLine(x + (i % 2) * w / 8, ly, w * 7 / 8, COLOR_CLOUD);
      gfx->drawFastHLine(x + (i % 2) * w / 8, ly + 1, w * 7 / 8, COLOR_CLOUD);
    }
    return;
  }

  // Precipitation: a raised cloud with something falling out of it.
  int cloudBottom = y + w * 5 / 8;
  drawCloud(x, cloudBottom, w, code >= 95 ? COLOR_DIM : COLOR_CLOUD);
  int dropTop = cloudBottom + w / 10, dropLen = w / 4;
  if (code >= 95) {
    int bx = x + w / 2;
    gfx->fillTriangle(bx, dropTop, bx - w / 8, dropTop + dropLen, bx + w / 16, dropTop + dropLen, COLOR_SUN);
    gfx->fillTriangle(bx + w / 16, dropTop + dropLen / 2, bx - w / 16, dropTop + dropLen * 3 / 2,
                      bx - w / 16, dropTop + dropLen / 2, COLOR_SUN);
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    for (int i = 0; i < 3; i++) {
      gfx->fillCircle(x + w / 4 + i * w / 4, dropTop + (i % 2) * dropLen / 2 + 2, max(1, w / 16), RGB565_WHITE);
    }
  } else {
    for (int i = 0; i < 3; i++) {
      int dx = x + w / 4 + i * w / 4;
      gfx->drawLine(dx, dropTop, dx - w / 10, dropTop + dropLen, COLOR_RAIN);
      gfx->drawLine(dx + 1, dropTop, dx + 1 - w / 10, dropTop + dropLen, COLOR_RAIN);
    }
  }
}

// Fetches current conditions, today's high/low and the next few hours. Keeps the old data on failure.
bool updateWeather() {
  HTTPClient http;
  http.begin("http://api.open-meteo.com/v1/forecast?latitude=" WEATHER_LAT "&longitude=" WEATHER_LON
             "&current=temperature_2m,weather_code"
             "&hourly=temperature_2m,precipitation_probability,weather_code"
             "&daily=temperature_2m_max,temperature_2m_min"
             "&temperature_unit=fahrenheit&timezone=auto&forecast_days=2&forecast_hours=" +
             String(FORECAST_HOURS));
  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("weather request failed: %d\n", status);
    http.end();
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) {
    Serial.printf("weather parse failed: %s\n", err.c_str());
    return false;
  }

  weather.temp = lroundf(doc["current"]["temperature_2m"].as<float>());
  weather.code = doc["current"]["weather_code"];
  weather.high = lroundf(doc["daily"]["temperature_2m_max"][0].as<float>());
  weather.low = lroundf(doc["daily"]["temperature_2m_min"][0].as<float>());
  JsonObject hourly = doc["hourly"];
  for (int i = 0; i < FORECAST_HOURS; i++) {
    const char *t = hourly["time"][i] | "0000-00-00T00:00";  // ISO time; the hour starts at offset 11
    weather.hour[i] = atoi(t + 11);
    weather.hourTemp[i] = lroundf(hourly["temperature_2m"][i].as<float>());
    weather.hourCode[i] = hourly["weather_code"][i];
    weather.hourRain[i] = hourly["precipitation_probability"][i];
  }
  weather.valid = true;
  Serial.printf("weather: %dF %s, high %d low %d\n", weather.temp, weatherLabel(weather.code), weather.high,
                weather.low);
  return true;
}

void drawClock(const struct tm &now) {
  int hour = now.tm_hour;
  if (!USE_24H) hour = hour % 12 == 0 ? 12 : hour % 12;
  char buf[24];
  snprintf(buf, sizeof buf, "%d:%02d", hour, now.tm_min);
  int right = drawText(buf, 12, 50, &FreeSansBold24pt7b, RGB565_WHITE);
  if (!USE_24H) drawText(now.tm_hour < 12 ? "AM" : "PM", right + 6, 26, &FreeSansBold9pt7b, COLOR_DIM);

  strftime(buf, sizeof buf, "%A, %b %e", &now);
  drawText(buf, 14, 80, &FreeSans9pt7b, COLOR_DATE);
}

void drawCurrentWeather() {
  if (!weather.valid) {
    drawText("Loading", 252, 50, &FreeSans9pt7b, COLOR_DIM, CENTER);
    drawText("weather...", 252, 70, &FreeSans9pt7b, COLOR_DIM, CENTER);
    return;
  }
  drawIcon(192, 10, 42, weather.code);
  char buf[24];
  snprintf(buf, sizeof buf, "%d", weather.temp);
  int right = drawText(buf, 242, 44, &FreeSansBold18pt7b, tempColor(weather.temp));
  drawDegree(right + 3, 19, 4, tempColor(weather.temp));

  drawText(weatherLabel(weather.code), 192, 70, &FreeSans9pt7b, RGB565_WHITE);
  snprintf(buf, sizeof buf, "H %d   L %d", weather.high, weather.low);
  drawText(buf, 192, 90, &FreeSans9pt7b, COLOR_DIM);
}

void drawForecastStrip() {
  gfx->drawFastHLine(10, 100, gfx->width() - 20, COLOR_FAINT);
  if (!weather.valid) return;
  int colWidth = gfx->width() / FORECAST_HOURS;
  for (int i = 0; i < FORECAST_HOURS; i++) {
    int cx = i * colWidth + colWidth / 2;
    char buf[8];
    if (i == 0) {
      strcpy(buf, "Now");
    } else if (USE_24H) {
      snprintf(buf, sizeof buf, "%d", weather.hour[i]);
    } else {
      int h = weather.hour[i] % 12 == 0 ? 12 : weather.hour[i] % 12;
      snprintf(buf, sizeof buf, "%d%c", h, weather.hour[i] < 12 ? 'a' : 'p');
    }
    drawText(buf, cx, 117, &FreeSans9pt7b, i == 0 ? RGB565_WHITE : COLOR_DIM, CENTER);

    drawIcon(cx - 11, 121, 22, weather.hourCode[i]);

    snprintf(buf, sizeof buf, "%d", weather.hourTemp[i]);
    int right = drawText(buf, cx - 2, 160, &FreeSansBold9pt7b, tempColor(weather.hourTemp[i]), CENTER);
    drawDegree(right + 1, 147, 2, tempColor(weather.hourTemp[i]));

    if (weather.hourRain[i] >= 10) {
      snprintf(buf, sizeof buf, "%d%%", weather.hourRain[i]);
      drawSmallText(buf, cx - strlen(buf) * 3, 164, COLOR_RAIN);
    }
  }
}

void drawClockPage(const struct tm &now) {
  gfx->fillScreen(RGB565_BLACK);
  drawClock(now);
  drawCurrentWeather();
  drawForecastStrip();
  gfx->flush();
}

// Everything at once: clock, current weather, Govee thermometers and a compact forecast strip.
//   y   0-52   time + date (left), current conditions (right)
//   y  54-80   one line per thermometer
//   y  82-172  8-hour forecast
void drawDashboardPage(const struct tm &now) {
  gfx->fillScreen(RGB565_BLACK);
  char buf[24];

  int hour = now.tm_hour;
  if (!USE_24H) hour = hour % 12 == 0 ? 12 : hour % 12;
  snprintf(buf, sizeof buf, "%d:%02d", hour, now.tm_min);
  int right = drawText(buf, 8, 30, &FreeSansBold18pt7b, RGB565_WHITE);
  if (!USE_24H) drawText(now.tm_hour < 12 ? "AM" : "PM", right + 4, 16, &FreeSansBold9pt7b, COLOR_DIM);
  strftime(buf, sizeof buf, "%a, %b %e", &now);
  drawText(buf, 8, 48, &FreeSans9pt7b, COLOR_DATE);

  if (weather.valid) {
    drawIcon(160, 4, 32, weather.code);
    snprintf(buf, sizeof buf, "%d", weather.temp);
    right = drawText(buf, 200, 30, &FreeSansBold18pt7b, tempColor(weather.temp));
    drawDegree(right + 2, 6, 3, tempColor(weather.temp));
    snprintf(buf, sizeof buf, "H %d", weather.high);
    drawSmallText(buf, 278, 8, COLOR_DIM);
    snprintf(buf, sizeof buf, "L %d", weather.low);
    drawSmallText(buf, 278, 22, COLOR_DIM);
    drawText(weatherLabel(weather.code), 162, 48, &FreeSans9pt7b, RGB565_WHITE);
  } else {
    drawText("Loading weather...", 240, 30, &FreeSans9pt7b, COLOR_DIM, CENTER);
  }

  gfx->drawFastHLine(6, 54, gfx->width() - 12, COLOR_FAINT);
  const Thermometer *therms;
  int n = goveeThermometers(&therms);
  if (n == 0) {
    drawText("No thermometers yet", 160, 72, &FreeSans9pt7b, COLOR_DIM, CENTER);
  }
  // One line per thermometer, side by side: "Garden: 42.4°/85%".
  int tileWidth = n ? gfx->width() / n : gfx->width();
  for (int i = 0; i < n; i++) {
    const Thermometer &t = therms[i];
    int left = i * tileWidth;
    if (i > 0) gfx->drawFastVLine(left, 58, 18, COLOR_FAINT);
    snprintf(buf, sizeof buf, "%s:", t.name);
    int x = drawText(buf, left + 8, 72, &FreeSans9pt7b, COLOR_DATE) + 5;
    if (!t.valid) {
      drawText("--", x, 72, &FreeSansBold9pt7b, COLOR_DIM);
      continue;
    }
    uint16_t color = t.online ? tempColor(lroundf(t.temp)) : COLOR_DIM;
    snprintf(buf, sizeof buf, "%.1f", t.temp);
    x = drawText(buf, x, 72, &FreeSansBold9pt7b, color);
    drawDegree(x + 1, 59, 2, color);
    snprintf(buf, sizeof buf, "/%.0f%%", t.humidity);
    x = drawText(buf, x + 7, 72, &FreeSansBold9pt7b, t.online ? COLOR_RAIN : COLOR_DIM);
    if (!t.online) drawSmallText("off", x + 4, 64, COLOR_BAD);
  }

  gfx->drawFastHLine(6, 82, gfx->width() - 12, COLOR_FAINT);
  if (weather.valid) {
    int colWidth = gfx->width() / FORECAST_HOURS;
    for (int i = 0; i < FORECAST_HOURS; i++) {
      int cx = i * colWidth + colWidth / 2;
      if (i == 0) {
        strcpy(buf, "Now");
      } else if (USE_24H) {
        snprintf(buf, sizeof buf, "%d", weather.hour[i]);
      } else {
        int h = weather.hour[i] % 12 == 0 ? 12 : weather.hour[i] % 12;
        snprintf(buf, sizeof buf, "%d%s", h, weather.hour[i] < 12 ? "am" : "pm");
      }
      drawSmallText(buf, cx - strlen(buf) * 3, 88, i == 0 ? RGB565_WHITE : COLOR_DIM);
      drawIcon(cx - 14, 100, 28, weather.hourCode[i]);
      snprintf(buf, sizeof buf, "%d", weather.hourTemp[i]);
      right = drawText(buf, cx - 2, 150, &FreeSansBold9pt7b, tempColor(weather.hourTemp[i]), CENTER);
      drawDegree(right + 1, 137, 2, tempColor(weather.hourTemp[i]));
      if (weather.hourRain[i] >= 10) {
        snprintf(buf, sizeof buf, "%d%%", weather.hourRain[i]);
        drawSmallText(buf, cx - strlen(buf) * 3, 158, COLOR_RAIN);
      }
    }
  }
  gfx->flush();
}

void showStatus(const char *line1, const char *line2) {
  gfx->fillScreen(RGB565_BLACK);
  drawText(line1, 160, 76, &FreeSansBold9pt7b, RGB565_WHITE, CENTER);
  drawText(line2, 160, 104, &FreeSans9pt7b, COLOR_DIM, CENTER);
  gfx->flush();
}

void connectWiFi() {
  showStatus("Connecting to", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }
  Serial.printf("Wi-Fi connected, IP %s\n", WiFi.localIP().toString().c_str());
}

void syncTime() {
  showStatus("Syncing time", "pool.ntp.org");
  configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
  struct tm now;
  while (!getLocalTime(&now, 1000)) {
  }
  Serial.println("time synced");
}

void setup() {
  Serial.begin(115200);

  // The RGB LED keeps whatever colour the previous firmware left it with, so turn it off.
  rgbLedWrite(RGB_LED_PIN, 0, 0, 0);

  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed");
  }
  gfx->setTextWrap(false);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  pinMode(BOOT_BUTTON, INPUT_PULLUP);

  connectWiFi();
  syncTime();
  rosBegin();
  goveeBegin();
}

enum Page { CLOCK_PAGE, ROS_PAGE, GOVEE_PAGE, DASHBOARD_PAGE, PAGE_COUNT };

bool pageEnabled(Page page) { return page != DASHBOARD_PAGE || SHOW_DASHBOARD; }

Page nextPage(Page page) {
  do {
    page = (Page)((page + 1) % PAGE_COUNT);
  } while (!pageEnabled(page));
  return page;
}

// True once per press of the BOOT button.
bool buttonPressed() {
  static bool wasDown = false;
  static unsigned long lastChange = 0;
  bool down = digitalRead(BOOT_BUTTON) == LOW;
  if (down == wasDown || millis() - lastChange < 50) return false;
  wasDown = down;
  lastChange = millis();
  return down;
}

void loop() {
  static Page page = (Page)(PAGE_COUNT - 1);  // the first pass of the loop advances this to the clock
  static int lastMinute = -1;
  static unsigned long nextWeather = 0, lastPageDraw = 0, pageShownAt = 0;
  bool redraw = false;

  rosLoop();
  goveeLoop();

  if (buttonPressed() || millis() - pageShownAt >= PAGE_ROTATE_MS) {
    page = nextPage(page);
    pageShownAt = millis();
    redraw = true;
  }

  // Retry a failed fetch after a minute rather than waiting for the full interval.
  if (millis() >= nextWeather) {
    bool ok = updateWeather();
    nextWeather = millis() + (ok ? WEATHER_REFRESH_MS : WEATHER_RETRY_MS);
    redraw |= ok && (page == CLOCK_PAGE || page == DASHBOARD_PAGE);
  }

  if (page == ROS_PAGE || page == GOVEE_PAGE) {
    if (redraw || millis() - lastPageDraw >= 1000) {
      lastPageDraw = millis();
      if (page == ROS_PAGE) rosDraw();
      else goveeDraw();
      gfx->flush();
    }
  } else {
    struct tm now;
    if (getLocalTime(&now, 0) && (now.tm_min != lastMinute || redraw)) {
      lastMinute = now.tm_min;
      if (page == CLOCK_PAGE) drawClockPage(now);
      else drawDashboardPage(now);
    }
  }
  delay(5);
}
