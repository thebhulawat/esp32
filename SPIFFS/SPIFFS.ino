#include "FS.h"
#include "SPIFFS.h"
#include "sound.h"  // sound.h with sound_wav array and sound_wav_len

#define FORMAT_SPIFFS_IF_FAILED true

void createWavFile(fs::FS &fs, const char *path) {
  Serial.printf("Creating WAV file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }

  // Write the sound_wav array data to the file
  for (unsigned int i = 0; i < sound_wav_len; i++) {
    file.write(sound_wav[i]);
  }

  file.close();
  Serial.println("- WAV file created in SPIFFS.");
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("\tSIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void readFile(fs::FS &fs, const char *path) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return;
  }

  Serial.println("- read from file:");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void setup() {
  Serial.begin(115200);

  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  listDir(SPIFFS, "/", 0);

  // // Create the WAV file in SPIFFS only if it doesn't exist
  // const char *filePath = "/sound.wav";
  // if (!SPIFFS.exists(filePath)) {
  //   Serial.println("Creating WAV file");
  //   createWavFile(SPIFFS, filePath);
  // } else {
  //   Serial.println("WAV file already exists in SPIFFS.");
  // }

  // Read back the WAV file to verify
  //readFile(SPIFFS, filePath);

  listDir(SPIFFS, "/", 0);
  

  Serial.println("Setup complete");
}

void loop() {}
