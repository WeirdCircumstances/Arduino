#include <Adafruit_NeoPixel.h>
#include <Servo.h>
#ifdef __AVR__
  #include <avr/power.h>
#endif

#define PIN 6

Servo myservo;

Adafruit_NeoPixel strip = Adafruit_NeoPixel(20, PIN, NEO_GRB + NEO_KHZ800);

int button1 = 2;
int button2 = 3;
int buttonState1 = 0; 
int buttonState2;
int setState1, setState2;
int pos = 90;
int myServo = 9;
int controllPin = 8;

void setup() {
  Serial.begin(9600);
  strip.begin();
  strip.show();
  pinMode(button1, INPUT);
  pinMode(button2, INPUT);
  pinMode(myServo, OUTPUT);
  pinMode(controllPin, OUTPUT);
  myservo.attach(9);
}

void loop() {
  readButton1();
  readButton2();

  Serial.print("setState2: ");
  Serial.println(setState2);
  Serial.print("buttonState2: ");
  Serial.println(buttonState2);

  switch(buttonState1){
    case 0:
      colorWipe(strip.Color(255, 0, 0), 50);
      break;
    case 1:
      colorWipe(strip.Color(0, 255, 0), 50);
      break;
    case 2:
      colorWipe(strip.Color(0, 0, 255), 50);
      break;
    case 3:
      theaterChase(strip.Color(127, 127, 127), 50);
      break;
    case 4:
      theaterChase(strip.Color(127, 0, 0), 50);
      break;
    case 5:
      theaterChase(strip.Color(0, 0, 127), 50);
      break;
    case 6:
      rainbow(10);
      break;
    case 7:
      rainbowCycle(30);
      break;
    case 8:
      theaterChaseRainbow(20);
      break;
  }
}

// Fill the dots one after the other with a color
void colorWipe(uint32_t c, uint8_t wait) {
  for(uint16_t i=0; i<strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
  }
}

void rainbow(uint8_t wait) {
  uint16_t i, j;

  for(j=0; j<256; j++) {
//    for(j=0; j<100; j++) {
    for(i=0; i<strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel((i+j) & 255));
    }
    strip.show();
    delay(wait);
  }
}

// Slightly different, this makes the rainbow equally distributed throughout
void rainbowCycle(uint8_t wait) {
  uint16_t i, j;

  for(j=0; j<256*5; j++) { // 5 cycles of all colors on wheel
    for(i=0; i< strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + j) & 255));
    }
    strip.show();
    delay(wait);
  }
}

//Theatre-style crawling lights.
void theaterChase(uint32_t c, uint8_t wait) {
  for (int j=0; j<2; j++) {  //do 10 cycles of chasing
    for (int q=0; q < 3; q++) {
      for (uint16_t i=0; i < strip.numPixels(); i=i+3) {
        strip.setPixelColor(i+q, c);    //turn every third pixel on
      }
      strip.show();

      delay(wait);

      for (uint16_t i=0; i < strip.numPixels(); i=i+3) {
        strip.setPixelColor(i+q, 0);        //turn every third pixel off
      }
    }
  }
}

//Theatre-style crawling lights with rainbow effect
void theaterChaseRainbow(uint8_t wait) {
  for (int j=0; j < 256; j++) {     // cycle all 256 colors in the wheel
    for (int q=0; q < 3; q++) {
      for (uint16_t i=0; i < strip.numPixels(); i=i+3) {
        strip.setPixelColor(i+q, Wheel( (i+j) % 255));    //turn every third pixel on
      }
      strip.show();

      delay(wait);

      for (uint16_t i=0; i < strip.numPixels(); i=i+3) {
        strip.setPixelColor(i+q, 0);        //turn every third pixel off
      }
    }
  }
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void readButton1() {
  setState1 = digitalRead(button1);

  if (setState1 == 1) {
    if (buttonState1, buttonState1++, buttonState1 <= 8) {
      delay(200);
    }
    else {
      buttonState1 = 0;
      delay(200);
      }
    }
  }

void readButton2() {
  setState2 = digitalRead(button2);

  if (setState2 == 1) {
    buttonState2++;
    digitalWrite(controllPin, HIGH);
    serverino();
    digitalWrite(controllPin, LOW);
  }
  }

void serverino(){
    for (pos = 90; pos <= 180; pos += 1) { 
    myservo.write(pos);              
    delay(15);                       
  }
  delay(1000);
  for (pos = 180; pos >= 90; pos -= 1) {
    myservo.write(pos);
    delay(15);
  }

//  for(int i; 1<=10; i++){
//    digitalWrite(myServo,HIGH);
//    delayMicroseconds(1500);
//    digitalWrite(myServo,LOW);
//    delayMicroseconds(18500);  
//  }
}
