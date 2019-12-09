
int latchPin = 6; //alt 5
int clockPin = 9; //alt 6
int dataPin = 7;  //alt 4
int outputEnablePin = 5; // alt 3
int ledoff = 13;
//int soundpin = 12;
//int photopin = 0;

/*int numTones = 10;
int tones[] = {261, 277, 294, 311, 330, 349, 370, 392, 415, 440};
//            mid C  C#   D    D#   E    F    F#   G    G#   A
*/
byte leds = 0;

void setup() 
{
  pinMode(latchPin, OUTPUT);
  pinMode(dataPin, OUTPUT);  
  pinMode(clockPin, OUTPUT);
//  pinMode(outputEnablePin, OUTPUT); 
  pinMode(ledoff, OUTPUT);

/*
  pinMode(soundpin, OUTPUT);
    for (int i = 0; i < numTones; i++)
    {
      tone(soundpin, tones[i]);
      delay(500);
    }
      noTone(soundpin);
*/
}


void loop() 
{
  // Teremin
  /*
  int reading = analogRead(photopin);
  int pitch = 200 + reading / 4;
  tone(soundpin, pitch);
  */
  //
  
  bitClear(ledoff,0);
  setBrightness(0);                       // Helligkeit 0
  leds = 0;                               // Start bei Lampe 0
  updateShiftRegister();                  // gibt die Änderung an das Register
  //delay(100);
  for (int i = 0; i < 8; i++)             //Zählt die LEDs durch
  {
    bitSet(leds, i);                      //setzt diese LED fest
    updateShiftRegister();                //teilt den Register die Änderung mit
      for (byte b = 0; b < 255; b++)      //Fkt. stellt Helligkeit hoch
      {
      setBrightness(b);                     
      delayMicroseconds(500);
      }                 
      for (byte b = 255; b > 0; b--)        //Fkt. stellt Helligkeit wieder runter
      {
      setBrightness(b);
      delayMicroseconds(500);
      }
      
    bitClear(leds,i);                     //schaltet Lampe wieder aus, entfernt sie aus Register
    delay(100);
  }

    for (int i = 6; i > 0; i--)             //Zählt die LEDs durch
  {
    bitSet(leds, i);                      //setzt diese LED fest
    updateShiftRegister();                //teilt den Register die Änderung mit
      for (byte b = 0; b < 255; b++)      //Fkt. stellt Helligkeit hoch
      {
      setBrightness(b);                     
      delayMicroseconds(500);
      }                 
      for (byte b = 255; b > 0; b--)        //Fkt. stellt Helligkeit wieder runter
      {
      setBrightness(b);
      delayMicroseconds(500);
      }
    bitClear(leds,i);                     //schaltet Lampe wieder aus, entfernt sie aus Register
    delay(100);
  }
 
}

void updateShiftRegister()
{
   digitalWrite(latchPin, LOW);
   shiftOut(dataPin, clockPin, LSBFIRST, leds);
   digitalWrite(latchPin, HIGH);
}

void setBrightness(byte brightness) // 0 to 255
{
  analogWrite(outputEnablePin, 255-brightness);
}

