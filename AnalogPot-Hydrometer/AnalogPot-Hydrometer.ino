int potPin = 0;
int latchPin = 6; //alt 5
int clockPin = 9; //alt 6
int dataPin = 7;  //alt 4
int outputEnablePin = 5; // alt 3
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
  setBrightness(1);
  int reading  = analogRead(potPin);
  Serial.println(reading); //seriellen Anschluss auslesen
  int numLEDSLit = (1023 - (reading)) / 127; //1023 / 9 //Nummer hinter reading anpassen für bessere Darstellung
  leds = 0;
  for (int i = 0; i < numLEDSLit; i++)
  {
    bitSet(leds, i);
  }
  updateShiftRegister();

  delay(1000);
//  delay(3600000); // in ms - 1x pro 60 min messen
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
