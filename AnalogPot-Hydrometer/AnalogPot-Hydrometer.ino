int potPin = 0;
int latchPin = 5;
int clockPin = 6;
int dataPin = 4;
int outputEnablePin = 3;
int ledoff = 13;

int leds = 0;

void setup()
{
  pinMode(latchPin, OUTPUT);
  pinMode(dataPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(ledoff, OUTPUT);
  Serial.begin(9600);
}

void loop()
{
  //digitalWrite(ledoff,LOW);
  setBrightness(10);
  int reading  = analogRead(potPin);
  //Serial.println(reading); //seriellen Anschluss auslesen
  //delay(1000);
  delay(600000); // in ms - 1x pro 10 min messen
  int numLEDSLit = (1023 - (reading)) / 128; //1023 / 9 //Nummer hinter reading anpassen für bessere Darstellung
  leds = 0;
  for (int i = 0; i < numLEDSLit; i++)
  {
    bitSet(leds, i);
  }
  updateShiftRegister();
}

void updateShiftRegister()
{
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, LSBFIRST, leds);
  digitalWrite(latchPin, HIGH);
}

void setBrightness(byte brightness) // 0 to 255
{
  analogWrite(outputEnablePin, 255 - brightness);
}
