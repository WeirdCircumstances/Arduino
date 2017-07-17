

int led = 7;
int ledoff = 13;
int redled = 11;
int greenled = 10;
int blueled = 9;

// the setup function runs once when you press reset or power the board
void setup() {
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(led, OUTPUT);
  pinMode(ledoff, OUTPUT);
  pinMode(redled, OUTPUT);
  pinMode(greenled, OUTPUT);
  pinMode(blueled, OUTPUT);
}

// the loop function runs over and over again forever
void loop() {
  digitalWrite(ledoff,LOW);  // LED 13 off
 /* digitalWrite(led, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(214);                       // wait for a second
  digitalWrite(led, LOW);    // turn the LED off by making the voltage LOW
  delay(214);                       // wait for a second
  */
  setColor(255, 0, 0);  // red
  delay(1000);
  setColor(0, 255, 0);  // green
  delay(1000);
  setColor(0, 0, 255);  // blue
  delay(1000);
  setColor(255, 255, 0);  // yellow
  delay(1000);  
  setColor(80, 0, 80);  // purple
  delay(1000);
  setColor(0, 255, 255);  // aqua
  delay(1000);
  setColor(0x4B, 0x0, 0x82);  // indigo
  delay(1000);
  setColor(0x00,0x64,0x00);
  delay(1000);
}

void setColor(int red, int green, int blue)
{
  analogWrite(redled, red);
  analogWrite(greenled, green);
  analogWrite(blueled, blue);  
}
