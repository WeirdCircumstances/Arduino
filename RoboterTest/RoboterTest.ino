int Trig = 13;
int Echo = 12;
long cm, dauer; 
int MotorA = 10;
int MotorB = 11;
int zufall;

void setup() {
  pinMode(MotorA, OUTPUT);
  pinMode(MotorB, OUTPUT);
  pinMode(Trig, OUTPUT);
  pinMode(Echo, INPUT);
}

void loop() {
  zufall = random(2);
  digitalWrite(Trig, LOW);
  delay(5);
  digitalWrite(Trig, HIGH);
  delay(10);
  digitalWrite(Trig, LOW);
  dauer = pulseIn(Echo, HIGH);
  cm = (dauer / 2) * 0.03432;

  if(cm <= 30) {
    if(zufall == 0){
      digitalWrite(MotorA, LOW);
      digitalWrite(MotorB, HIGH);     
      delay(1000); 
    }
    if(zufall == 1){
        digitalWrite(MotorA, HIGH);
        digitalWrite(MotorB, LOW);     
        delay(1000); 
    }
  }
  else{
    digitalWrite(MotorA, HIGH);
    digitalWrite(MotorB, HIGH);   
    delay(100);
  }
                      
}
