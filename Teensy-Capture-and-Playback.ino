#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

//*************************************************************
// USER CONFIG
//*************************************************************
#define CAPTURE_PIN   A0    // Teensy 4.1 pin for digital input
#define OUTPUT_PIN    A9    // Teensy 4.1 pin for digital output

// At 300 kHz, 50 ms => ~15,000 samples
const uint32_t SAMPLE_RATE  = 300000; 
const uint32_t NUM_SAMPLES  = 15000;  

// We'll store files named capture0.bin, capture1.bin, etc. on the SD.
static const char* FILE_PREFIX = "capture";
static const char* FILE_SUFFIX = ".bin";

//*************************************************************
// GLOBALS
//*************************************************************

// Timers for capture & playback
IntervalTimer captureTimer;
IntervalTimer playTimer;

// Capture buffer in RAM (15,000 bytes)
volatile uint8_t  captureBuffer[NUM_SAMPLES];
volatile uint32_t sampleIndex   = 0;
volatile bool     capturing     = false;
volatile bool     playing       = false;

// For playback from RAM:
static volatile const uint8_t* playDataPtr = nullptr;
static volatile uint32_t       playDataSize = 0;
static volatile uint32_t       playPos = 0;

// We’ll give a message when capturing starts/ends
static bool captureJustStarted = false;

// For playback looping & pause
bool     loopPlayback   = false;          // If true, we re-start playback after finishing
uint32_t playbackPauseMs = 0;            // Pause (in ms) between playbacks
uint32_t lastPlaybackEndMillis = 0;      // when playback ended

//*************************************************************
// CAPTURE (ISR)
//*************************************************************
void captureSample() {
  if (!capturing) return;

  // read digital (0 or 1)
  uint8_t val = digitalReadFast(CAPTURE_PIN);
  captureBuffer[sampleIndex++] = val;

  // If we've reached the end, stop
  if (sampleIndex >= NUM_SAMPLES) {
    capturing = false;
    captureTimer.end();
  }
}

//*************************************************************
// PLAYBACK (ISR)
//*************************************************************
void playbackCallback() {
  if (!playing || playDataPtr == nullptr) return;

  digitalWriteFast(OUTPUT_PIN, playDataPtr[playPos]);

  playPos++;
  if (playPos >= playDataSize) {
    // Reached the end of this playback
    playTimer.end();
    playing = false;
    playPos = 0;

    // Record time we finished
    lastPlaybackEndMillis = millis();
    // Pin goes LOW at the end
    digitalWriteFast(OUTPUT_PIN, LOW);

    // We'll handle the "looping" re-start in the main loop() 
  }
}

//*************************************************************
// STOP ALL
//*************************************************************
void stopAll() {
  captureTimer.end();
  playTimer.end();
  capturing = false;
  playing   = false;
  sampleIndex = 0;

  // Clear playback globals
  playDataPtr  = nullptr;
  playDataSize = 0;
  playPos      = 0;

  digitalWrite(OUTPUT_PIN, LOW);
  Serial.println("Stopped all capture/playback.");
}

//*************************************************************
// SD HELPER FUNCTIONS
//*************************************************************
// Build "capture0.bin", "capture1.bin", etc.
String buildFileName(uint8_t index) {
  String fname = FILE_PREFIX;
  fname += index;
  fname += FILE_SUFFIX;
  return fname;
}

// Find next file index that doesn't exist on SD
int findNextFileIndex() {
  for (int i = 0; i < 255; i++) {
    String fname = buildFileName(i);
    if (!SD.exists(fname.c_str())) {
      return i; // free slot
    }
  }
  return -1; // no free index
}

//*************************************************************
// WAIT FOR PIN STATE
//*************************************************************
// Blocks until CAPTURE_PIN reads the desired state (HIGH or LOW).
void waitForPinState(bool waitForHigh) {
  if (waitForHigh) {
    Serial.println("Waiting for pin to go HIGH...");
    while (digitalRead(CAPTURE_PIN) == LOW) {
      yield(); // Avoid locking up
    }
  } else {
    Serial.println("Waiting for pin to go LOW...");
    while (digitalRead(CAPTURE_PIN) == HIGH) {
      yield();
    }
  }
  // Once the pin is at the desired state, exit
}

//*************************************************************
// ARDUINO SETUP
//*************************************************************
void setup() {
  Serial.begin(115200);
  while (!Serial) { /* wait */ }

  pinMode(CAPTURE_PIN, INPUT);
  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, LOW);

  // Initialize SD card
  Serial.println("Initializing SD card...");
  if (!SD.begin(BUILTIN_SDCARD)) {
    Serial.println("SD card initialization failed!");
  } else {
    Serial.println("SD card initialized.");
  }

  Serial.println("Teensy 4.1 Capture/Playback + SD Demo");
  Serial.println("Commands:");
  Serial.println("  READHIGH  -> Wait pin HIGH, capture 50 ms @300kHz");
  Serial.println("  READLOW   -> Wait pin LOW, capture 50 ms @300kHz");
  Serial.println("  SAVE      -> Save RAM buffer to SD (auto filename)");
  Serial.println("  PLAY      -> Play from RAM");
  Serial.println("  PLAY N    -> Load captureN.bin from SD to RAM, then play");
  Serial.println("  PAUSE ms  -> Set pause (ms) between looped playbacks");
  Serial.println("  LOOP      -> Toggle playback looping ON/OFF");
  Serial.println("  STOP      -> Stop capturing/playing");
  Serial.println("  FORMAT    -> Remove all capture*.bin files");
}

//*************************************************************
// CAPTURE START
//*************************************************************
void startCapture() {
  // Reset the buffer
  for (uint32_t i = 0; i < NUM_SAMPLES; i++) {
    captureBuffer[i] = 0;
  }
  sampleIndex = 0;
  capturing  = true;
  captureJustStarted = true;

  bool result = captureTimer.begin(captureSample, 1000000.0f / SAMPLE_RATE);
  if (!result) {
    Serial.println("Failed to start capture timer!");
    capturing = false;
  } else {
    Serial.println("Capture started!");
  }
}

//*************************************************************
// MAIN LOOP
//*************************************************************
void loop() {
  // 1) Handle capture completion
  // If capturing ended in the ISR (sampleIndex >= NUM_SAMPLES),
  // we detect it here to print "Capture ended."
  static bool wasCapturing = false;
  if (capturing != wasCapturing) {
    // State changed
    if (!capturing && wasCapturing) {
      // Just finished capturing
      captureTimer.end(); // ensure timer is off
      Serial.print("Capture ended. Collected ");
      Serial.print(sampleIndex);
      Serial.println(" samples.");
    }
    wasCapturing = capturing;
  }
  
  // 2) Playback looping logic
  // If loopPlayback == true, and we are NOT playing,
  // we wait the user-set pause, then re-start playback
  // from RAM (assuming there's a buffer).
  static bool wasPlaying = false;
  if (wasPlaying && !playing) {
    // We just finished a playback 
    // -> this was handled in playbackCallback, which sets playing=false
  }
  wasPlaying = playing;

  // If we want to loop, we're not currently playing, and we have some data in RAM
  if (loopPlayback && !playing && (sampleIndex > 0)) {
    // Check if the pause has elapsed
    if (millis() - lastPlaybackEndMillis >= playbackPauseMs) {
      // Start playback from the beginning
      playPos      = 0;
      playDataPtr  = captureBuffer;
      playDataSize = sampleIndex;
      playing      = true;

      bool result = playTimer.begin(playbackCallback, 1000000.0f / SAMPLE_RATE);
      if (!result) {
        Serial.println("Failed to start playback timer (loop)!");
        playing = false;
      } else {
        Serial.println("Playback (loop) started again from RAM.");
      }
    }
  }

  // 3) Check for serial commands
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    //--------------------
    // READHIGH
    //--------------------
    if (command.equalsIgnoreCase("READHIGH")) {
      if (capturing) {
        Serial.println("Already capturing. STOP first if needed.");
      } else {
        waitForPinState(true); // wait for pin to go HIGH
        startCapture();
      }

    //--------------------
    // READLOW
    //--------------------
    } else if (command.equalsIgnoreCase("READLOW")) {
      if (capturing) {
        Serial.println("Already capturing. STOP first if needed.");
      } else {
        waitForPinState(false); // wait for pin to go LOW
        startCapture();
      }

    //--------------------
    // SAVE
    //--------------------
    } else if (command.equalsIgnoreCase("SAVE")) {
      if (capturing) {
        Serial.println("Still capturing. Wait or STOP first.");
      } else if (sampleIndex == 0) {
        Serial.println("No data in RAM. Use READHIGH/READLOW first.");
      } else {
        // Find next free file index
        int fileIndex = findNextFileIndex();
        if (fileIndex < 0) {
          Serial.println("No free file index (0-254). SD might be full or too many files!");
        } else {
          String fname = buildFileName(fileIndex);
          File f = SD.open(fname.c_str(), FILE_WRITE);
          if (!f) {
            Serial.print("Error opening for write: ");
            Serial.println(fname);
          } else {
            uint32_t sizeToWrite = sampleIndex;
            // Write a 4-byte header with the sample count
            f.write((uint8_t*)&sizeToWrite, sizeof(sizeToWrite));
            // Write samples
            f.write((uint8_t*)captureBuffer, sampleIndex);
            f.close();
            Serial.print("Saved to ");
            Serial.println(fname);
          }
        }
      }

    //--------------------
    // PLAY (RAM)
    //--------------------
    } else if (command.equalsIgnoreCase("PLAY")) {
      if (capturing) {
        Serial.println("Cannot play while capturing. STOP first.");
      } else if (sampleIndex == 0) {
        Serial.println("No data in RAM. Use READHIGH/READLOW or PLAY N first.");
      } else if (playing) {
        Serial.println("Already playing. STOP first if you want to restart.");
      } else {
        // Start playback from RAM
        playPos = 0;
        playDataPtr  = captureBuffer;
        playDataSize = sampleIndex;
        playing      = true;
        bool result = playTimer.begin(playbackCallback, 1000000.0f / SAMPLE_RATE);
        if (!result) {
          Serial.println("Failed to start playback timer!");
          playing = false;
        } else {
          Serial.println("Playing RAM buffer on pin A9 (no pause).");
        }
      }

    //--------------------
    // PLAY N (from SD)
    //--------------------
    } else if (command.startsWith("PLAY ")) {
      int fileIndex = command.substring(5).toInt();
      if (capturing) {
        Serial.println("Cannot load/play from SD while capturing. STOP first.");
      } else if (playing) {
        Serial.println("Already playing. STOP first.");
      } else {
        String fname = buildFileName(fileIndex);
        if (!SD.exists(fname.c_str())) {
          Serial.print("File not found: ");
          Serial.println(fname);
        } else {
          File f = SD.open(fname.c_str(), FILE_READ);
          if (!f) {
            Serial.print("Failed to open: ");
            Serial.println(fname);
          } else {
            // Read 4-byte header
            uint32_t fileSampleCount = 0;
            if (f.read((uint8_t*)&fileSampleCount, sizeof(fileSampleCount)) != sizeof(fileSampleCount)) {
              Serial.println("Error reading sample count from file.");
              f.close();
            } else {
              if (fileSampleCount > NUM_SAMPLES) {
                Serial.println("File data bigger than captureBuffer; truncating!");
                fileSampleCount = NUM_SAMPLES;
              }
              // Read the samples
              uint32_t bytesRead = f.read((uint8_t*)captureBuffer, fileSampleCount);
              f.close();
              sampleIndex = fileSampleCount; // how many valid samples in RAM
              if (bytesRead < fileSampleCount) {
                Serial.println("Warning: could not read full data from SD.");
              }

              // Now play from RAM
              playPos      = 0;
              playDataPtr  = captureBuffer;
              playDataSize = sampleIndex;
              playing      = true;
              bool result = playTimer.begin(playbackCallback, 1000000.0f / SAMPLE_RATE);
              if (!result) {
                Serial.println("Failed to start playback timer!");
                playing = false;
              } else {
                Serial.print("Loaded ");
                Serial.print(fname);
                Serial.println(" -> playing on pin A9.");
              }
            }
          }
        }
      }

    //--------------------
    // STOP
    //--------------------
    } else if (command.equalsIgnoreCase("STOP")) {
      stopAll();

    //--------------------
    // FORMAT (delete capture*.bin)
    //--------------------
    } else if (command.equalsIgnoreCase("FORMAT")) {
      stopAll();
      uint16_t count = 0;
      for (int i = 0; i < 255; i++) {
        String fname = buildFileName(i);
        if (SD.exists(fname.c_str())) {
          if (SD.remove(fname.c_str())) {
            count++;
          }
        }
      }
      Serial.print("Deleted ");
      Serial.print(count);
      Serial.println(" capture*.bin files.");

    //--------------------
    // LOOP (toggle playback looping)
    //--------------------
   } else if (command.equalsIgnoreCase("LOOP")) {
      loopPlayback = !loopPlayback;
      Serial.print("Looping is now: ");
      Serial.println(loopPlayback ? "ON" : "OFF");

    //--------------------
    // PAUSE <ms>
    //--------------------
    } else if (command.startsWith("PAUSE ")) {
      // e.g. "PAUSE 500"
      uint32_t p = command.substring(6).toInt();
      playbackPauseMs = p;
      Serial.print("Playback pause set to ");
      Serial.print(playbackPauseMs);
      Serial.println(" ms.");

    //--------------------
    // UNKNOWN
    //--------------------
    } else {
      Serial.println("Unknown command. Commands:");
      Serial.println("  READHIGH, READLOW");
      Serial.println("  SAVE, PLAY, PLAY N, STOP");
      Serial.println("  FORMAT, LOOP, PAUSE <ms>");
    }
  }
}
