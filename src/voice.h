// Petoi Voice Command Module
// Doc: https://docs.petoi.com/extensible-modules/voice-command-module
// use the software serial port on the NyBoard to read the module. connect the module to the grove socket with pin 6
// and 7. or use the serial 2 port on the BiBoard to read the module. connect the module to the pin Tx2 and Rx2. if you
// wire the module up with the USB programmer directly, connect the module's Tx to the programmer's Rx, and Rx to Tx.
// Rongzhong Li
// Petoi LLC
// Jan 12, 2023

#define SERIAL_VOICE_BAUD_RATE 9600
#define MAX_CUSTOMIZED_CMD 10

// Speak "start learning" to record your voice commands in order. You can record up to 10 voice commands
// Speak "stop learning" to stop in the middle
// Speak one of the recorded voice commands to trigger the reaction
// Speak "clear the learning data" to delete all the recordings at once. (you cannot delete a specific recording)
// The reactions below are already defined in the program. You may use SkillComposer to design new skills then import
// them into InstinctX.h Other serial commands are also supported, such as joint movements and melody

// Say "start learning" to begin recording, up to 10 commands
// Say "stop learning" to stop entering
// Say the voice command to trigger reactions
// Say "clear data" to delete all customized voice commands (cannot delete a single voice command)
// The following behaviors are preset in the program. You can use Skill Composer to design new skills and import them into InstinctX.h
// Supports other serial commands, such as joint movements and melody

// #define VOICE_MODULE_SAMPLE
String customizedCmdList[] = {
    // "rg",
    "fl",               // learn skill with feedback servos
    "fr",               // replay skill learned with feedback servos
    "fF",               // movement follower demo with feedback servos
    "kpu1",             // single-handed pushups
    "m0 80 0 -80 0 0",  // wave head
    "kmw",              // moonwalk
    "b14,8,14,8,21,8,21,8,23,8,23,8,21,4,19,8,19,8,18,8,18,8,16,8,16,8,14,4,\
  21,8,21,8,19,8,19,8,18,8,18,8,16,4,21,8,21,8,19,8,19,8,18,8,18,8,16,4,\
  14,8,14,8,21,8,21,8,23,8,23,8,21,4,19,8,19,8,18,8,18,8,16,8,16,8,14,4",  // twinkle star
    "T",  // call the last skill data sent by the Skill Composer
    "6th",
    "7th",
    "8th",
    "9th",
    "10th"  // define up to 10 customized commands.
};
int listLength = 0;
bool enableVoiceQ = true;
void beginVoiceSerial() {
  if (!SERIAL_VOICE) {
    // PTL("Begin Voice Serial port");
    SERIAL_VOICE.begin(SERIAL_VOICE_BAUD_RATE);
    SERIAL_VOICE.setTimeout(5);
  }
  delay(20);
}

void set_voice(char* cmd) {  // send some control command directly to the module
  // XAa: switch English
  // XAb: switch Chinese
  // XAc: turn on the sound response
  // XAd: turn off the sound response
  // XAe: start learning
  // XAf: stop learning
  // XAg: clear the learning data
  if (cmd[1] == 'a' ||
      cmd[1] ==
          'b') {  // enter "XAa" in the serial monitor or add button "X65,97" in the mobile app to switch to English
    // Enter command "XAa" in the serial monitor or create button "X65,97" in the mobile app to switch to English
    defaultLan = cmd[1];
    config.putChar("defaultLan", defaultLan);
    config.putChar("currentLan", currentLan);

    PTHL("Default language: ", defaultLan == 'b' ? " Chinese" : " English");
  }
  byte c = 0;
  while (cmd[c] != '\0' && cmd[c] != '~') c++;
  cmd[c] = '\0';
  SERIAL_VOICE.print("X");
  SERIAL_VOICE.println(cmd);
  delay(10);
  if (!SERIAL_VOICE
           .available()) {  // the serial port may need to re-open for the first time after rebooting. Don't know why.
    SERIAL_VOICE.end();
    PTLF("Reopen Voice Serial port");
    beginVoiceSerial();
    delay(10);
    SERIAL_VOICE.print("X");
    SERIAL_VOICE.println(cmd);
    delay(10);
  }
  while (SERIAL_VOICE.available())  // avoid echo
    PT(char(SERIAL_VOICE.read()));
  PTL();
  if (!strcmp(cmd, "Ac"))  // enter "XAc" in the serial monitor or add button "X65,99" in the mobile app to enable voice
                           // reactions. Enter command "XAc" in the serial monitor or create button "X65,99" in the mobile app to enable voice reactions
    enableVoiceQ = true;
  else if (!strcmp(cmd,
                   "Ad"))  // enter "XAd" in the serial monitor or add button "X65,100" in the mobile app to disable
                           // voice reactions. Enter command "XAd" in the serial monitor or create button "X65,100" in the mobile app to disable voice reactions
    enableVoiceQ = false;

  printToAllPorts('X');  // the blue read runs on a separate core.
  // if the message arrives after the reaction(), it may not reply 'X' to BLE and the mobile app will keep waiting for
  // it.
  resetCmd();
}

void voiceSetup() {
  PTLF("Init voice");
  listLength = min(int(sizeof(customizedCmdList) / sizeof(customizedCmdList[0])), MAX_CUSTOMIZED_CMD);
  PTF("Number of customized voice commands on the main board: ");
  PTL(listLength);
  beginVoiceSerial();
  if (currentLan != defaultLan) {
    char temp[4] = "XA\0";
    temp[2] = defaultLan;
    SERIAL_VOICE.println(temp);
    currentLan = defaultLan;
    config.putChar("currentLan", currentLan);
  }

  SERIAL_VOICE.println("XAc");
  PTLF("Turn on the audio response");
  enableVoiceQ = true;
}
void voiceStop() {
  beginVoiceSerial();
  SERIAL_VOICE.println("XAd");
  delay(5);
  SERIAL_VOICE.end();
  PTLF("Turn off the audio response");
  enableVoiceQ = false;
}

void read_voice() {
  if (SERIAL_VOICE.available()) {
    String raw = SERIAL_VOICE.readStringUntil('\n');
#ifdef BT_CLIENT
    if (btConnected) {
      PTLF("Ignore voice for remote controllor");
      return;
    }
#endif
    PTL(raw);
    byte index = (byte)raw[2];  // interpret the 3rd byte as integer
    int shift = -1;
    if (index > 10 && index < 61) {
      if (index < 21) {  // 11 ~ 20 are customized commands, and their indexes should be shifted by 11
        index -= 11;
        PT(index);
        PT(' ');
        if (index < listLength) {
          raw = customizedCmdList[index];
          token = raw[0];
          shift = 1;
        } else {
          PTLF("Undefined!");
        }
      } else if (index < 61) {  // 21 ~ 60 are preset commands, and their indexes should be shifted by 21.
                                // But we don't need to use their indexes.
#ifdef VOICE_MODULE_SAMPLE
        token = T_SKILL;
        shift = 3;
#else
        token = raw[3];
        shift = 4;
#endif
      }
      if (enableVoiceQ) {
        const char* cmd = raw.c_str() + shift;
        tQueue->addTask(token, shift > 0 ? cmd : "", 2500);
        if (strlen(cmd) > 0) {
          char end = cmd[strlen(cmd) - 1];
          if (!strcmp(cmd, "bk") || !strcmp(cmd, "x") || (end >= 'A' && end <= 'Z')) { tQueue->addTask('k', "up"); }
        }
      }
    } else {
      switch (tolower(index)) {
        case 'a':  // say "Bing-bing" to switch English / Say "Bing-bing" to switch to English
        {
          PTLF("Switch English");
          currentLan = 'a';
          config.putChar("currentLan", 'a');
          if (lastToken == 'c') {  // only change the default language in calibration mode.
                                   // otherwise the language will roll back to default after reboot
            defaultLan = 'a';
            config.putChar("defaultLan", 'a');
            PTHL("Default language: ", defaultLan == 'b' ? " Chinese" : " English");
          }
          break;
        }
        case 'b':  // say "Di-di" to switch Chinese / Say "Di-di" to switch to Chinese
        {
          PTLF("Switch Chinese");
          currentLan = 'b';
          config.putChar("currentLan", 'b');
          if (lastToken == 'c') {
            defaultLan = 'b';
            config.putChar("defaultLan", 'b');
            PTHL("Default language: ", defaultLan == 'b' ? " Chinese" : " English");
          }
          break;
        }
        case 'c':  // say "play sound" to enable voice reactions / Say "play sound" to enable voice reactions
        {
          enableVoiceQ = true;
          PTLF("Turn on the audio response");
          break;
        }
        case 'd':  // say "be quiet" to disable voice reactions / Say "be quiet" to disable voice reactions
        {
          enableVoiceQ = false;
          PTLF("Turn off the audio response");
          break;
        }
        case 'e': {
          PTLF("Start learning");
          break;
        }
        case 'f': {
          PTLF("Stop learning");
          break;
        }
        case 'g': {
          PTLF("Delete all learning data!");
          break;
        }
      }
    }
  }
}
