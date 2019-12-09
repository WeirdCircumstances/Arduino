#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "images.h"


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

int seconds;
int oldtime;
String LoRaData;
int timer;

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
 
  digitalWrite(16, LOW);    // set GPIO16 low to reset OLED
  delay(50); 
  digitalWrite(16, HIGH); // while OLED is running, must set GPIO16 in high

  
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
  // try to parse packet
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    // received a packet
    Serial.print("Received packet '");

    // read packet
    while (LoRa.available()) {
      LoRaData = LoRa.readString();
      timer = 0;  
    }
  }

  seconds = millis();
  if(seconds >= oldtime + 1000){
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextColor(WHITE);
    display.println("Received packet:");
    display.println(LoRaData);
    display.print("with RSSI: ");
    display.println(LoRa.packetRssi());  
    display.print(timer);
    display.print(" seconds ago");
    display.display();
    oldtime = seconds;
    timer++;
  }  
}
