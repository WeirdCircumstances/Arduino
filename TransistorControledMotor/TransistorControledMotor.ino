int motorL = 9;
int motorR = 10;
int trigger1 = 7;
int echo1 = 6;
int trigger2 = 4;
int echo2 = 3;
long dauer1, cm1;
long dauer2, cm2;

void setup() {
  Serial.begin(9600);
  Serial.println("ready");

  pinMode(motorL, OUTPUT);
  pinMode(motorR, OUTPUT);

  pinMode(trigger1, OUTPUT); 
  pinMode(echo1, INPUT);
  
  pinMode(trigger2, OUTPUT); 
  pinMode(echo2, INPUT);
}


void loop() {

// Ultraschall1:
  digitalWrite(trigger1, LOW); 
  delay(5); 
  digitalWrite(trigger1, HIGH); 
  delay(10);
  digitalWrite(trigger1, LOW);
  dauer1 = pulseIn(echo1, HIGH); 
  cm1 = (dauer1 / 2) * 0.03432;

  // Ultraschall1:
  digitalWrite(trigger2, LOW); 
  delay(5); 
  digitalWrite(trigger2, HIGH); 
  delay(10);
  digitalWrite(trigger2, LOW);
  dauer2 = pulseIn(echo2, HIGH); 
  cm2 = (dauer2 / 2) * 0.03432; 

// Geradeaus fahren:
  analogWrite(motorL, 124); //maximal geht es bis 255
  analogWrite(motorR, 150);  

//Abbiegen:
  if(cm1 <= 50 || cm2 <= 50){
    analogWrite(motorL, 0);
    analogWrite(motorR, 124);
  }

// Stabilität:
  delay(1);
}
