#include <Servo.h>

Servo servo1;  // create servo object to control a servo
Servo servo2;
Servo servo3;
Servo servo4;
Servo servo5;

// twelve servo objects can be created on most boards

int pos = 0;    // variable to store the servo position


void setup() {
  servo1.attach(2);  // attaches the servo on pin 9 to the servo object
  servo2.attach(3);
  servo3.attach(4);
  servo3.attach(5);
  servo3.attach(6);
}

void loop() {
  
  for (pos = 0; pos <= 180; pos += 1) { // goes from 0 degrees to 180 degrees
    // in steps of 1 degree
    servo1.write(pos);              // tell servo to go to position in variable 'pos'
    delayMicroseconds(3500);                       // waits 15ms for the servo to reach the position
  }
  for (pos = 180; pos >= 0; pos -= 1) { // goes from 180 degrees to 0 degrees
    servo1.write(pos);              // tell servo to go to position in variable 'pos'
    delayMicroseconds(3500);                        // waits 15ms for the servo to reach the position
  }

  for (pos = 0; pos <= 180; pos += 1) { // goes from 0 degrees to 180 degrees
    // in steps of 1 degree
    servo2.write(pos);              // tell servo to go to position in variable 'pos'
    delayMicroseconds(3500);                       // waits 15ms for the servo to reach the position
  }
  for (pos = 180; pos >= 0; pos -= 1) { // goes from 180 degrees to 0 degrees
    servo2.write(pos);              // tell servo to go to position in variable 'pos'
    delayMicroseconds(3500);                        // waits 15ms for the servo to reach the position
  }

    for (pos = 0; pos <= 180; pos += 1) { // goes from 0 degrees to 180 degrees
    // in steps of 1 degree
    servo3.write(pos);              // tell servo to go to position in variable 'pos'
    delayMicroseconds(3500);                       // waits 15ms for the servo to reach the position
  }
  for (pos = 180; pos >= 0; pos -= 1) { // goes from 180 degrees to 0 degrees
    servo3.write(pos);              // tell servo to go to position in variable 'pos'
    delayMicroseconds(3500);                        // waits 15ms for the servo to reach the position
  }

    for (pos = 0; pos <= 180; pos += 1) { // goes from 0 degrees to 180 degrees
    // in steps of 1 degree
    servo4.write(pos);              // tell servo to go to position in variable 'pos'
    delayMicroseconds(3500);                       // waits 15ms for the servo to reach the position
  }
  
  for (pos = 180; pos >= 0; pos -= 1) { // goes from 180 degrees to 0 degrees
    servo4.write(pos);              // tell servo to go to position in variable 'pos'
    delayMicroseconds(3500);                        // waits 15ms for the servo to reach the position
  }

    for (pos = 0; pos <= 180; pos += 1) { // goes from 0 degrees to 180 degrees
    // in steps of 1 degree
    servo5.write(pos);              // tell servo to go to position in variable 'pos'
    delayMicroseconds(3500);                       // waits 15ms for the servo to reach the position
  }
  
  for (pos = 180; pos >= 0; pos -= 1) { // goes from 180 degrees to 0 degrees
    servo5.write(pos);              // tell servo to go to position in variable 'pos'
    delayMicroseconds(3500);                        // waits 15ms for the servo to reach the position
  }

}
