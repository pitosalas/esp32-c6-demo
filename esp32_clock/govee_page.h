#pragma once
// Govee thermometer page: temperature and humidity from the Govee cloud API.

struct Thermometer {
  char sku[12];
  char device[32];
  char name[32];
  bool online;
  bool valid;    // has a reading
  float temp;    // Fahrenheit
  float humidity;
};

// The known thermometers, for other pages to show. Returns how many there are.
int goveeThermometers(const Thermometer **list);

void goveeBegin();
void goveeLoop();   // fetches the device list and readings on a schedule
void goveeDraw();   // draws the whole page into the canvas (caller flushes)
