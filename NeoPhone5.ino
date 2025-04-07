/* 
  Rewritten Neotropolis Telephone v5 using a state machine
  by Phil Montgomery (phil@majura.com)
  Last Updated 3/6/2025
  
  Changes implemented:
  - complete rewrite as state machine and to use AdaFruit AudioFX sound board with files being able to be added and numbers read from filenames
  - The audioFX is timing sensitive when sending commands, so we need to use delays to prevent problems. Didn't add this to the triggerSoundUART() function as sometimes we want to process the delay after other things.
  - Added additional comments and refactored repeated code for clarity
  - The UNO was running out of dynamic memory so converted the code to eliminate strings and store as integers where possible. Please use a modern controller with much more memory
  - If not using an AVR based controller you'll need to stop the use of PROGMEM and just use normal memory (assuming you have more than 2kb)
  - ALL FILENAMES MUCH BE INTEGERS.
  - keyfob files A=1, B=2, C=3, D=4.  So 11.WAV = AA (two presses of button A)
  - added a screensaver to prevent any sort of LCD burn-in - looks cool too

  This project is licensed under the MIT License.
*/

//#define DEBUG  // uncomment this line to enable debugging

#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include "Adafruit_Soundboard.h"
#include <avr/pgmspace.h>  // Required to use PROGMEM

// ----- Pin and Global Variable Setup -----
// Audio board pins
const uint8_t SFX_TX = 12;       // AudioFX TX pin
const uint8_t SFX_RX = 11;       // AudioFX RX pin
const uint8_t SFX_RST = 10;      // AudioFX reset pin
const uint8_t SFX_ACT = 13;      // AudioFX ACT pin
const uint8_t ReceiverHook = 9;  //Phone receiver hook

// Define a struct to hold pin and letter mapping
struct KeyFobButton {
  uint8_t pin;     // Pin number
  uint8_t number;  // number for filename
  char letter;     // Assigned letter
};

// Define keyfob buttons with pin and letter assignments
const uint8_t numButtons = 4;  // Number of keyfob buttons
KeyFobButton keyfobButtons[numButtons] = {
  { 16, 1, 'A' },  // Pin 16 corresponds to 'A' & '1'
  { 15, 2, 'B' },  // Pin 15 corresponds to 'B' & '2'
  { 14, 3, 'C' },  // Pin 14 corresponds to 'C' & '3'
  { 17, 4, 'D' }   // Pin 17 corresponds to 'D' & '4'
};

// Keypad definitions
const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  { '1', '2', '3' },
  { '4', '5', '6' },
  { '7', '8', '9' },
  { '*', '0', '#' }
};
byte rowPins[ROWS] = { 5, 4, 3, 2 };
byte colPins[COLS] = { 8, 7, 6 };

// Audio file names used with triggerSoundUART. Note that .WAV theoretically play quicker as they are uncompressed
uint8_t dialTone = 01;      //name on soundboard is "1.OGG or 1.WAV"
uint8_t buttonPress = 02;   //name on soundboard is "2.WAV or 2.OGG"
uint8_t ringingTone = 03;   //name on soundboard is "3.OGG or 3.WAV"
uint8_t callFinished = 04;  ////name on soundboard is "4.OGG or 4.WAV"

// Timing constants
const uint16_t RingDelay = 1000;
const uint16_t ringTimeout = 60000;
const uint16_t sfxDelay = 400;  //The delay between sending commands to the AudioFX
const uint16_t messageWait = 1000;
const unsigned long debounceDelay = 2;           //Make this as low as possible to avoid lag
const unsigned long screensaverTimeout = 60000;  // 60 seconds screensaver timeout
unsigned long lastActivityTime = 0;              // Tracks last user action

//For screen saver
bool screensaverActive = false;
unsigned long screensaverLastUpdate = 0;
int screensaverPosition = 0;
bool screensaverDirection = true;  // true = moving right, false = moving left


// LCD messages

const char StartMessage1[] PROGMEM = "Neo Coms Network";
const char StartMessage2[] PROGMEM = "  Lift Handset";
const char entryMessage1[] PROGMEM = "  Enter Number";
const char callingMessage[] PROGMEM = "    Calling";
const char callComplete1[] PROGMEM = "  Call Complete";
const char replaceHandset[] PROGMEM = "Replace Handset";
const char connectingMessage[] PROGMEM = "  Connecting";
const char connectedMessage[] PROGMEM = "   Connected";
const char invalidNumber[] PROGMEM = " Invalid Number";
const char incomingCallMsg[] PROGMEM = " Incoming Call";
const char noAnswer[] PROGMEM = "   No Answer";
const char answer[] PROGMEM = "   Answer Me!";
const char audioFX[] PROGMEM = "AudioFX";
const char notFound[] PROGMEM = "Not Found";
const char blank[] PROGMEM = "                ";
const char ringError[] PROGMEM = "RingTone not found";
const char errorMsg[] PROGMEM = "Error: ";
const char noFiles[] PROGMEM = "No Sound Files!";

// Hardware objects
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
LiquidCrystal_I2C lcd(0x27, 16, 2);
SoftwareSerial ss(SFX_TX, SFX_RX);
Adafruit_Soundboard sfx(&ss, NULL, SFX_RST);

// Audio file lookup arrays
struct AudioFile {
  long fileID;       // Integer representation of filename
  uint8_t trackNum;  // Corresponding track number on AudioFX
};
#define MAX_TRACKS 30
AudioFile audioFiles[MAX_TRACKS];  // Array of struct to store file mappings
uint8_t totalFiles = 0;            // Number of files loaded

// ----- State Machine Setup -----
enum PhoneState {
  STATE_IDLE,
  STATE_DIAL_TONE,
  STATE_KEYPAD_ENTRY,
  STATE_CONNECTING,
  STATE_INCOMING_CALL,
  STATE_RESET  // New state for resetting
};

PhoneState currentState = STATE_IDLE;

//Other globals
bool startupComplete = false;

// ----- Setup Function -----
void setup() {

  pinMode(SFX_ACT, INPUT_PULLUP);
  pinMode(ReceiverHook, INPUT_PULLUP);

  for (uint8_t i = 0; i < numButtons; i++) {
    pinMode(keyfobButtons[i].pin, INPUT_PULLUP);
  }

  lcd.init();
  lcd.backlight();

#ifdef DEBUG
  Serial.begin(9600);  //For debugging - theoretically we could move the AudioFX here, but it seems to work fine on software serial.
#endif

  ss.begin(9600);      //Start the software serial
  delay(messageWait);  //Wait for serials and SFX to initialize

  if (!sfx.reset()) {
    StillLCD_P(audioFX, notFound);
    while (1)
      ;  // Halt if no sound board is found
  }

  sfx.volUp();  // Sets volume UP by 25 units
  sfx.volUp();  // Sets volume UP by 25 units - should now be almost at maximum.

  scanAudioFXStorageNew();  // Build the lookup table

#ifdef DEBUG
  Serial.println(F("Debugging is enabled"));
  printFoundFiles();  //For debugging
#endif


  delay(messageWait);
}

// ----- Main Loop: State Machine Implementation -----
void loop() {

  static uint8_t trackNum = 255;  //255 means do valid track selected/found - this is static to loop and tracks the passed track index.

  if (!startupComplete) {
    StillLCD_P(StartMessage1, StartMessage2);
    startupComplete = true;
  }

  // If screensaver is active, animate it
  if (screensaverActive) {
    runScreensaver();  // non-blocking animation
  }

  // Check if screensaver should activate
  if (!screensaverActive && (millis() - lastActivityTime > screensaverTimeout)) {
    activateScreensaver();
  }

  switch (currentState) {
    case STATE_IDLE:
      {
        if (isReceiverHookOff()) {
          currentState = STATE_DIAL_TONE;
          if (screensaverActive) {  //Turn off Screensaver
            exitScreensaver();
            StillLCD_P(StartMessage1, StartMessage2);  // Restore normal display
            delay(messageWait);
          }
        } else {
          int buttonIndex = CheckForIncomingCall();
          if (buttonIndex != -1) {  // ✅ Keyfob press detected
            trackNum = ProcessIncomingKeyFob(buttonIndex);

            if (trackNum < 255) {           // ✅ Valid incoming call
              lastActivityTime = millis();  // Reset timer on activity
              if (screensaverActive) {      //Turn off Screensaver
                exitScreensaver();
              }
              StillLCD_P(incomingCallMsg, answer);
              currentState = STATE_INCOMING_CALL;
            }
          }
        }
        break;
      }

    case STATE_DIAL_TONE:
      {
        uint8_t tempNum = findTrackNumberByFilename(dialTone);

#ifdef DEBUG
        Serial.print(F("Track number for dialtone: "));
        Serial.println(tempNum);
#endif
        triggerSoundUART(tempNum);
        delay(sfxDelay);
        currentState = STATE_KEYPAD_ENTRY;
        break;
      }

    case STATE_KEYPAD_ENTRY:
      {
#ifdef DEBUG
        Serial.println(F("In STATE_KEYPAD_ENTRY"));
#endif
        trackNum = GetKeyPadNumber();

        if (trackNum == 255) {  // Ensure invalid/hang-up cases reset
          //Serial.println(F("Invalid number or hang-up detected. Resetting."));
          currentState = STATE_RESET;
          break;
        }

        //Serial.println(F("Valid number entered, transitioning to STATE_CONNECTING."));
        currentState = STATE_CONNECTING;
        break;
      }

    case STATE_CONNECTING:
      {
#ifdef DEBUG
        Serial.print(F("Entering STATE_CONNECTING"));
#endif

        // Check if the receiver has been hung up
        if (!isReceiverHookOff()) {
          // Handset replaced, transition to reset state
          sfx.stop();
          currentState = STATE_RESET;
          break;
        }

#ifdef DEBUG
        // Dial the number entered.
        Serial.print(F("Calling dialing phone with trackNum: "));
        Serial.println(trackNum);
#endif
        dialingPhone(trackNum);
        break;
      }

    case STATE_INCOMING_CALL:
      {
#ifdef DEBUG
        Serial.println(F("In STATE_INCOMING_CALL"));
#endif

        if (trackNum < 255) {  // ✅ Ensure valid file exists
          processIncomingCall(trackNum);
          currentState = STATE_RESET;
        } else {
          //Serial.println(F("Invalid trackNum - skipping call"));
          currentState = STATE_IDLE;  // ✅ Stay idle instead of resetting
        }
        break;
      }


    case STATE_RESET:
      {
#ifdef DEBUG
        Serial.println(F("In STATE_RESET"));
#endif
        sfx.stop();

        while (isReceiverHookOff()) {
          //Serial.println(F("Waiting for handset to be placed back..."));
          delay(10);  //short delay between checks
        }
        trackNum = 255;
        startupComplete = false;
        sfx.reset();
        lastActivityTime = millis();
        currentState = STATE_IDLE;  // Transition back to idle after reset
        break;
      }
  }
}


// ----- DialingPhone -----
// Called after keypad entry to process dialing.
void dialingPhone(int tempNum) {
  if (!isReceiverHookOff()) {

#ifdef DEBUG
    Serial.println(F("Receiver is on-hook. Cancelling call."));
#endif

    sfx.stop();
    currentState = STATE_RESET;
    return;
  }

  // Display dialing message
  displayLCDTopMessage_P(callingMessage);

  // Play connecting sound
  int index = findTrackNumberByFilename(ringingTone);
  sfx.stop();
  delay(sfxDelay);
  triggerSoundUART(index);
  delay(RingDelay);
  sfx.stop();
  delay(sfxDelay);

  // Show "Connected" message
  displayLCDTopMessage_P(connectedMessage);

  // Play the call recording
  triggerSoundUART(tempNum);
  delay(sfxDelay);

  // Call Loop: Monitor handset until replaced
  checkForHangupOrEnd();  // If user hangs up, it will stop sound and reset

  // **Check if the receiver is already on-hook before playing ending sound**
  if (!isReceiverHookOff()) {
#ifdef DEBUG
    Serial.println(F("Receiver was hung up before ending sound."));
#endif
    sfx.stop();
    currentState = STATE_RESET;
    return;  // **Exit early to avoid playing callFinished sound**
  }

  // **Play ending call sound only if still off-hook**
  index = findTrackNumberByFilename(callFinished);
  triggerSoundUART(index);
  StillLCD_P(callComplete1, replaceHandset);

  // Wait for handset to be placed back
  while (isReceiverHookOff()) {
    if (!stillPlaying()) {
      triggerSoundUART(index);
      delay(sfxDelay);
    }
  }

  delay(sfxDelay);  //Make sure there is a delay before talking again to AudioFX
  sfx.stop();
  currentState = STATE_RESET;
  return;
}



// ----- GetKeyPadNumber -----
// Blocks until a 5-digit phone number is entered or the handset is replaced.

uint8_t GetKeyPadNumber() {
  char phoneNumber[6] = "     ";  // Initialize with spaces
  phoneNumber[5] = '\0';          // Null-terminate manually

  char displayBuffer[7] = "      ";  // Buffer for LCD display
  displayBuffer[6] = '\0';           // Null-terminate manually

  bool firstKeyPressed = false;
  uint8_t KeyPadCount = 0;

  displayLCDTopMessage_P(entryMessage1);  // Only update once

  uint8_t tempIndex = findTrackNumberByFilename(buttonPress);

  lcd.setCursor(0, 1);
  lcdClearLine();
  lcd.setCursor(5, 1);
  lcd.print(displayBuffer);

  while (KeyPadCount < 5) {
    if (!isReceiverHookOff()) {
      currentState = STATE_RESET;
      return 255;  // Exit on hang-up
    }

    char key = keypad.getKey();  // Use getKey() instead of getKeys()

    if (key != NO_KEY) {
#ifdef DEBUG
      Serial.print("Key Pressed: ");  // Debug log
      Serial.println(key);
#endif

      if (!firstKeyPressed) {
        firstKeyPressed = true;
        sfx.stop();  // Stop dial tone
        //delay(sfxDelay);  // Give time for sound system to reset
      }

      if (isdigit(key)) {
        triggerSoundUART(tempIndex);  // Ensure sound plays immediately
        phoneNumber[KeyPadCount] = key;
        displayBuffer[KeyPadCount + (KeyPadCount > 1 ? 1 : 0)] = key;

        lcd.setCursor(5, 1);
        lcd.print(displayBuffer);
        lcd.display();  // Ensure LCD updates immediately

        KeyPadCount++;
      } else if (key == '#') {  // Reset input
        KeyPadCount = 0;
        memset(displayBuffer, ' ', 6);
        memset(phoneNumber, ' ', 5);
        lcd.setCursor(5, 1);
        lcd.print(displayBuffer);
        lcd.display();
      } else if (key == '*') {  // Predefined emergency number
        KeyPadCount = 5;
        strcpy(phoneNumber, "11111");
        lcd.setCursor(5, 1);
        lcd.print("11-111");
        lcd.display();
      }
    }
  }

  // Add a small delay to avoid skipping sound on the next state transition
  delay(sfxDelay);

  long trackID = convertFilenameToInt(phoneNumber);
  tempIndex = findTrackNumberByFilename(trackID);

  if (tempIndex == 255) {  // File not found
    sfx.stop();
    triggerSoundUART(findTrackNumberByFilename(callFinished));
    delay(sfxDelay);
    StillLCD_P(invalidNumber, replaceHandset);

    while (isReceiverHookOff()) {
      if (!stillPlaying()) {
        triggerSoundUART(findTrackNumberByFilename(callFinished));
      }
      delay(5);
    }

    currentState = STATE_RESET;
    return 255;
  }

  delay(sfxDelay);
  bool soundPlayed = sfx.playTrack(tempIndex);

  if (!soundPlayed) {
    currentState = STATE_RESET;
    return 255;
  }

  return tempIndex;
}

// ----- StillPlaying -----
// Checks whether a sound track is still playing using the hardware ACT pin
bool stillPlaying() {
  return (digitalRead(SFX_ACT) == LOW);
}

// ----- StillLCD_P -----
// This function is for the messages stored in PROGMEM
void StillLCD_P(const char* text1, const char* text2) {
  char buffer1[17], buffer2[17];  // Buffers for LCD display

  lcd.clear();
  strlcpy_P(buffer1, text1, sizeof(buffer1));  // Safe copy from PROGMEM
  strlcpy_P(buffer2, text2, sizeof(buffer2));
  lcd.setCursor(0, 0);
  lcd.print(buffer1);
  lcd.setCursor(0, 1);
  lcd.print(buffer2);
}

// ----- StillLCD -----
// This function is for dispalying messages not in PROGMEM. Left it here if people convert to a non-AVR controller

void StillLCD(const char* text1, const char* text2) {

  char buffer1[17], buffer2[17];

  lcd.clear();
  strlcpy(buffer1, text1, sizeof(buffer1));
  strlcpy(buffer2, text2, sizeof(buffer2));
  lcd.setCursor(0, 0);
  lcd.print(buffer1);
  lcd.setCursor(0, 1);
  lcd.print(buffer2);
}

// ----- triggerSoundUART -----
// Plays a sound track by finding the corresponding track number.
void triggerSoundUART(uint8_t trackNum) {
  if (trackNum >= 0) {
#ifdef DEBUG
    Serial.print(F("Playing track number: "));
    Serial.println(trackNum);
#endif
    if (!sfx.playTrack(trackNum)) {
#ifdef DEBUG
      Serial.print(F("Error: Failed to play track: "));
      Serial.println(trackNum);
#endif
    }
  }
}

// ----- findTrackNumberByFilename -----
// Searches the file lookup table for a matching filename.
int findTrackNumberByFilename(long trackID) {

#ifdef DEBUG
  Serial.print(F("Trying to find trackID: "));
  Serial.println(trackID);
#endif

  for (uint8_t i = 0; i < totalFiles; i++) {
    //Serial.print("Comparing to: ");
    //Serial.println(audioFiles[i].fileID);

    if (audioFiles[i].fileID == trackID) {
#ifdef DEBUG
      Serial.print(F("Match with filename returing tracknumber: "));
      Serial.println(audioFiles[i].trackNum);
#endif
      return audioFiles[i].trackNum;  // Return the corresponding track number
    }
  }

#ifdef DEBUG
  Serial.print(F("ERROR: File not found for ID: "));
  Serial.println(trackID);
#endif

  // Create a string with "<trackID>.OGG"
  char missingFileMsg[17];
  snprintf(missingFileMsg, sizeof(missingFileMsg), "    %ld.OGG", trackID);

  // Display on LCD
  StillLCD(missingFileMsg, "Number Not Found");
  delay(messageWait);          // Wait 5 seconds for user to see error
  currentState = STATE_RESET;  // Reset system after error

  return 255;
}

//----- printFoundFiles -----
//Prints all found files and their corresponding track numbers.
#ifdef DEBUG

void printFoundFiles() {
  Serial.println(F("Found Files and Track Numbers:"));
  for (uint8_t i = 0; i < totalFiles; i++) {
    Serial.print(F("Track "));
    Serial.print(audioFiles[i].fileID);
    Serial.print(F(": "));
    Serial.println(audioFiles[i].trackNum);
  }
}

#endif

bool isReceiverHookOff() {
  int reading = digitalRead(ReceiverHook);
  //Serial.print(F("📞 Hook State (Raw): "));
  //Serial.println(reading);
  return (reading == LOW);  // ✅ Flip if your switch logic needs it
}

void scanAudioFXStorageNew() {
  //Serial.println(F("Scanning AudioFX storage..."));
  uint8_t files = sfx.listFiles();

  if (files == 0) {
    //Serial.println(F("No audio files found!"));
    StillLCD_P(errorMsg, noFiles);
    delay(messageWait * 3);
    currentState = STATE_RESET;
    return;
  }

  totalFiles = 0;  // Reset before filling the array

  for (uint8_t f = 0; f < files; f++) {
    if (totalFiles >= MAX_TRACKS) {  // ✅ Prevent overflow

      break;  // Stop adding more files
    }

    const char* filename = sfx.fileName(f);
    long trackID = convertFilenameToInt(filename);

    if (trackID == -1) {
      continue;  // Skip invalid filenames
    }

    audioFiles[totalFiles].fileID = trackID;
    audioFiles[totalFiles].trackNum = f;
    totalFiles++;
  }

#ifdef DEBUG
  Serial.print(F("Loaded "));
  Serial.print(totalFiles);
  Serial.println(F(" files from AudioFX."));
#endif
}


void displayLCDTopMessage_P(const char* message) {
  char buffer[17];  // Buffer for a 16-character message + null terminator
  strlcpy_P(buffer, message, sizeof(buffer));

  lcd.setCursor(0, 0);  // Set cursor to the start of the line
  lcdClearLine();       // Print 16 spaces to clear the line
  lcd.setCursor(0, 0);  // Reset cursor to the start
  lcd.print(buffer);
}

long convertFilenameToInt(const char* filename) {

  if (filename == NULL) return -1;

  char numStr[6] = { 0 };  // Buffer for extracted number (max 5 digits + null)
  int index = 0, numIndex = 0;

  // Step 1: Skip leading non-numeric characters
  while (filename[index] != '\0' && !isdigit(filename[index])) {
    index++;
  }

  // Step 2: Extract up to 5 numeric digits
  while (filename[index] != '\0' && filename[index] != '.' && numIndex < 5) {
    if (!isdigit(filename[index])) break;
    numStr[numIndex++] = filename[index];
    index++;
  }

  numStr[numIndex] = '\0';  // Ensure string is null-terminated

  // Step 3: Ensure at least 1 digit was extracted
  if (numIndex == 0) return -1;

  // Step 4: Convert extracted string to long safely
  char* endPtr;
  long num = strtol(numStr, &endPtr, 10);

  // Step 5: Ensure number is valid and within range
  if (*endPtr != '\0' || num < 0 || num > 99999) return -1;

  //Serial.print("Dialed Input: ");
  //Serial.println(filename);
  //Serial.print("Converted to ID: ");
  //Serial.println(num);


  return num;
}

// Processes an incoming call once a valid sequence has been received.
void processIncomingCall(int trackNum) {

  uint8_t index = findTrackNumberByFilename(ringingTone);
  if (index == 255) {  // Ringing file is missing!
    StillLCD_P(ringError, blank);
    currentState = STATE_RESET;
    delay(messageWait * 3);
    return;
  }

  triggerSoundUART(index);

  unsigned long ringStartTime = millis();

  while (millis() - ringStartTime < ringTimeout) {  // Ensure the loop exits after timeout
    if (isReceiverHookOff()) {                      // If the receiver is picked up
      sfx.stop();
      StillLCD_P(incomingCallMsg, connectedMessage);
      delay(sfxDelay);  // Wait until pickup is complete
      triggerSoundUART(trackNum);
      delay(sfxDelay);

      // Wait until the sound file stops playing or the handset is replaced
      checkForHangupOrEnd();

      if (!isReceiverHookOff()) {  // Receiver hung up
        currentState = STATE_RESET;
        return;  // Exit immediately to prevent any further sounds or messages
      }

      index = findTrackNumberByFilename(callFinished);
      triggerSoundUART(index);
      StillLCD_P(callComplete1, replaceHandset);

      while (isReceiverHookOff()) {
        delay(50);  // Wait for the receiver to be hung up
      }

      currentState = STATE_RESET;
      return;
    }



    if (!stillPlaying()) {
      triggerSoundUART(index);  // Restart ringing tone
    }

    delay(5);  // Keep delays minimal
  }
  //  **Check if ringing timeout has passed**
  if (millis() - ringStartTime >= ringTimeout) {
    delay(sfxDelay);
    sfx.stop();

    // **Show timeout message on LCD**
    StillLCD_P(noAnswer, blank);
    delay(messageWait);  // Keep message visible
    currentState = STATE_RESET;
    return;
  }
}

void lcdClearLine() {
  lcd.print("                ");
}

// Poll for an initial keyfob press
int CheckForIncomingCall() {
  static unsigned long lastDebounceTime = 0;
  static int lastButtonPressed = -1;
  static bool buttonStable = false;

  for (uint8_t i = 0; i < numButtons; i++) {
    int reading = digitalRead(keyfobButtons[i].pin);

    if (reading == LOW) {            // Button is pressed
      if (lastButtonPressed != i) {  // New press detected
        lastDebounceTime = millis();
        lastButtonPressed = i;
        buttonStable = false;
      }

      if ((millis() - lastDebounceTime) > debounceDelay) {  // Check if stable
        if (!buttonStable) {
          buttonStable = true;
          //Serial.println(F("🔔 Incoming call detected, processing..."));
          return i;
        }
      }
    } else if (lastButtonPressed == i) {  // Button released
      lastButtonPressed = -1;
      buttonStable = false;
    }
  }

  return -1;  // No button press detected
}


int ProcessIncomingKeyFob(uint8_t firstButtonIndex) {
  uint8_t firstDigit = keyfobButtons[firstButtonIndex].number;
  char firstLetter = keyfobButtons[firstButtonIndex].letter;
  uint8_t secondDigit = 255;
  uint8_t detectedIndex = 255;

  lcd.setCursor(0, 1);
  lcdClearLine();
  lcd.setCursor(0, 1);
  lcd.print(firstLetter);  //Display on the LCD for feedback and troubleshooting.

  // Wait for first button release with non-blocking debounce
  while (digitalRead(keyfobButtons[firstButtonIndex].pin) == LOW) {
    delay(5);  // Minimum delay to avoid excessive CPU load
  }

  // Wait up to 5 seconds for a second key press
  unsigned long startTime = millis();
  unsigned long lastDebounceTime = 0;
  uint8_t lastButtonPressed = 255;

  while (millis() - startTime < 5000) {  // 5-second window for second press
    for (uint8_t i = 0; i < numButtons; i++) {
      int reading = digitalRead(keyfobButtons[i].pin);

      if (reading == LOW) {            // Button pressed
        if (lastButtonPressed != i) {  // New press detected
          lastDebounceTime = millis();
          lastButtonPressed = i;
        }

        if ((millis() - lastDebounceTime) > debounceDelay) {  // Stable press
          detectedIndex = i;
          break;
        }
      } else if (lastButtonPressed == i) {  // Button released
        lastButtonPressed = 255;            // Reset for next detection
      }
    }

    if (detectedIndex != 255) break;  // Exit loop once second press is confirmed
  }

  if (detectedIndex != 255) {  // Second press detected
    secondDigit = keyfobButtons[detectedIndex].number;
    char secondLetter = keyfobButtons[detectedIndex].letter;

    lcd.setCursor(1, 1);
    lcd.print(secondLetter);
    delay(messageWait);

    int finalSequence = (firstDigit * 10) + secondDigit;
    //Serial.print(F("Final Sequence: "));
    //Serial.println(finalSequence);

    return findTrackNumberByFilename(finalSequence);
  }

  return 255;  // No valid incoming call sequence
}

void checkForHangupOrEnd() {

  while (stillPlaying()) {

    if (!isReceiverHookOff()) {
      //Serial.println("Receiver Hung Up! Ending call.");
      sfx.stop();
      currentState = STATE_RESET;
      return;  // Exit immediately to prevent any further sounds
    }
  }
}
void exitScreensaver() {
  if (!screensaverActive) return;  // Prevent redundant calls
  screensaverActive = false;
  lcd.clear();
  lastActivityTime = millis();  // Reset inactivity timer
}

void activateScreensaver() {
  screensaverActive = true;
  screensaverLastUpdate = millis();
  screensaverPosition = 0;
  screensaverDirection = true;

  // Gradually dim the backlight
  for (int i = 255; i > 0; i -= 25) {
    lcd.setBacklight(i);
    delay(50);
  }

  lcd.clear();
}


void runScreensaver() {
  // Check if enough time has passed for the next animation step
  if (millis() - screensaverLastUpdate < 200) {
    return;  // Wait until the delay interval passes
  }

  screensaverLastUpdate = millis();  // Update the timestamp

  // Erase previous characters (to prevent flickering)
  lcd.setCursor(screensaverPosition, 0);
  lcd.print(" ");
  lcd.setCursor(15 - screensaverPosition, 1);
  lcd.print(" ");

  // Update position for scrolling animation
  if (screensaverDirection) {
    screensaverPosition++;
    if (screensaverPosition > 15) screensaverDirection = false;  // Change direction at right edge
  } else {
    screensaverPosition--;
    if (screensaverPosition < 0) screensaverDirection = true;  // Change direction at left edge
  }

  // Draw the new position of the `*` character
  lcd.setCursor(screensaverPosition, 0);
  lcd.print("*");
  lcd.setCursor(15 - screensaverPosition, 1);
  lcd.print("*");

  // check for user input and exit screensaver immediately
  if (isReceiverHookOff() || keypad.getKey() != NO_KEY) {
    exitScreensaver();
  }
}

//End of NeoTelephone