#include <SPI.h>
#include <Wire.h>
// für den DUE in die Wire Bibliothek gehen und den clockspeed auf 400 mHz erhöhen!!!
// (eine 400000 statt 100000 bei clock speed)
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <stdint.h>
#include <SparkFunBME280.h> // Temperatur
#include <SD.h>
#include <TimeLib.h>

#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);
#define TIME_HEADER  "T"   // Header tag for serial time sync message
#define TIME_REQUEST  7    // ASCII bell character requests a time sync message 

BME280 bcme280;
File tempLog;

const int setButton = 32;
const int sdPin = 34;

int setState;                      // Abfragen des Set Knopfes
int sec;                           // temp sec Speicher
int setCount = 1;                  // setzt den Zustand set
float temp;                        // float für Nachkommastellen
float humid;                       // Luftfeuchtigkeit
int h;
int m;
int s;

// Anfangswerte erstmal auf false setzen, wird durch das Programm angepasst
boolean updateTemp  = false;            // soll die Temp ein update bekommen?
boolean oneSec      = false;            // ändert sich einmal pro Sekunde
boolean onceAMin    = false;            // Ereignis nur einmal pro Minute, nicht einmal pro Sekunde
boolean writelog    = false;            // öffne log.csv
boolean readlog     = false;            // schließe log.csv
boolean removeOld   = false;            // lösche alte log.csv Datei?
boolean serialTime  = false;            // wenn die Zeit über seriell angekommen ist...
//boolean writeOk     = false;           // konnten die Daten geschrieben werden? Für Displayausgabe

void setup()   {
  Serial.begin(9600);
  while (!Serial); // wait for serial monitor
  setSyncProvider(requestSync);

  if (!SD.begin(34)) {
    Serial.println("initialization failed!");
    return;
  }
  Serial.println("initialization done.");

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  pinMode(setButton, INPUT);

  setBCME280();

  //  welcomeScreen();

}

void loop() {

  getSomeSeriousTime();

  getTemp();

  readButtons();

  setTime();

  boolValues();

  showTime();

  delteoldFile();

  getData();

  drawGraph();

}

void readButtons() {
  setState = digitalRead(setButton);

  if (setState == HIGH) {
    if (setCount, setCount++, setCount < 3) {
      delay(200);
    }
    else {
      setCount = 0;
      delay(200);
    }
  }
}

void setTime() {
  if (setCount == 0) {

    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Bens clock v0.3");
    display.print("todo: bat, ");

    display.setTextSize(4);
    display.setCursor(4, 20);

    displayDigits(hour());
    showColon();
    displayDigits(minute());

    display.println();
    display.setTextSize(1);
    display.print("time: ");

    displayDigits(hour());
    showColon();
    displayDigits(minute());
    showColon();
    displayDigits(second());

    display.display();
  }
}

void boolValues() {
  sec = second();
  if (sec % 2 == 0) oneSec = true;

  else oneSec = false;

  if (second() == 0) {
    if (onceAMin == true)
      updateTemp = true;
    onceAMin = false;
  }
  else onceAMin = true;
}

void showTime() {
  if (setCount == 1) {
    display.clearDisplay();
    //  display.fillRect(x,y,width,height, BLACK);
    display.setTextColor(WHITE);
    display.setTextSize(4);
    display.setCursor(5, 0); //(4,20) für ungefähr Mitte

    displayDigits(hour());
    showColon();
    displayDigits(minute());

    //display.fillRect(0, 32, 128, 32, BLACK);

    display.println();
    display.setTextSize(1);
    display.print(temp, 2);
    display.print(" C   ");
    display.print(humid, 2);
    display.println(" %");

    display.print("Last write: ");
    displayDigits(h);
    display.print(":");
    displayDigits(m);
    display.print(":");
    displayDigits(s);

    display.display();
  }
}

void getSomeSeriousTime() {

  if (serialTime == false) {
    if (Serial.available()) {
      processSyncMessage();
      Serial.print("Waiting for time...");
    }
    if (timeStatus() != timeNotSet) {
      serialTime = true;                        // nachdem die Zeit eingerichtet wurde, wird
      Serial.println("done.");   // das logging begonnen

    }
  }
}

void processSyncMessage() {
  unsigned long pctime;
  const unsigned long DEFAULT_TIME = 1357041600; // Jan 1 2013

  if (Serial.find(TIME_HEADER)) {
    pctime = Serial.parseInt();
    if ( pctime >= DEFAULT_TIME) { // check the integer is a valid time (greater than Jan 1 2013)
      setTime(pctime); // Sync Arduino clock to the time received on the serial port
    }
  }
}

time_t requestSync() {
  Serial.write(TIME_REQUEST);
  return 0; // the time will be sent later in response to serial mesg
}

void getTemp() {
  if (updateTemp == true) {
    temp = bcme280.readTempC();
    humid = bcme280.readFloatHumidity();
    writelog = true;
    readlog = true;
    updateTemp = false;
  }
}

void getData() {

  if (serialTime == true) {
    if (writelog == true) {
      tempLog = SD.open("log.csv", FILE_WRITE);

      if (tempLog) {
        Serial.println();
        Serial.print("Writing to log.csv...");
        //   writeOk = true;
        tempLogDigits(day());
        tempLog.print(",");
        tempLogDigits(month());
        tempLog.print(",");
        tempLogDigits(year());
        tempLog.print(",");
        tempLogDigits(hour());
        tempLog.print(",");
        tempLogDigits(minute());
        tempLog.print(",");
        tempLogDigits(second());
        tempLog.print(",");
        tempLog.print(temp, 2);
        tempLog.print(",");
        tempLog.println(humid, 2);
        writelog = false;

        h = hour();
        m = minute();
        s = second();

        // close the file:
        tempLog.close();
        Serial.println("done.");
      }
      else {
        Serial.println("error opening log.csv");
        //    writeOk = false;
      }

    }

    /*

        // Funktion ist gestört durch überschreiben von templog... bin zu müde zu fixen, ist aber einfach...

          // re-open the file for reading:
          tempLog = SD.open("log.csv");
          if (tempLog) {
            Serial.println("log.csv:");

      //         read from the file until there's nothing else in it:
                      while (tempLog.available())
                      Serial.write(tempLog.read());

                 else Serial.println("error opening log.csv");

    */
    /*
      tempLog = SD.open("log.csv", FILE_READ);
      if (readlog == true) {
            // read last line
            while (true) {
              cr = tempLog.read();
              if ((cr == '\n') || (cr == '\r') || (cr < 0))
                break;
              Serial.write(cr);
            }
            tempLog.close();
            readlog = false;
          }
        }
      //*/
  }
}

void displayDigits(int digits) {
  if (digits < 10)
    display.print('0');
  display.print(digits);
}

void tempLogDigits(int digits) {
  if (digits < 10)
    tempLog.print('0');
  tempLog.print(digits);
}

void showColon() {
  if (oneSec == true) display.print(":");
  else display.print(" ");
}

void delteoldFile() {
  if (removeOld == true) {
    Serial.println("Removing old log.csv...");
    SD.remove("log.csv");
    removeOld = false;
  }
}

void welcomeScreen() {
  display.clearDisplay();

  drawStar();
  display.clearDisplay();

  testdrawcircle();
  display.clearDisplay();

  testdrawline();
  display.clearDisplay();

  helloscreen();
  display.clearDisplay();

  circlewithversion();
  display.clearDisplay();
}

void drawGraph() {
  if (setCount == 2) {

    display.clearDisplay();

    // aktuelle Daten anzeigen
    display.setCursor(2, 0);
    //    display.fillRect(0, 32, 128, 32, BLACK);
    display.setTextSize(1);
    display.print(temp, 2);
    display.print("C ");
    display.setCursor(44, 0);
    display.print(humid, 2);
    display.print("% ");
    display.setCursor(86, 0);
    displayDigits(hour());
    showColon();
    displayDigits(minute());

    // Diagramm zeichnen
    display.drawLine(0, 0, 0, 48, WHITE);       // Y-Achse
    display.drawLine(0, 47, 127, 47, WHITE);    // X-Achse
    display.drawPixel(25, 48, WHITE);
    display.drawPixel(50, 48, WHITE);
    display.drawPixel(75, 48, WHITE);
    display.drawPixel(100, 48, WHITE);
    display.drawPixel(125, 48, WHITE);

    // Diagramm Beschriftung
    display.setTextSize(1);
    display.setCursor(0, 55);
    display.print("0   ");
    display.print("1   ");
    display.print("2   ");
    display.print("3   ");
    display.print("4   ");
    display.print("5");

    display.display();
  }
}

void drawStar() {
  for (int16_t i = 0; i < 64; i++) {
    display.clearDisplay();

    display.drawLine(64, 32, 0 + 2 * i, 0, WHITE); //oben links
    display.drawLine(64, 32, 128 - 2 * i, 64, WHITE); //unten rechts
    display.drawLine(64, 32, 0, 64 - i, WHITE); // unten links
    display.drawLine(64, 32, 128, 0 + i, WHITE); //oben rechts

    display.drawLine(64, 32, 64 + i,   -32 + i,  WHITE); //oben
    display.drawLine(64, 32, 128 - i,  32 + i,   WHITE); //rechts
    display.drawLine(64, 32, 64 - i,   96 - i,   WHITE); // unten
    display.drawLine(64, 32, 0 + i,    32 - i,   WHITE); //links

    //      display.drawLine(64,32,   64+2*i,-96+2*i,  WHITE); //oben
    //      display.drawLine(64,32, 128-i,  32+i,   WHITE); //rechts
    //      display.drawLine(64,32, 64-i,   96-i,   WHITE); // unten
    //      display.drawLine(64,32, 0+i,    32-i,   WHITE); //links

    display.display();
  }
}

void testdrawcircle(void) {
  for (int16_t i = 0; i < display.height(); i += 2) {
    display.drawCircle(display.width() / 2, display.height() / 2, i, WHITE);
    display.display();
    delay(1);
  }
}

void circlewithversion() {
  for (int16_t i = 0; i < 70; i++) {
    display.drawCircle(64, 32, 0 + i, WHITE);
    //delay(1);
    display.setTextColor(BLACK);
    display.setTextSize(2);
    display.setCursor(5, 20);
    display.println("Bens clock");
    display.setCursor(40, 40);
    display.println("v0.3");
    display.setTextColor(WHITE);
    display.display();
  }
}

void helloscreen() {
  for (int16_t i = 0; i < 149; i++) {
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.drawCircle(10 + i, 10 + i, 10 + i, WHITE);
    display.drawCircle(117 - i, 53 - i, 10 + i, WHITE);
    display.drawCircle(117 - i, 10 + i, 10 + i, WHITE);
    display.drawCircle(10 + i, 53 - i, 10 + i, WHITE);
    //delay(1);
    display.setTextSize(3);
    display.setCursor(12, 20);
    display.print("Hello!");
    display.display();
  }
}

void testdrawline() {
  for (int16_t i = 0; i < display.width(); i += 4) {
    display.drawLine(0, 0, i, display.height() - 1, WHITE);
    display.display();
    delay(1);
  }
  for (int16_t i = 0; i < display.height(); i += 4) {
    display.drawLine(0, 0, display.width() - 1, i, WHITE);
    display.display();
    delay(1);
  }
  delay(250);

  display.clearDisplay();
  for (int16_t i = 0; i < display.width(); i += 4) {
    display.drawLine(0, display.height() - 1, i, 0, WHITE);
    display.display();
    delay(1);
  }
  for (int16_t i = display.height() - 1; i >= 0; i -= 4) {
    display.drawLine(0, display.height() - 1, display.width() - 1, i, WHITE);
    display.display();
    delay(1);
  }
  delay(250);

  display.clearDisplay();
  for (int16_t i = display.width() - 1; i >= 0; i -= 4) {
    display.drawLine(display.width() - 1, display.height() - 1, i, 0, WHITE);
    display.display();
    delay(1);
  }
  for (int16_t i = display.height() - 1; i >= 0; i -= 4) {
    display.drawLine(display.width() - 1, display.height() - 1, 0, i, WHITE);
    display.display();
    delay(1);
  }
  delay(250);

  display.clearDisplay();
  for (int16_t i = 0; i < display.height(); i += 4) {
    display.drawLine(display.width() - 1, 0, 0, i, WHITE);
    display.display();
    delay(1);
  }
  for (int16_t i = 0; i < display.width(); i += 4) {
    display.drawLine(display.width() - 1, 0, i, display.height() - 1, WHITE);
    display.display();
    delay(1);
  }
  delay(250);
}

void setBCME280() {
  bcme280.settings.commInterface = I2C_MODE;
  bcme280.settings.I2CAddress = 0x77;
  bcme280.settings.runMode = 3; //Normal mode
  bcme280.settings.tStandby = 0;
  bcme280.settings.filter = 0;
  bcme280.settings.tempOverSample = 1;
  bcme280.settings.pressOverSample = 1;
  bcme280.settings.humidOverSample = 1;
  Serial.println(bcme280.begin(), HEX);
}
