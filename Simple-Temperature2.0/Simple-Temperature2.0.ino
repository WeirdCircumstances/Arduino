// Die Adresse des Sensors ist hardcoded. Sie kann mit "OneWire-GetAdress" ermittelt werden.
// Es sind 8 Felder, jedes davon beginnt mit "0x"


#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
//#include <Adafruit_SSD1306.h>
#include <U8x8lib.h>

U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(SCL, SDA, U8X8_PIN_NONE);

// Create a Onewire Referenca and assign it to pin 10 on your Arduino
OneWire oneWire(10);

// declare as sensor referenec by passing oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

// declare your device address
// YOUR ADDRESS GOES HERE!!!!
DeviceAddress tempSensor = {0x28, 0xFF, 0x4B, 0x23, 0x71, 0x17, 0x3, 0xD};


// A Variable to hold the temperature you retrieve
float celsius;
bool ping = true;
int x = 0;

void setup(void)
{
  u8x8.begin();
  
  // start serial port
  Serial.begin(9600);
  //display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  
  // set the resolution to 9 bit - Valid values are 9, 10, or 11 bit. Or 12
 // sensors.setResolution(tempSensor, 11);
 sensors.setResolution(tempSensor,12);
  
  // confirm that we set that resolution by asking the DS18B20 to repeat it back
  Serial.print("Sensor Resolution: ");
  Serial.println(sensors.getResolution(tempSensor), DEC);
  Serial.println();
  
// welcome screen
//  welcome();

}



void loop(void)
{ 
  // Tell the Sensor to Measure and Remember the Temperature it Measured
  sensors.requestTemperaturesByAddress(tempSensor); // Send the command to get temperatures
 
  // Get the temperature that you told the sensor to measure
  celsius = sensors.getTempC(tempSensor);
  
//  Serial.print("Celsius: ");
//  Serial.println(celsius,4);  // The four just increases the resolution that is printed

/*
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.println("Celsius:");
  display.println();
  display.setCursor(0,20);
  display.setTextSize(4);
  display.print(celsius);
  */
  
  u8x8.setFont(u8x8_font_amstrad_cpc_extended_f);    
  u8x8.clear();
  u8x8.noInverse();
  u8x8.setCursor(0,1);
  u8x8.clearDisplay();
  u8x8.setCursor(0,0);
//  display.setTextColor(WHITE);
//  display.setTextSize(1);
  u8x8.println("Celsius:");
  u8x8.println();
  u8x8.setCursor(0,20);
//  u8x8.setTextSize(4);
  u8x8.print(celsius,10);
  
  if(ping == true)
  x++;
  if(x >= 122)
  ping = false;
  if(ping == false)
  x--;
  if(x <= 0)
  ping = true;
  
//  display.fillCircle(3 + x, 60, 2, WHITE);
  
//  display.display();
//delay(10);
  
}

/*
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
*/
