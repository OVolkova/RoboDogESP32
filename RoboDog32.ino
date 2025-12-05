// modify the model and board definitions
//***********************

#define VOICE  // Petoi Grove voice module
#define PIR    // for PIR (Passive Infrared) sensor

#include "src/RoboDog.h"

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);  // USB serial
  Serial.setTimeout(SERIAL_TIMEOUT);
  // Serial1.begin(115200); //second serial port
  while (Serial.available() && Serial.read());  // empty buffer
  initRobot();
}

void loop() {
  //  //— read environment sensors (low level)
  readEnvironment();  // update the gyro data
  //  //— special behaviors based on sensor events
  dealWithExceptions();  // low battery, fall over, lifted, etc.
  if (!tQueue->cleared()) {
    tQueue->popTask();
  } else {
    readSignal();
  }
  // — generate behavior
  reaction();

#ifdef WEB_SERVER
  WebServerLoop();  // Handle asynchronous Web requests
#endif
}
