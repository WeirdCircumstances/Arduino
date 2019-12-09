#include <Arduino.h>
#include <TM1637Display.h>

// Module connection pins (Digital Pins)
#define CLK 2
#define DIO 3

// The amount of time (in milliseconds) between tests
#define TEST_DELAY   200
#include <TimeLib.h>


const uint8_t SEG_DONE[] = {
	SEG_B | SEG_C | SEG_D | SEG_E | SEG_G,           // d
	SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,   // O
	SEG_C | SEG_E | SEG_G,                           // n
	SEG_A | SEG_D | SEG_E | SEG_F | SEG_G            // E
	};

TM1637Display display(CLK, DIO);

void setup()
{
}

void loop()
{

  int rando1 = random(10);
  int rando2 = random(10);
  int rando3 = random(10);
  int rando4 = random(10);
  uint8_t data[] = { 0xff, 0xff, 0xff, 0x00 };
  display.setBrightness(0xf0);

  uint8_t blank[] = { 0x00, 0x00, 0x00, 0x00 };
  display.setBrightness(0x0f);


    int    h = hour();
    int    m = minute();
    int    s = second();
    int    sEiner, sZehner;

    sEiner = s;
    if(sEiner >= 10){
      sEiner = 0;
    }

    if(sEiner%10 == 0 && sEiner != 0){
      sZehner++;
    }
  

  // Selectively set different digits
  data[0] = display.encodeDigit(h);
  data[1] = display.encodeDigit(m);
  data[2] = display.encodeDigit(s);
  data[3] = display.encodeDigit(0);
  display.setSegments(data);
  delay(TEST_DELAY);



}
