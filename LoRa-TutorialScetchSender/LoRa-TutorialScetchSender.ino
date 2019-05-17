#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);

// wichtig für die normale Adafruit Display Bib!
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RST 4

//define the pins used by the transceiver module
#define ss 18
#define rst 12
#define dio0 26

int counter = 1;

void setup() {
  //initialize Serial Monitor
  Serial.begin(115200);
  while (!Serial);
  Serial.println("LoRa Sender");

  //setup LoRa transceiver module
  LoRa.setPins(ss, rst, dio0);

// wichtig für die normale Adafruit Display Bib! 2. Teil
  Wire.begin( OLED_SDA, OLED_SCL);
  
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); 
  display.setRotation(2);
  
  //replace the LoRa.begin(---E-) argument with your location's frequency 
  //433E6 for Asia
  //866E6 for Europe
  //915E6 for North America
  while (!LoRa.begin(866E6)) {
    Serial.println(".");
    delay(500);
  }
   // Change sync word (0xF3) to match the receiver
  // The sync word assures you don't get LoRa messages from other LoRa transceivers
  // ranges from 0-0xFF
  LoRa.setSyncWord(0xF3);
  Serial.println("LoRa Initializing OK!");
}

void loop() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  Serial.print("Sending packet: ");
  display.print("Sending packet: ");
  Serial.println(counter);
  display.println(counter);
  display.display();

  //Send LoRa packet to receiver
  LoRa.beginPacket();
  LoRa.print("hello ");
  LoRa.print(counter);
  LoRa.endPacket();

  counter++;

  delay(1000);
}
