 #include <OneWire.h>

// DS18S20 Temperaturchip i/o
OneWire ds(10);  // an pin 10

void setup(void) {
  // inputs/outputs initialisieren
  // seriellen port starten
  Serial.begin(9600);
}

void loop(void) {
  byte i;
  byte present = 0;
  byte data[12];
  byte addr[8];

  if ( !ds.search(addr)) {
      Serial.print("Keine weiteren Addressen.\n");
      ds.reset_search();
      return;
  }

  Serial.print("R=");
  for( i = 0; i < 8; i++) {
    Serial.print(addr[i], HEX);
    Serial.print(" ");
  }

  if ( OneWire::crc8( addr, 7) != addr[7]) {
      Serial.print("CRC nicht gÃ¼ltig!\n");
      return;
  }

  if ( addr[0] == 0x10) {
      Serial.print("GerÃ¤t ist aus der DS18S20 Familie.\n");
  }
  else if ( addr[0] == 0x28) {
      Serial.print("GerÃ¤t ist aus der DS18B20 Familie.\n");
  }
  else {
      Serial.print("GerÃ¤tefamilie nicht erkannt : 0x");
      Serial.println(addr[0],HEX);
      return;
  }

  ds.reset();
  ds.select(addr);
  ds.write(0x44,1);         // start Konvertierung, mit power-on am Ende

  delay(1000);     // 750ms sollten ausreichen
  // man sollte ein ds.depower() hier machen, aber ein reset tut das auch

  present = ds.reset();
  ds.select(addr);    
  ds.write(0xBE);         // Wert lesen

  Serial.print("P=");
  Serial.print(present,HEX);
  Serial.print(" ");
  for ( i = 0; i < 9; i++) {           // 9 bytes
    data[i] = ds.read();
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.print(" CRC=");
  Serial.print( OneWire::crc8( data, 8), HEX);
  Serial.println();
}
