#include <Arduino.h>
// Govee thermometers via the Govee developer API (openapi.api.govee.com). The device list is fetched at
// startup and hourly; readings every few minutes. Each thermometer gets a column on the page.

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "display.h"
#include "govee_page.h"
#include "govee_ca.h"
#include "secrets.h"  // defines GOVEE_API_KEY
#include "fonts/FreeSans9pt7b.h"
#include "fonts/FreeSansBold9pt7b.h"
#include "fonts/FreeSansBold24pt7b.h"

#define GOVEE_API "https://openapi.api.govee.com/router/api/v1"
#define DEVICE_REFRESH_MS (60 * 60 * 1000UL)
#define READING_REFRESH_MS (5 * 60 * 1000UL)
#define RETRY_MS (60 * 1000UL)
#define MAX_THERMOMETERS 4

// Display names by Govee device ID, overriding the names in the Govee app.
struct Alias {
  const char *device, *name;
};
const Alias ALIASES[] = {
    {"16:A0:18:1F:6C:71:DD:2C", "MA"},  // "Garden" in the Govee app
    {"44:19:E0:E1:02:C6:6B:4D", "NH"},  // "Cottage Thermometer" in the Govee app
};

static Thermometer thermometers[MAX_THERMOMETERS];
static int thermometerCount = 0;
static unsigned long nextDevices = 0, nextReadings = 0, lastUpdate = 0;
static bool lastFetchFailed = false;

// Sends one API request and parses the JSON reply. body == nullptr means GET.
static bool goveeRequest(const char *path, const char *body, JsonDocument &reply) {
  WiFiClientSecure client;
  client.setCACert(AMAZON_ROOT_CA1);
  HTTPClient http;
  http.begin(client, String(GOVEE_API) + path);
  http.addHeader("Govee-API-Key", GOVEE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  int status = body ? http.POST(body) : http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("govee %s failed: %d\n", path, status);
    http.end();
    return false;
  }
  DeserializationError err = deserializeJson(reply, http.getString());
  http.end();
  if (err || reply["code"] != 200) {
    Serial.printf("govee %s bad reply: %s\n", path, err ? err.c_str() : (const char *)(reply["message"] | "?"));
    return false;
  }
  return true;
}

static bool fetchDevices() {
  JsonDocument reply;
  if (!goveeRequest("/user/devices", nullptr, reply)) return false;
  thermometerCount = 0;
  for (JsonObject d : reply["data"].as<JsonArray>()) {
    if (d["type"] != "devices.types.thermometer" || thermometerCount == MAX_THERMOMETERS) continue;
    Thermometer &t = thermometers[thermometerCount++];
    memset(&t, 0, sizeof t);
    strlcpy(t.sku, d["sku"] | "", sizeof t.sku);
    strlcpy(t.device, d["device"] | "", sizeof t.device);
    strlcpy(t.name, d["deviceName"] | "", sizeof t.name);
    // Names like "Cottage Thermometer" are too wide for a column, and every device here is a thermometer.
    char *suffix = strstr(t.name, " Thermometer");
    if (suffix) *suffix = '\0';
    for (const Alias &a : ALIASES)
      if (strcmp(t.device, a.device) == 0) strlcpy(t.name, a.name, sizeof t.name);
  }
  Serial.printf("govee: %d thermometers\n", thermometerCount);
  return true;
}

static bool fetchReading(Thermometer &t) {
  JsonDocument request, reply;
  request["requestId"] = "clock";
  request["payload"]["sku"] = t.sku;
  request["payload"]["device"] = t.device;
  String body;
  serializeJson(request, body);
  if (!goveeRequest("/device/state", body.c_str(), reply)) return false;
  for (JsonObject c : reply["payload"]["capabilities"].as<JsonArray>()) {
    const char *instance = c["instance"] | "";
    if (strcmp(instance, "online") == 0) t.online = c["state"]["value"];
    if (strcmp(instance, "sensorTemperature") == 0) t.temp = c["state"]["value"];
    if (strcmp(instance, "sensorHumidity") == 0) t.humidity = c["state"]["value"];
  }
  t.valid = true;
  Serial.printf("govee: %s %.1fF %.0f%%%s\n", t.name, t.temp, t.humidity, t.online ? "" : " (offline)");
  return true;
}

int goveeThermometers(const Thermometer **list) {
  *list = thermometers;
  return thermometerCount;
}

void goveeBegin() {}

void goveeLoop() {
  if (millis() >= nextDevices) {
    bool ok = fetchDevices();
    nextDevices = millis() + (ok ? DEVICE_REFRESH_MS : RETRY_MS);
    if (ok) nextReadings = 0;
  }
  if (thermometerCount > 0 && millis() >= nextReadings) {
    bool ok = true;
    for (int i = 0; i < thermometerCount; i++) ok &= fetchReading(thermometers[i]);
    if (ok) lastUpdate = millis();
    lastFetchFailed = !ok;
    nextReadings = millis() + (ok ? READING_REFRESH_MS : RETRY_MS);
  }
}

static void drawThermometer(const Thermometer &t, int left, int width) {
  int cx = left + width / 2;
  drawText(t.name, cx, 30, &FreeSans9pt7b, COLOR_DATE, CENTER);
  if (!t.valid) {
    drawText("--", cx, 92, &FreeSansBold24pt7b, COLOR_DIM, CENTER);
    return;
  }
  uint16_t color = t.online ? tempColor(lroundf(t.temp)) : COLOR_DIM;
  char buf[16];
  snprintf(buf, sizeof buf, "%.1f", t.temp);
  int right = drawText(buf, cx - 5, 92, &FreeSansBold24pt7b, color, CENTER);
  drawDegree(right + 3, 58, 5, color);

  snprintf(buf, sizeof buf, "%.0f%% humidity", t.humidity);
  drawText(buf, cx, 124, &FreeSans9pt7b, t.online ? COLOR_RAIN : COLOR_DIM, CENTER);
  if (!t.online) drawText("offline", cx, 146, &FreeSansBold9pt7b, COLOR_BAD, CENTER);
}

void goveeDraw() {
  gfx->fillScreen(RGB565_BLACK);
  if (thermometerCount == 0) {
    drawText("Govee", 160, 76, &FreeSansBold9pt7b, RGB565_WHITE, CENTER);
    drawText(lastFetchFailed || nextDevices ? "no thermometers found" : "loading...", 160, 104,
             &FreeSans9pt7b, COLOR_DIM, CENTER);
    return;
  }

  int width = gfx->width() / thermometerCount;
  for (int i = 0; i < thermometerCount; i++) {
    if (i > 0) gfx->drawFastVLine(i * width, 16, 120, COLOR_FAINT);
    drawThermometer(thermometers[i], i * width, width);
  }

  char buf[40];
  if (lastUpdate == 0) {
    strcpy(buf, "waiting for readings");
  } else {
    unsigned long minutes = (millis() - lastUpdate) / 60000;
    if (minutes == 0) snprintf(buf, sizeof buf, "updated just now");
    else snprintf(buf, sizeof buf, "updated %lu min ago", minutes);
  }
  if (lastFetchFailed) strlcat(buf, " - fetch failed", sizeof buf);
  drawSmallText(buf, (gfx->width() - strlen(buf) * 6) / 2, 158, lastFetchFailed ? COLOR_WARN : COLOR_DIM);
}
