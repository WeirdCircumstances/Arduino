#include <Adafruit_NeoPixel.h>
#define PIN            3
#define NUMPIXELS      10
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  pixels.begin();
  pixels.setBrightness(50);
  pixels.show();
}

void loop() {
  static uint32_t timer = millis();

  if (millis() - timer > 1000) {
    timer = millis();
    uint16_t DecToBin = timer / 1000;
    for (int i=0; i < NUMPIXELS; i++) {
      bool j = DecToBin % 2;
      DecToBin /= 2;
      pixels.setPixelColor(i, pixels.Color(j * 150, 0, 0));
    }
    pixels.show();
  }
}
