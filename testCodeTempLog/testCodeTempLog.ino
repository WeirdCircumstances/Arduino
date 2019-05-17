#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <stdint.h>
#include <SparkFunBME280.h>
//#include <SD.h>

#define OLED_RESET 4 // not used / nicht genutzt bei diesem Display 
Adafruit_SSD1306 display(OLED_RESET);

BME280 mySensor;

const int timeButton = 2;
const int setButton = 3;

// variables will change:
int timeState;         // variable for reading the pushbutton status
int setState;

int days;
int hours;
int minutes;
int seconds = 12;
int setCount = 3;

//int vPixel;
//int hPixel;
int i;

const long interval = 1000;        // Umrechnen von Millisekunden zu Sekunden
unsigned long previousMillis;     // zur Berechung der Zeitdifferenz, wann eine Sekunde vergangen ist

boolean updateTemp = false;
boolean oneSec = false;

const int sdPin = 4;


void setup() {
  // initialize with the I2C addr 0x3C / mit I2C-Adresse 0x3c initialisieren
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  pinMode(timeButton, INPUT);
  pinMode(setButton, INPUT);

  //////////////////////////////////////
  //
  // for BCME280 temperture sensor
  //
  //////////////////////////////////////

  mySensor.settings.commInterface = I2C_MODE;
  mySensor.settings.I2CAddress = 0x77;
  mySensor.settings.runMode = 3; //Normal mode
  mySensor.settings.tStandby = 0;
  mySensor.settings.filter = 0;
  mySensor.settings.tempOverSample = 1;
  mySensor.settings.pressOverSample = 1;
  mySensor.settings.humidOverSample = 1;

  //nur für Testzwecke
  //  Serial.begin(57600);
  Serial.println(mySensor.begin(), HEX);
}


#define DRAW_DELAY 118
#define D_NUM 47

void loop() {
  
      unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    seconds++;
    previousMillis = currentMillis;
    oneSec = true; 
    if (seconds >= 60) {
      minutes++;
      seconds = 0;
      updateTemp = false;
      if (minutes >= 60) {
        hours++;
        minutes = 0;
        if (hours >= 24) {
          days++;
          hours = 0;
        }
      }
    }
  }

  if(updateTemp == true){
    
    display.clearDisplay();

// aktuelle Daten anzeigen
    display.setCursor(64, 0);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.print(mySensor.readTempC(), 2);
    display.print(" C");
    display.setCursor(64, 9);
    display.print(mySensor.readFloatHumidity(), 2);
    display.print(" %");

// Diagramm zeichnen
    display.drawLine(0,0,0,48, WHITE);
    display.drawLine(0,47,127,47, WHITE);
    display.drawLine(25,47,25,48, WHITE);
    display.drawLine(50,47,50,48, WHITE);
    display.drawLine(75,47,75,48, WHITE);
    display.drawLine(100,47,100,48, WHITE);
    display.drawLine(125,47,125,48, WHITE);

// Diagramm Beschriftung
    display.setCursor(0,55);
    display.print("0   ");
    display.print("1   ");
    display.print("2   ");
    display.print("3   ");
    display.print("4   ");
    display.print("5");

    display.display();
    oneSec = false;
    updateTemp = false;
  }

    for(i=0;i<64;i++){
     display.clearDisplay();

      display.drawLine(64,32,0+2*i,0, WHITE); //oben links
      display.drawLine(64,32,128-2*i,64, WHITE); //unten rechts
      display.drawLine(64,32,0,64-i, WHITE); // unten links
      display.drawLine(64,32,128,0+i, WHITE); //oben rechts

      display.drawLine(64,32, 64+i,   -32+i,  WHITE); //oben
      display.drawLine(64,32, 128-i,  32+i,   WHITE); //rechts
      display.drawLine(64,32, 64-i,   96-i,   WHITE); // unten
      display.drawLine(64,32, 0+i,    32-i,   WHITE); //links

//      display.drawLine(64,32,   64+2*i,-96+2*i,  WHITE); //oben
//      display.drawLine(64,32, 128-i,  32+i,   WHITE); //rechts
//      display.drawLine(64,32, 64-i,   96-i,   WHITE); // unten
//      display.drawLine(64,32, 0+i,    32-i,   WHITE); //links


    
      display.display();
  }

/*    for(i=0;i<65;i++){
      display.clearDisplay();
      display.drawLine(64,32,64+i,-32+i, WHITE); //oben
      display.drawLine(64,32,128-i,32+i, WHITE); //rechts
      display.drawLine(64,32,64-i,97-i, WHITE); // unten
      display.drawLine(64,32,0+i,32-i, WHITE); //links
    
      display.display();
    }
    */
}
