#include <Arduino.h>
#include <string.h>
#include <stdio.h>

#define UPDATE_INTERVALL 5  // microseconds

#define MAX_C 3    
#define MAX_X 300 
#define BAUD 115200

uint8_t y[MAX_C][MAX_X];
uint8_t used_c = 0;  // number of used layers
uint16_t used_x = 0; // number of logical x columns used

typedef enum {
  RECEIVE,
  DRAW
} state_t;

state_t state = RECEIVE;

void clear_image() {
  for (uint8_t c = 0; c < MAX_C; c++) {
    for (uint16_t x = 0; x < MAX_X; x++) {
      y[c][x] = 0;   
    }
  }
  used_c = 0;
  used_x = 0;
}

void storeRange(uint16_t x, uint8_t c, uint8_t bottom, uint8_t top) {
  if (c >= MAX_C){
    return;
  }
  if ((x * 2 + 1) >= MAX_X){
    return;
  }

  y[c][x * 2]     = bottom;
  y[c][x * 2 + 1] = top;

  if (c + 1 > used_c) used_c = c + 1;
  if (x + 1 > used_x) used_x = x + 1;
}

bool handleLine(char *line) {
  if (strcmp(line, "END") == 0) {
    return true;
  }

  unsigned int x, c, b, t;
  int parsed = sscanf(line, "X,%u,%u,%u,%u", &x, &c, &b, &t);

  if (x <= 65535u && c <= 255u && b <= 255u && t <= 255u) {
    storeRange((uint16_t)x, (uint8_t)c, (uint8_t)b, (uint8_t)t);
  }
  
  return false;
}

bool receive(){
  static char line[64];
  static uint8_t idx = 0;

  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\r'){
      continue;
    }

    if (c == '\n') {
      line[idx] = '\0';

      if (idx > 0){
        if (handleLine(line)){
          return true;
        }
      } 
      idx = 0;

      continue;
    }

    line[idx] = c;
    idx++;
  }

  return false;
}

void draw_image() {
  static uint32_t last_update = 0;
  static uint32_t last_layer = 0;

  for (uint8_t layer = 0; layer < used_c; layer++) {
  while (micros() - last_layer < 6000) {}
  last_layer = micros();

  // Trigger pulse
  PORTD = 0xFF;
  while ((micros() - last_layer) < 20) {}

  // Whitespace
  PORTD = 0;
  while ((micros() - last_layer) <= 1000) {}

  // Image layer
  for (uint16_t x = 0; x < (used_x * 2u); x++) {
    while ((micros() - last_update) < UPDATE_INTERVALL) {}
    last_update = micros();

    PORTD = y[layer][x];
  }

  // Whitespace
  PORTD = 0x00;
  }
}


int main(void){
  init();
  DDRD = 0xF0;

  Serial.begin(BAUD);
  clear_image();

  while (true){
    switch (state) {
      case RECEIVE:
        if (receive()){
          DDRD = 0xFF;
          state = DRAW;
        }
        break;

      case DRAW:
        draw_image();
        break;

      default:
        state = RECEIVE; // safer default
        break;
    }
  }
}

