int trigger = 7;
int echo = 6; 
long dauer, cm;


void setup()
{
  Serial.begin (9600); 
  pinMode(trigger, OUTPUT); 
  pinMode(echo, INPUT); 
}
void loop()
{
  digitalWrite(trigger, LOW); 
  delay(5); 
  digitalWrite(trigger, HIGH); 
  delay(10);
  digitalWrite(trigger, LOW);
  dauer = pulseIn(echo, HIGH); 
  cm = (dauer / 2) * 0.03432; 

}
