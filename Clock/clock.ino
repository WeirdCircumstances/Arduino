#include <SPI.h>
#include <Wire.h>

// Display
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <stdint.h>

// Temperatur
#include <SparkFunBME280.h>

#define OLED_RESET 4 // not used / nicht genutzt bei diesem Display 
Adafruit_SSD1306 display(OLED_RESET);

BME280 mySensor;

const int timeButton = 30;
const int setButton = 32; //Achtung, nicht ändern!!!

// variables will change:
int timeState;         // variable for reading the pushbutton status
int setState;

int days;
int hours;
int minutes;
int seconds = 12;
int setCount = 0; // <- hier ändern!!!

//int vPixel;
//int hPixel;
int i;

const long interval = 1000;        // Umrechnen von Millisekunden zu Sekunden
unsigned long previousMillis;      // zur Berechung der Zeitdifferenz, wann eine Sekunde vergangen ist

boolean updateTemp = false;
boolean oneSec = false;

void setup()   {

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

  // es wird sofort begonnen die Zeit zu zählen
  // die Funktion millis() wartet nicht wie delay, sondern nimmt die reale Zeit

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    seconds++;
    previousMillis = currentMillis;
    oneSec = true; 
    if (seconds >= 60) {
      minutes++;
      seconds = 0;
      updateTemp = true;
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


  //////////////////////////////////////////////
  ////
  ////  Welcome Screen
  ////
  /////////////////////////////////////////////

  if (setCount == 0) {

    display.clearDisplay();

    for (seconds % 2 == 0; i < 150; i++) {
      display.clearDisplay();
      display.setTextColor(WHITE);
      display.drawCircle(10 + i, 10 + i, 10 + i, WHITE);
      display.drawCircle(117 - i, 53 - i, 10 + i, WHITE);
      display.drawCircle(117 - i, 10 + i, 10 + i, WHITE);
      display.drawCircle(10 + i, 53 - i, 10 + i, WHITE);
      //      delay(DRAW_DELAY);
      display.setTextSize(3);
      display.setCursor(12, 20);
      display.print("Hello!");
      display.display();
    }

    i = 0;
    display.clearDisplay();

    for (seconds % 2 == 0; i < 70; i++) {
      display.drawCircle(64, 32, 0 + i, WHITE);
      display.display();
      //      delay(DRAW_DELAY);
      display.setTextColor(BLACK);
      display.setTextSize(2);
      display.setCursor(5, 20);
      display.println("Bens clock");
      display.setCursor(40, 40);
      display.println("v0.2");
      display.setTextColor(WHITE);
    }

    i = 0;

    setCount++;

    //  display.display();
    //  display.clearDisplay();


  }

  ////////////////////////////////////////////////
  ////
  //// Uhrzeit einstellen:
  ////
  ////////////////////////////////////////////////

  timeState = digitalRead(timeButton);
  setState = digitalRead(setButton);

  //auslesen der Knoepfe
  //Set Knopf definiert mehrere Zustaende: 0 = Willkommens Bildschirm, 1 = Stunden einstellen, 2 = Minuten einstellen, 3 = Uhrzeit anzeigen


  if (setState == HIGH) {
    if (setCount, setCount++, setCount < 4) {
      delay(200);
    }
    else {
      setCount = 1;
      delay(200);
    }
  }

  //Stunden einstellen
  if (timeState == HIGH && setCount == 1) {
    if (hours, hours++, hours < 24) {
      delay(200);
    }
    else {
      hours = 0;
      delay(200);
    }
  }

  //Minuten einstellen
  if (timeState == HIGH && setCount == 2) {
    if (minutes, minutes++, minutes < 60) {
      delay(200);
    }
    else {
      minutes = 0;
      delay(200);
    }
  }



  ////////////////////////////////////////////////////////////
  //
  //Anzeigen der Einstellungen (hoffentlich selbsterklaerend)
  //
  ////////////////////////////////////////////////////////////

  if (setCount == 1 || setCount == 2) {

    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Bens clock v0.2");
    display.print("todo: bat, log, ");
    display.println(setCount);

    display.setTextSize(4);
    display.setCursor(4, 20);

    //bringt die Anzeige fuer Stunden zum blinken
    if (seconds % 2 == 0 && setCount == 1 && !(timeState == HIGH)) {
      display.setTextColor(BLACK);
    }

    if (hours < 10) {
      display.print("0");
    }
    display.print(hours);
    display.setTextColor(WHITE);


    display.print(":");

    //einstellen der Minuten
    if (seconds % 2 == 0 && setCount == 2 && !(timeState == HIGH)) {
      display.setTextColor(BLACK);
    }

    if (minutes < 10) {
      display.print("0");
    }
    display.println(minutes);
    display.setTextColor(WHITE);


    display.setTextSize(1);
    display.print("time: ");
    if (days < 10) {
      display.print("0");
    }
    display.print(days);
    display.print(":");
    if (hours < 10) {
      display.print("0");
    }
    display.print(hours);
    display.print(":");

    if (minutes < 10) {
      display.print("0");
    }
    display.print(minutes);
    display.print(":");

    if (seconds < 10) {
      display.print("0");
    }
    display.print(seconds);

    display.display();
    display.clearDisplay();
  }


  ///////////////////////////////////////////////
  //
  //ab hier wird die Uhrzeit angezeigt
  //
  ///////////////////////////////////////////////

  if (setCount == 3 && oneSec == true) {

//  display.clearDisplay();
//  display.fillRect(x,y,width,height, BLACK); 
    display.fillRect(0,0,128,32, BLACK); 
    display.setTextColor(WHITE);
    display.setTextSize(4);
    display.setCursor(5, 0); //(4,20) für ungefähr Mitte

    if (hours < 10) {
      display.print("0");
    }
    display.print(hours);

    if (seconds % 2 == 0) {
      display.print(":");
    }
    else {
      display.print(" ");
    }

    if (minutes < 10) {
      display.print("0");
    }
    display.println(minutes);


  if(updateTemp == true){
    display.fillRect(0,32,128,32, BLACK);
    display.setTextSize(1);
    display.print(mySensor.readTempC(), 2);
    display.println(" C");
    display.print(mySensor.readFloatHumidity(), 2);
    display.print(" %");
   // updateTemp = false;
  }
  
    display.display();
   // oneSec = false; // damit wird die Funktion nur einmal pro Sekunde ausgeführt
  }
}
