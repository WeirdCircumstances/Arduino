#define rot 4
#define blau_1 2
#define gruen 8
#define blau_2 6
#define taste_R 3
#define taste_G 5
#define taste_B 13
#define taste_A 12

int Rot_An = 0;
int Gruen_An = 0;
int Blau_An = 0;
int Alle_An = 0;
bool lampRed = false;
bool pushButton = true;

void setup() {
  // put your setup code here, to run once:
  pinMode(rot, OUTPUT);
  pinMode(blau_1, OUTPUT);
  pinMode(gruen, OUTPUT);
  pinMode(blau_2, OUTPUT);
  pinMode(taste_R, INPUT);
  pinMode(taste_G, INPUT);
  pinMode(taste_B, INPUT);
  pinMode(taste_A, INPUT);
}

void loop() {
  // put your main code here, to run repeatedly:
  // ###################  Taste ROT  ###########
  Rot_An = digitalRead(taste_R);
  digitalWrite(rot, LOW);
  if (Rot_An == HIGH && pushButton)
  {
    boolFarbe();
    pushButton = false;
  }
  if (Rot_An == LOW){
    pushButton = true;
  }

  // ####################  Taste Gruen  #########
  Gruen_An = digitalRead(taste_G);
  digitalWrite(gruen, LOW);
  if (Gruen_An == HIGH)
  {
    digitalWrite(gruen, HIGH);
  }
  // ####################  Taste Blau  ##########
  Blau_An = digitalRead(taste_B);
  digitalWrite(blau_1, LOW);
  digitalWrite(blau_2, LOW);
  if (Blau_An == HIGH)
  {
    digitalWrite(blau_1, HIGH);
    digitalWrite(blau_2, HIGH);
  }
  // ########################  Taste ALLE  ##########
  Alle_An = digitalRead(taste_A);
  if (Alle_An == HIGH)
  {
    digitalWrite(rot, HIGH);
    digitalWrite(blau_1, HIGH);
    digitalWrite(gruen, HIGH);
    digitalWrite(blau_2, HIGH);
  }

  setRed();
}

void boolFarbe(){
  lampRed = !lampRed;
}

void setRed(){
  if(lampRed){
  digitalWrite(rot, HIGH);
  }
}

