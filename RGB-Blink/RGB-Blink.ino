int blueled = 3;
int greenled = 5;
int redled = 6;

// the setup function runs once when you press reset or power the board
void setup() {
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(redled, OUTPUT);
  pinMode(blueled, OUTPUT);
  pinMode(greenled, OUTPUT);
  
}

// the loop function runs over and over again forever
void loop() {
//  digitalWrite(redled, HIGH); 
  setColor(255, 0, 0);  // red
  delay(300);
  setColor(0, 255, 0);  // green
  delay(300);
  setColor(0, 0, 255);  // blue
  delay(300);
  setColor(255, 255, 0);  // yellow
  delay(300);  
  setColor(80, 0, 80);  // purple
  delay(300);
  setColor(0, 255, 255);  // aqua
  delay(300);
  setColor(0x4B, 0x0, 0x82);  // indigo
  delay(300);
  setColor(0x00,0x64,0x00); //blau
  delay(300);
}

void setColor(int red, int green, int blue)
{
  analogWrite(redled, red);
  analogWrite(greenled, green);
  analogWrite(blueled, blue);  
}
