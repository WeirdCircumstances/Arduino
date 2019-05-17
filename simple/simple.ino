#include <Adafruit_NeoPixel.h>

#define PIN            3

#define NUMPIXELS      10

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

int delayval = 100;

int i;

void setup() {
  pixels.begin();
}

void loop() {

   int x = random(2);
   int y = random(2);
   int z = random(2);
   
  for(i;i<NUMPIXELS-1;i++){    
    pixels.setPixelColor(i+2, pixels.Color(0,0,0));
    pixels.setPixelColor(i+1, pixels.Color(x*10,y*10,z*10));
    pixels.setPixelColor(i, pixels.Color(x*150,y*150,z*150));
    pixels.setPixelColor(i-1, pixels.Color(x*10,y*10,z*10));
    pixels.setPixelColor(i-2, pixels.Color(0,0,0));
    pixels.show();
    delay(delayval); 
  }

    for(i;i>0;i--){     
    pixels.setPixelColor(i+2, pixels.Color(0,0,0));
    pixels.setPixelColor(i+1, pixels.Color(x*10,y*10,z*10));
    pixels.setPixelColor(i, pixels.Color(x*150,y*150,z*150));
    pixels.setPixelColor(i-1, pixels.Color(x*10,y*10,z*10));
    pixels.setPixelColor(i-2, pixels.Color(0,0,0));
    pixels.show();
    delay(delayval);
  }
  //*/
}
