#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
  #include <avr/power.h>
#endif

// Which pin on the Arduino is connected to the NeoPixels?
// On a Trinket or Gemma we suggest changing this to 1
#define PIN            6

// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS      1

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, 6, NEO_GRB + NEO_KHZ800);

int Taste = 2; // Taste an digital Pin 2
int setCount = 0;
int setState;

void setup() {
  Serial.begin(9600);  

  pinMode(Taste, INPUT);

  #if defined (__AVR_ATtiny85__)
    if (F_CPU == 16000000) clock_prescale_set(clock_div_1);
  #endif
  // End of trinket special code

  pixels.begin(); // This initializes the NeoPixel library.

}

void loop() {

  readButtons();

  Serial.println(setState);
  Serial.println(setCount);
  
  // oder == 1, musst du mal gucken, was da rauskommt
  // du kannst auch mal sehen, was ausgegeben wird mit der seriellen Konsole: 
  
  if(setCount == 0){ 
    for(int i=0;i<NUMPIXELS;i++){
      pixels.setPixelColor(i, pixels.Color(0,155,0)); // Mache LED 1 grün

      pixels.show(); // schicke den Befehl an den Pixel (oder wenn es mehrere sind auch Pixel)

      delay(100); // warte danach noch eine Sekunde, bevor es neu geprüft iwrd, ob die Taste gedrückt wurde
    }
  }
  if(setCount == 1){ 
    for(int i=0;i<NUMPIXELS;i++){
      pixels.setPixelColor(i, pixels.Color(155,0,0)); // Mache LED 1 rot

      pixels.show(); // schicke den Befehl an den Pixel (oder wenn es mehrere sind auch Pixel)

      delay(100); // warte danach noch eine Sekunde, bevor es neu geprüft iwrd, ob die Taste gedrückt wurde
    }
  }
  if(setCount == 2){ 
    for(int i=0;i<NUMPIXELS;i++){
      pixels.setPixelColor(i, pixels.Color(0,0,155)); // Mache LED 1 rot

      pixels.show(); // schicke den Befehl an den Pixel (oder wenn es mehrere sind auch Pixel)

      delay(100); // warte danach noch eine Sekunde, bevor es neu geprüft iwrd, ob die Taste gedrückt wurde
    }
  }
    if(setCount == 3){ 
    for(int i=0;i<NUMPIXELS;i++){
      pixels.setPixelColor(i, pixels.Color(155,155,0)); // Mache LED 1 rot

      pixels.show(); // schicke den Befehl an den Pixel (oder wenn es mehrere sind auch Pixel)

      delay(100); // warte danach noch eine Sekunde, bevor es neu geprüft iwrd, ob die Taste gedrückt wurde
    }
  }
      if(setCount == 4){ 
    for(int i=0;i<NUMPIXELS;i++){
      pixels.setPixelColor(i, pixels.Color(0,155,155)); // Mache LED 1 rot

      pixels.show(); // schicke den Befehl an den Pixel (oder wenn es mehrere sind auch Pixel)

      delay(100); // warte danach noch eine Sekunde, bevor es neu geprüft iwrd, ob die Taste gedrückt wurde
    }
  }
  if(setCount == 5){ 
    for(int i=0;i<NUMPIXELS;i++){
      pixels.setPixelColor(i, pixels.Color(155,155,155)); // Mache LED 1 rot

      pixels.show(); // schicke den Befehl an den Pixel (oder wenn es mehrere sind auch Pixel)

      delay(100); // warte danach noch eine Sekunde, bevor es neu geprüft iwrd, ob die Taste gedrückt wurde
    }
  }
}

void readButtons() {
  setState = !digitalRead(Taste);

  if (setState == HIGH) {
    if (setCount, setCount++, setCount < 6) {
      delay(200);
    }
    else {
      setCount = 0;
      delay(200);
    }
  }
}
