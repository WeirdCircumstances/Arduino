#include <OneWire.h>
#include <Wire.h>

OneWire  ds(10);

void setup() {
  Serial.begin(9600);

}

void loop() {
  byte addr[8];
  ds.search(addr); // address on 1wire bus
  
  if (addr[0] == DS18B20) // check we are really using a DS18B20
  {
  ds.reset(); // rest 1-Wire
  ds.select(addr); // select DS18B20
  
  ds.write(0x4E); // write on scratchPad
  ds.write(0x00); // User byte 0 - Unused
  ds.write(0x00); // User byte 1 - Unused
  ds.write(0x7F); // set up en 12 bits (0x7F)
  
  ds.reset(); // reset 1-Wire
  }

 Serial.print(addr);
}
