#include <ESP_I2S.h>
#include "FS.h"
#include "SPIFFS.h"

// make changes as needed
#define RECORD_TIME   10  // seconds, changed from 20 to 10 as requested
#define FORMAT_SPIFFS_IF_FAILED true

// do not change for best
#define SAMPLE_RATE 16000U
#define SAMPLE_BITS 16
#define VOLUME_GAIN 2

I2SClass I2S;

// WAV header structure
struct WavHeader {
    // RIFF chunk
    char riff_header[4] = {'R', 'I', 'F', 'F'};
    uint32_t wav_size;       // Size of WAV
    char wave_header[4] = {'W', 'A', 'V', 'E'};
    
    // fmt chunk
    char fmt_header[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_chunk_size = 16;
    uint16_t audio_format = 1;    // PCM
    uint16_t num_channels = 1;    // Mono
    uint32_t sample_rate = SAMPLE_RATE;
    uint32_t byte_rate = SAMPLE_RATE * SAMPLE_BITS/8;
    uint16_t sample_alignment = SAMPLE_BITS/8;
    uint16_t bit_depth = SAMPLE_BITS;
    
    // data chunk
    char data_header[4] = {'d', 'a', 't', 'a'};
    uint32_t data_bytes;
};

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // Initialize SPIFFS
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  // Delete existing recording if it exists
  if (SPIFFS.exists("/recording.wav")) {
    SPIFFS.remove("/recording.wav");
    Serial.println("Deleted existing recording");
  }

  // setup 42 PDM clock and 41 PDM data pins
  I2S.setPinsPdmRx(42, 41);

  // start I2S at 16 kHz with 16-bits per sample
  if (!I2S.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("Failed to initialize I2S!");
    while (1); // do nothing
  }

  record_audio();
}

void loop() {
  delay(1000);
  Serial.print(".");
}

void record_audio() {
  uint32_t sample_size = 0;
  uint32_t record_size = (SAMPLE_RATE * SAMPLE_BITS / 8) * RECORD_TIME;
  char *rec_buffer = NULL;  
  Serial.println("Ready to start recording...");

  // PSRAM malloc for recording
  rec_buffer = (char *)ps_malloc(record_size);  
  if (rec_buffer == NULL) {
    Serial.println("malloc failed!");
    while(1);
  }
  Serial.printf("Buffer: %d bytes\n", ESP.getPsramSize() - ESP.getFreePsram());

  // Start recording
  sample_size = I2S.readBytes(rec_buffer, record_size);
  if (sample_size == 0) {
    Serial.println("Record Failed!");
  } else {
    Serial.printf("Recorded %d bytes\n", sample_size);
  }

  // Increase volume
  for (uint32_t i = 0; i < sample_size; i += SAMPLE_BITS/8) {
    (*(int16_t *)(rec_buffer+i)) <<= VOLUME_GAIN;
  }

  // Create WAV header
  WavHeader header;
  header.data_bytes = sample_size;
  header.wav_size = header.data_bytes + 44 - 8;  // 44 is total header size, 8 is size of wav_size and riff_header

  // Write to SPIFFS with WAV header
  File file = SPIFFS.open("/recording.wav", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
  } else {
    file.write((const uint8_t*)&header, sizeof(WavHeader));  // Write WAV header
    file.write((const uint8_t*)rec_buffer, sample_size);     // Write audio data
    file.close();
    
    // Get and print the file size
    File savedFile = SPIFFS.open("/recording.wav", FILE_READ);
    if (savedFile) {
      size_t fileSize = savedFile.size();
      Serial.printf("Saved WAV file size: %d bytes\n", fileSize);
      savedFile.close();
    } else {
      Serial.println("Failed to read saved file size");
    }
    
    Serial.println("Recording saved to SPIFFS as WAV");
  }

  free(rec_buffer);
  Serial.println("The recording is over.");
}