

//***********************

// Send '!' token to reset the birthmark in the EEPROM so that the robot will restart to reset

// you can also activate the following modes by the 'X' token defined in src/OpenCat.h
#define VOICE                     // Petoi Grove voice module
#define PIR                       // for PIR (Passive Infrared) sensor
#define QUICK_DEMO                // for quick demo
#include "src/RoboDog.h"

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);  // USB serial
  Serial.setTimeout(SERIAL_TIMEOUT);
  // Serial1.begin(115200); //second serial port
  while (Serial.available() && Serial.read())
    ;  // empty buffer
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
#ifdef QUICK_DEMO
    if (moduleList[moduleIndex] == EXTENSION_QUICK_DEMO)
      quickDemo();
#endif
    // readHuman();
  }
  //— generate behavior by fusing all sensors and instruction
  // decision();

   //— action
   // playSound();
  reaction();

#ifdef WEB_SERVER
  WebServerLoop();  // Handle asynchronous Web requests
#endif
}

#ifdef QUICK_DEMO  // enter XQ in the serial monitor to activate the following section
int prevReading = 0;
void quickDemo() {  // this is an example that use the analog input pin ANALOG1 as a touch pad
  // if the pin is not connected to anything, the reading will be noise
  int currentReading = analogRead(ANALOG1);
  if (abs(currentReading - prevReading) > 50)  // if the reading is significantly different from the previous reading
  {
    PT("Reading on pin ANALOG1:\t");
    PTL(currentReading);
    if (currentReading < 50) {                                        // touch and hold on the A2 pin until the condition is met
      tQueue->addTask(T_BEEP, "12 4 14 4 16 2");                      // make melody
      tQueue->addTask(T_INDEXED_SEQUENTIAL_ASC, "0 30 0 -30", 1000);  // move the neck, left shoulder, right shoulder one by one
    } else if (abs(currentReading - prevReading) < 100) {
      if (strcmp(lastCmd, "sit"))
        tQueue->addTask(T_SKILL, "sit", 1000);  // make the robot sit. more tokens are defined in OpenCat.h
    } else {
      if (strcmp(lastCmd, "up"))
        tQueue->addTask(T_SKILL, "up", 1000);  // make the robot stand up. more tokens are defined in OpenCat.h
    }
  }
  prevReading = currentReading;
}
#endif
