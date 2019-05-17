// Quelle: https://www.hackster.io/gatoninja236/neomatrix-arduino-pong-fd1ede

#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>

#define PIN 6

int xMatrix = 12;
int yMatrix = 12;

Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(xMatrix, yMatrix, PIN,
  NEO_MATRIX_BOTTOM     + NEO_MATRIX_LEFT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB            + NEO_KHZ800);

// variables for the position of the ball and paddle
int paddleX = 0;
int paddleY = 0;
int oldPaddleX, oldPaddleY;
int ballDirectionX = 1;
int ballDirectionY = 1;
int score = 0;

int ballSpeed = 10; // lower numbers are faster
int oldTime;
int newTime;

int ballX, ballY, oldBallX, oldBallY;

void setup() {
  matrix.setBrightness(100);
  Serial.begin(9600);
  // initialize the display
  matrix.begin();
  // black background
  matrix.fillScreen(0);
  matrix.setTextColor(matrix.Color(0,255,0));
  matrix.print("GO");
  matrix.show();
  delay(2000);
  matrix.fillScreen(0);
  matrix.show();
}

void loop() {

  // save the width and height of the screen
  int myWidth = matrix.width();
  int myHeight = matrix.height();

  // map the paddle's location to the position of the potentiometers
  paddleX = map(analogRead(A0), 0, 1023, 0, xMatrix -1);
  paddleY = yMatrix -1;
  
  // set the fill color to black and erase the previous
  // position of the paddle if different from present

  if (oldPaddleX != paddleX || oldPaddleY != paddleY) {
    matrix.fillRect(oldPaddleX, oldPaddleY, 4, 1,matrix.Color(0,0,0));
  }

  // draw the paddle on screen, save the current position
  // as the previous.

  matrix.fillRect(paddleX, paddleY, 4, 1,matrix.Color(0,0,255));

  oldPaddleX = paddleX;
  oldPaddleY = paddleY;
  matrix.show();

  // update the ball's position and draw it on screen
  // das geht besser
//  if (millis() % ballSpeed < 2) {
//    moveBall();
//  }

  newTime = millis();
  if (newTime >= oldTime + ballSpeed){
    oldTime = newTime;
    moveBall();
  }
  
  matrix.show();
  if(ballY > 11 && (millis() > 10000)){
    score += 1;
    matrix.fillScreen(0);
    matrix.setTextColor(matrix.Color(255,0,0));
    matrix.setCursor(0,2);
    matrix.print(String(score));
    matrix.show();
    delay(4000);
    ballX = random(3,7);
    ballY = random(1,1);
    matrix.fillScreen(0);
    matrix.show();
    delay(1000);
  }
  delay(5);
}

// this function determines the ball's position on screen
void moveBall() {
  // if the ball goes offscreen, reverse the direction:
  if (ballX > matrix.width() -1 || ballX < 0) {
    ballDirectionX = -ballDirectionX;
  }

  if (ballY > matrix.height() || ballY < 0) {
    ballDirectionY = -ballDirectionY;
  }

  // check if the ball and the paddle occupy the same space on screen
  if (inPaddle(ballX, ballY, paddleX, paddleY, 4, 1)) {
    if(ballX == paddleX && ballY == paddleY){
    ballDirectionX = -ballDirectionX;
    ballDirectionY = -ballDirectionY;
    }
    else if(ballX == paddleX + 1 && ballY == paddleY){
      ballDirectionX = ballDirectionX;
      ballDirectionY = -ballDirectionY;
    }
    else if(ballX == paddleX + 2 && ballY == paddleY){
      ballDirectionX = -ballDirectionX;
      ballDirectionY = -ballDirectionY;
    }
    else if(ballX == paddleX + 3 && ballY == paddleY){
      ballDirectionX = ballDirectionX;
      ballDirectionY = -ballDirectionY;
    }
  }

  // update the ball's position
  ballX += ballDirectionX;
  ballY += ballDirectionY;

  // erase the ball's previous position

  if (oldBallX != ballX || oldBallY != ballY) {
    matrix.drawPixel(oldBallX, oldBallY,matrix.Color(0,0,0));
  }


  // draw the ball's current position
  matrix.drawPixel(ballX, ballY,matrix.Color(150,150,0));

  oldBallX = ballX;
  oldBallY = ballY;

}

// this function checks the position of the ball
// to see if it intersects with the paddle
boolean inPaddle(int x, int y, int rectX, int rectY, int rectWidth, int rectHeight) {
  boolean result = false;

  if ((x >= rectX && x <= (rectX + rectWidth)) &&
      (y >= rectY && y <= (rectY + rectHeight))) {
    result = true;
  }

  return result;
}

