#pragma once
// ROS 2 topic monitor page, fed by a rosbridge WebSocket connection.

void rosBegin();
void rosLoop();   // call often; services the WebSocket and updates topic rates
void rosDraw();   // draws the whole page into the canvas (caller flushes)
