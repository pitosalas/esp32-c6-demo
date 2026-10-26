#include <Arduino.h>
// ROS 2 topic monitor. Connects to rosbridge over a WebSocket, lists topics via rosapi, subscribes to the
// lightweight ones and shows how often each is publishing, plus the latest /rosout line.

#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "display.h"
#include "ros_page.h"
#include "fonts/FreeSansBold9pt7b.h"

// rosbridge_server on the Ubuntu VM. A DHCP change on the VM will move this.
#define ROSBRIDGE_HOST "192.168.4.83"
#define ROSBRIDGE_PORT 9090

#define TOPIC_REFRESH_MS (30 * 1000UL)
#define RATE_WINDOW_MS 2000
#define ACTIVE_MS 3000       // a topic counts as active if it published this recently
#define THROTTLE_MS 50       // rosbridge sends at most 20 messages/s per topic
#define MAX_TOPICS 48
#define ROW_HEIGHT 11
#define ROWS_TOP 26
#define ROWS_BOTTOM 152

// Message types too big to stream to the ESP32 as JSON; they're listed but not watched.
const char *const HEAVY_TYPES[] = {
    "sensor_msgs/msg/Image",     "sensor_msgs/msg/CompressedImage",   "sensor_msgs/msg/PointCloud2",
    "nav_msgs/msg/OccupancyGrid", "map_msgs/msg/OccupancyGridUpdate", "visualization_msgs/msg/MarkerArray",
};
const char *const SKIPPED_TOPICS[] = {"/robot_description"};

struct Topic {
  char name[64];
  bool watched;     // subscribed through rosbridge
  bool present;     // still in the latest topic list
  uint32_t count;   // messages in the current rate window
  float rate;
  unsigned long lastSeen;  // millis() of the last message, 0 if never
};

static WebSocketsClient ws;
static Topic topics[MAX_TOPICS];
static int topicCount = 0;
static bool connected = false;
static unsigned long nextTopicRefresh = 0, windowStart = 0;
static char lastLog[96] = "";
static int lastLogLevel = 0;

static bool isHeavy(const char *name, const char *type) {
  for (const char *t : HEAVY_TYPES)
    if (strcmp(type, t) == 0) return true;
  for (const char *t : SKIPPED_TOPICS)
    if (strcmp(name, t) == 0) return true;
  return false;
}

static Topic *findTopic(const char *name) {
  for (int i = 0; i < topicCount; i++)
    if (strcmp(topics[i].name, name) == 0) return &topics[i];
  return nullptr;
}

static void sendJson(JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  ws.sendTXT(out);
}

static void requestTopics() {
  JsonDocument doc;
  doc["op"] = "call_service";
  doc["service"] = "/rosapi/topics";
  doc["id"] = "topics";
  sendJson(doc);
}

static void subscribe(const char *name) {
  JsonDocument doc;
  doc["op"] = "subscribe";
  doc["topic"] = name;
  doc["throttle_rate"] = THROTTLE_MS;
  doc["queue_length"] = 1;
  sendJson(doc);
}

// Merges a fresh topic list into the table, subscribing to any new lightweight topics.
static void handleTopicList(JsonObject values) {
  for (int i = 0; i < topicCount; i++) topics[i].present = false;
  JsonArray names = values["topics"], types = values["types"];
  for (size_t i = 0; i < names.size(); i++) {
    const char *name = names[i];
    const char *type = types[i] | "";
    Topic *t = findTopic(name);
    if (!t) {
      if (topicCount == MAX_TOPICS) continue;
      t = &topics[topicCount++];
      memset(t, 0, sizeof *t);
      strlcpy(t->name, name, sizeof t->name);
      t->watched = !isHeavy(name, type);
      if (t->watched) subscribe(name);
    }
    t->present = true;
  }
  Serial.printf("ros: %d topics\n", topicCount);
}

static void handleMessage(uint8_t *payload, size_t length) {
  // Only keep the fields we use, so large messages don't need a large document.
  JsonDocument filter;
  filter["op"] = true;
  filter["topic"] = true;
  filter["id"] = true;
  filter["values"] = true;
  filter["msg"]["level"] = true;
  filter["msg"]["name"] = true;
  filter["msg"]["msg"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, payload, length, DeserializationOption::Filter(filter))) return;

  const char *op = doc["op"] | "";
  if (strcmp(op, "service_response") == 0 && doc["id"] == "topics") {
    handleTopicList(doc["values"]);
    return;
  }
  if (strcmp(op, "publish") != 0) return;
  const char *name = doc["topic"] | "";
  Topic *t = findTopic(name);
  if (t) {
    t->count++;
    t->lastSeen = millis();
  }
  // rosbridge logs every client connect/disconnect, including ours, so skip its own lines.
  if (strcmp(name, "/rosout") == 0 && strcmp(doc["msg"]["name"] | "", "rosbridge_websocket") != 0) {
    snprintf(lastLog, sizeof lastLog, "%s: %s", doc["msg"]["name"] | "?", doc["msg"]["msg"] | "");
    lastLogLevel = doc["msg"]["level"] | 0;
  }
}

static void onEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("ros: connected to rosbridge");
      connected = true;
      topicCount = 0;  // rosbridge forgets our subscriptions on disconnect, so start afresh
      nextTopicRefresh = 0;
      break;
    case WStype_DISCONNECTED:
      if (connected) Serial.println("ros: disconnected");
      connected = false;
      break;
    case WStype_TEXT:
      handleMessage(payload, length);
      break;
    default:
      break;
  }
}

void rosBegin() {
  ws.begin(ROSBRIDGE_HOST, ROSBRIDGE_PORT, "/");
  ws.onEvent(onEvent);
  ws.setReconnectInterval(5000);
}

void rosLoop() {
  ws.loop();
  if (connected && millis() >= nextTopicRefresh) {
    nextTopicRefresh = millis() + TOPIC_REFRESH_MS;
    requestTopics();
  }
  unsigned long elapsed = millis() - windowStart;
  if (elapsed >= RATE_WINDOW_MS) {
    for (int i = 0; i < topicCount; i++) {
      topics[i].rate = topics[i].count * 1000.0f / elapsed;
      topics[i].count = 0;
    }
    windowStart = millis();
  }
}

static bool isActive(const Topic &t) { return t.lastSeen && millis() - t.lastSeen < ACTIVE_MS; }

// Display order: active topics by rate, then quiet watched ones, then unwatched, each alphabetical.
static int rank(const Topic &t) { return isActive(t) ? 0 : t.watched ? 1 : 2; }

static int compareTopics(const void *a, const void *b) {
  const Topic &x = **(const Topic *const *)a, &y = **(const Topic *const *)b;
  if (rank(x) != rank(y)) return rank(x) - rank(y);
  if (rank(x) == 0 && x.rate != y.rate) return x.rate > y.rate ? -1 : 1;
  return strcmp(x.name, y.name);
}

static void drawTopicRow(const Topic &t, int y) {
  uint16_t dot = isActive(t) ? COLOR_OK : t.lastSeen ? COLOR_WARN : COLOR_FAINT;
  gfx->fillCircle(10, y + 3, 3, dot);

  char name[40];
  strlcpy(name, t.name, sizeof name);  // 38 characters fit before the rate column
  drawSmallText(name, 18, y, t.watched ? RGB565_WHITE : COLOR_DIM);

  char rate[12];
  if (!t.watched) {
    strcpy(rate, "skip");
  } else if (!isActive(t)) {
    strcpy(rate, "-");
  } else if (t.rate >= 1000.0f / THROTTLE_MS - 0.5f) {
    snprintf(rate, sizeof rate, "%d+ Hz", 1000 / THROTTLE_MS);
  } else {
    snprintf(rate, sizeof rate, "%.1f Hz", t.rate);
  }
  drawSmallText(rate, gfx->width() - 8 - strlen(rate) * 6, y, isActive(t) ? COLOR_OK : COLOR_DIM);
}

void rosDraw() {
  gfx->fillScreen(RGB565_BLACK);
  int right = drawText("ROS 2", 8, 17, &FreeSansBold9pt7b, RGB565_WHITE);

  Topic *present[MAX_TOPICS];
  int n = 0, active = 0;
  for (int i = 0; i < topicCount; i++) {
    if (!topics[i].present) continue;
    present[n++] = &topics[i];
    if (isActive(topics[i])) active++;
  }

  char buf[64];
  if (connected) {
    snprintf(buf, sizeof buf, "%d topics, %d active", n, active);
  } else {
    snprintf(buf, sizeof buf, "no rosbridge at %s", ROSBRIDGE_HOST);
  }
  drawSmallText(buf, right + 10, 8, connected ? COLOR_DIM : COLOR_BAD);
  gfx->fillCircle(gfx->width() - 12, 11, 4, connected ? COLOR_OK : COLOR_BAD);
  gfx->drawFastHLine(8, 22, gfx->width() - 16, COLOR_FAINT);

  qsort(present, n, sizeof present[0], compareTopics);
  int rows = (ROWS_BOTTOM - ROWS_TOP) / ROW_HEIGHT;
  for (int i = 0; i < n && i < rows; i++) {
    if (i == rows - 1 && n > rows) {
      snprintf(buf, sizeof buf, "+ %d more", n - i);
      drawSmallText(buf, 18, ROWS_TOP + i * ROW_HEIGHT, COLOR_DIM);
      break;
    }
    drawTopicRow(*present[i], ROWS_TOP + i * ROW_HEIGHT);
  }

  // Latest log line, coloured by severity (rcl_interfaces/Log: 30 = WARN, 40 = ERROR, 50 = FATAL).
  gfx->drawFastHLine(8, ROWS_BOTTOM + 2, gfx->width() - 16, COLOR_FAINT);
  uint16_t logColor = lastLogLevel >= 40 ? COLOR_BAD : lastLogLevel >= 30 ? COLOR_WARN : COLOR_CLOUD;
  char line[52];
  strlcpy(line, lastLog[0] ? lastLog : "no log messages yet", sizeof line);
  drawSmallText(line, 8, ROWS_BOTTOM + 8, lastLog[0] ? logColor : COLOR_DIM);
}
