#include <OneWire.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// für den NANO in die Wire Bibliothek gehen und den clockspeed auf 100 mHz einstellen!!!
// (eine 100000 statt 400000 bei clock speed)

#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);

OneWire  ds(10);  // on pin 10 (a 4.7K resistor is necessary)

  bool ping = true;
  int x = 0;

void setup(void) {
  Serial.begin(9600);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  
// welcome screen
  welcome();
}

void loop(void) {
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte addr[8];
  float celsius;
  int16_t raw;

  
 if ( !ds.search(addr)) {
    Serial.println("No more addresses.");
    Serial.println();
    ds.reset_search();
    delay(1000);
    return;
  }
//*/
  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  
  delay(1000);     // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.
  
  present = ds.reset();
  ds.select(addr);    
  ds.write(0xBE);         // Read Scratchpad

  for ( i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
  }

  raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }

  
  celsius = (float)raw / 16.0;

 // Serial.println(celsius);

    ping = true;
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.println("Celsius:");
    display.println();
    display.setCursor(0,20);
    display.setTextSize(4);
    display.print(celsius);

    if(ping == true)
      x++;
    if(x >= 122)
      ping = false;
    if(ping == false)
      x--;
    if(x <= 0)
      ping = true;
    
    display.drawCircle(3 + x, 60, 2, WHITE);

    display.setCursor(5,55);
    display.setTextSize(1);
    display.print(x);
    display.display();
//    delay(10);
//*/ 

}

void welcome() {
  display.clearDisplay();
  display.setTextSize(3);
  display.setTextColor(WHITE);
  display.setCursor(10, 20);
  display.print("Hi! :P");
  for (int i = 0; i < 10; i++) {
    display.invertDisplay(true);
    display.display();
    delay(10);
    display.invertDisplay(false);
    delay(10);
    display.display();
  }
}
