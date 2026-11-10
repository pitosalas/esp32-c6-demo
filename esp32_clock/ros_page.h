#pragma once
// ROS 2 topic monitor page, fed by a rosbridge WebSocket connection.

void rosBegin();
void rosLoop();   // call often; services the WebSocket and updates topic rates
void rosDraw();   // draws the whole page into the canvas (caller flushes)
// Streams topics only while the page is on screen, so Wi-Fi can rest the rest of the time.
// /rosout stays subscribed so the latest log line is current when the page comes back.
void rosSetVisible(bool visible);
