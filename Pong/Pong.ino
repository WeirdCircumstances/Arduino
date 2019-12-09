#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// für den NANO in die Wire Bibliothek gehen und den clockspeed auf 100 mHz einstellen!!!
// (eine 100000 statt 400000 bei clock speed)

#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);

const int setLeft = 7;
const int setRight = 6;

// define pots
int leftButton;
int rightButton;
//define initial left/right, up/down
int i;
int j;
// x and y values
float x;
float y;
// does somebody got a point?
bool Point = true;
// asking for touching borders
bool maxX = false;
bool maxY = false;
// count points here
int player1;
int player2;
// speed ball up
unsigned long oldtime1;
float a;
float velocity;
float highest_velocity;
//CPUvsCPU
int noLeftPlayer;
int noRightPlayer;
int b;
unsigned long oldtime2;
bool autoplay = false;
bool pong = false;      // invert display after each match
char funnywinner[9];

void setup() {
  Serial.begin(9600);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  pinMode(setLeft, INPUT);
  pinMode(setRight, INPUT);

  // get some randomness
  randomSeed(analogRead(0));
  // giving a welcome screen
welcome();
}

void loop() {

  if (Point) {

    // i loads x, j loads y -> define start direction of ball
    // random generator
    i = random(2);
    j = random(2);

    if (i == 1) {
      maxX = !maxX;
    }
    if (j == 1) {
      maxY = !maxY;
    }
    x = 0;
    y = random(-30, 30);
    a = 1;
    Point = false;
  }

  //reads input
  leftButton = analogRead(setLeft);
  rightButton = analogRead(setRight);

  display.clearDisplay();

  if(pong) display.invertDisplay(true);
  else display.invertDisplay(false);

  // show middle line and points
  display.drawLine(64, 0, 64, 64, WHITE);
  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setCursor(20, 0);
  display.print(player1);
  display.setCursor(96, 0);
  display.print(player2);

  // draw paddle
  if (autoplay == false) {
    display.drawLine(0, 44 - (leftButton / 23), 0, 64 - (leftButton / 23), WHITE);
    display.drawLine(127, 44 - (rightButton / 23), 127, 64 - (rightButton / 23), WHITE);
  }

  // draw ball
  display.drawCircle(64 + x, 32 + y, 2, WHITE);

  if (autoplay == false) {
    // calculate paddle collision
    // && define x values
    // take initial position and goes from there
    // x and y taking numbers beginning at 0! Attention by calculating paddle collision!
    if (x >= 61) {
      if (((y + 32 >= (44 - (rightButton / 23))) && (y + 32 <= (64 - (rightButton / 23))))) {
        maxX = true;
      }
      else {
        player1++;
        Point = true;
      }
    }

    if (x <= -62) {
      if (((y + 32 >= (44 - (leftButton / 23))) && (y + 32 <= (64 - (leftButton / 23))))) {
        maxX = false;
      }
      else {
        player2++;
        Point = true;
      }
    }
  }

  if (autoplay == true) {
    // define x values
    if (x >= 62) {
      maxX = true;
    }
    if (x <= -62) {
      maxX = false;
    }
  }

  // define y values
  if (y >= 29) {
    maxY = true;
    i = -1;
  }
  if (y <= -30) {
    maxY = false;
    i = 1;
  }

  // define what happens, when touching a border
  if (maxX) {
    x = x - velocity;
  }
  if (!maxX) {
    x = x + velocity;
  }
  if (maxY) {
    y = y - velocity;
  }
  if (!maxY) {
    y = y + velocity;
  }

  // show who is the winner
  if (player1 == 10) {
    funnynames();
    for (i = 0; i < 64; i++) {
      display.clearDisplay();
      display.setTextSize(2);
      //display.fillRect(70, 0+i, 50, 15+i, BLACK);
      display.setCursor(0, -10 + i);
      display.println(funnywinner);
      display.setCursor(50, 10 + i);
      display.println("<- ");
      display.setTextSize(1);
      //    display.setCursor(0, 30+i);
      //    display.print("fastest ball:");
      //    display.print(highest_velocity);
      display.display();
      delay(100);
    }
    Point = true;
    player1 = 0;
    player2 = 0;
    highest_velocity = 0;
   // pong = !pong;
  }
  if (player2 == 10) {
    funnynames();
    display.setTextSize(2);
    for (i = 0; i < 64; i++) {
      display.clearDisplay();
      display.setTextSize(2);
      //display.fillRect(70, 0+i, 50, 15+i, BLACK);
      display.setCursor(0, -10 + i);
      display.println(funnywinner);
      display.setCursor(50, 10 + i);
      display.println(" ->");
      display.setTextSize(1);
      //    display.setCursor(0, 30+i);
      //    display.print("fastest ball: ");
      //    display.print(highest_velocity);
      display.display();
      delay(100);
    }
    Point = true;
    player1 = 0;
    player2 = 0;
    highest_velocity = 0;
  //  pong = !pong;
  }

  // print all what happens here on display each cycle

  /*
    //debugging
    display.setTextSize(1);
    display.setCursor(16,56);
    display.print(b);
    display.setCursor(16+64,56);
    display.print(velocity);
  */

  speedball();
  CPUvsCPU();

  display.display();
}

void welcome() {
  display.clearDisplay();
  display.setTextSize(4);
  display.setTextColor(WHITE);
  display.setCursor(7, 20);
  display.print("PONG!");
  for (i = 0; i < 4; i++) {
    display.invertDisplay(true);
    display.display();
    delay(500);
    display.invertDisplay(false);
    delay(500);
    display.display();
  }
}

void speedball() {
  unsigned long newtime = millis();
  if (newtime >= oldtime1 + 100) {
    a++;
    velocity = 1 + (a / 100);
    oldtime1 = newtime;
    if (velocity > highest_velocity) {
      highest_velocity = velocity;
    }
  }
}

void funnynames() {
  int z = random(10);
  switch (z) {
    case 0: strcpy(funnywinner, "Pro Gamer\!"); break;
    case 1: strcpy(funnywinner, "  Godlike\!"); break;
    case 2: strcpy(funnywinner, " Ultimate\!"); break;
    case 3: strcpy(funnywinner, "  Master\!"); break;
    case 4: strcpy(funnywinner, "  Killer\!"); break;
    case 5: strcpy(funnywinner, "  Monster\!"); break;
    case 6: strcpy(funnywinner, "Merciless\!"); break;
    case 7: strcpy(funnywinner, "  Winner\!"); break;
    case 8: strcpy(funnywinner, "Immortable"); break;
    case 9: strcpy(funnywinner, "Unbeatable"); break;
  }
}

void CPUvsCPU() {
  unsigned long newtime = millis();
  if (newtime - oldtime2 > 1000) {
    oldtime2 = newtime;
    b++;
  }
  if (leftButton / 23 != noLeftPlayer) {
    b = 0;
    noLeftPlayer = leftButton / 23;
    autoplay = false;
  }
  if (rightButton / 23 != noRightPlayer) {
    b = 0;
    noRightPlayer = rightButton / 23;
    autoplay = false;
  }

  // giving a point to a random player or keep playing
  if (b >= 10) {
    if (random(5,1000) < velocity && (x <= -62 || x >= 62)) {
      if (random(2) == 1) {
        player1++;
        Point = true;
      }
      else {
        player2++;
        Point = true;
      }
    }
    else {
      autoplay = true;
      if ( maxX ) {
        display.drawLine(0, 20 + y * 0.7 , 0, 40 + y * 0.7, WHITE);
      }
      if (!maxX ) {
        display.drawLine(127, 20 + y * 0.7, 127, 40 + y * 0.7, WHITE);
      }
    }
    display.fillRect(39, 27, 51, 11, WHITE);
    display.setTextSize(1);
    display.setTextColor(BLACK);
    display.setCursor(41, 29);
    display.print("CPUvsCPU");
  }
}

