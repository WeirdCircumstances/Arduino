#include <Adafruit_NeoPixel.h>

#define PIN            3

#define NUMPIXELS      10

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

int delayval = 50;

int i = 0;

void setup() {
  pixels.begin();
}

void loop() {
  for(i;i<NUMPIXELS-1;i++){    
    pixels.setPixelColor(i+2, pixels.Color(0,0,0));
    pixels.setPixelColor(i+1, pixels.Color(10,0,0));
    pixels.setPixelColor(i, pixels.Color(150,0,0));
    pixels.setPixelColor(i-1, pixels.Color(10,0,0));
    pixels.setPixelColor(i-2, pixels.Color(0,0,0));
    pixels.show();
    delay(delayval); 
  }
    for(i;i>0;i--){     
    pixels.setPixelColor(i+2, pixels.Color(0,0,0));
    pixels.setPixelColor(i+1, pixels.Color(10,0,0));
    pixels.setPixelColor(i, pixels.Color(150,0,0));
    pixels.setPixelColor(i-1, pixels.Color(10,0,0));
    pixels.setPixelColor(i-2, pixels.Color(0,0,0));
    pixels.show();
    delay(delayval);
  }
}
