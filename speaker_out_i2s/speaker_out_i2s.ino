#include "Arduino.h"
#include "FS.h"
#include "SPIFFS.h"
#include <ESP_I2S.h>

// Define the I2S pins (adjust as needed)
#define I2S_WS   44    // LRCLK (Word Select)
#define I2S_BCK  7    // Bit Clock
#define I2S_DATA 8    // I2S Data

#define SAMPLE_RATE 16000    // Ensure this matches your audio file's sample rate
#define BUFFER_SIZE 1024     // Buffer size for reading data
#define GAIN 2.0            // Amplification factor

File audioFile;
int16_t audioBuffer[BUFFER_SIZE];  // Buffer to hold audio samples

// Create I2S instance
I2SClass I2SAudio;

void amplifyBuffer(int16_t *buffer, size_t size, float gain) {
    for (size_t i = 0; i < size; i++) {
        int32_t sample = buffer[i] * gain;
        // Clip the sample to prevent overflow
        buffer[i] = (sample > INT16_MAX) ? INT16_MAX : (sample < INT16_MIN) ? INT16_MIN : sample;
    }
}

void setup() {
    Serial.begin(115200);

    // Initialize SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        return;
    }

    // Open the audio file
    audioFile = SPIFFS.open("/sound.wav", "r");
    if (!audioFile) {
        Serial.println("Failed to open WAV file in SPIFFS");
        return;
    }

    // Skip WAV header (44 bytes)
    audioFile.seek(44, SeekSet);

    // Initialize I2S
    Serial.println("Initializing I2S...");
    
    // End any existing I2S operations
    I2SAudio.end();
    
    // Configure I2S pins
    I2SAudio.setPins(I2S_BCK, I2S_WS, I2S_DATA);
    
    // Initialize I2S with the configuration
    if (!I2SAudio.begin(I2S_MODE_STD, 
                       SAMPLE_RATE, 
                       I2S_DATA_BIT_WIDTH_16BIT,
                       I2S_SLOT_MODE_STEREO)) {
        Serial.println("Failed to initialize I2S!");
        return;
    }

    Serial.println("Starting audio playback with amplification...");
}

void loop() {
    if (audioFile.available()) {
        // Read audio data into buffer
        size_t bytesRead = audioFile.read((uint8_t *)audioBuffer, BUFFER_SIZE * sizeof(int16_t));
        size_t samplesRead = bytesRead / sizeof(int16_t);

        // Amplify the buffer
        amplifyBuffer(audioBuffer, samplesRead, GAIN);

        // Convert 16-bit samples to bytes and write to I2S
        uint8_t *byteBuffer = (uint8_t *)audioBuffer;
        size_t bytesToWrite = samplesRead * sizeof(int16_t);
        size_t bytesWritten = 0;
        
        while (bytesWritten < bytesToWrite) {
            size_t written = I2SAudio.write(byteBuffer + bytesWritten, 
                                          bytesToWrite - bytesWritten);
            if (written == 0) {
                break;  // Error or buffer full
            }
            bytesWritten += written;
        }

        if (bytesRead < BUFFER_SIZE * sizeof(int16_t)) {
            // If less data read than buffer size, end of file reached
            audioFile.seek(44, SeekSet);  // Reset to loop the audio
        }
    } else {
        Serial.println("Audio playback finished");
        audioFile.seek(44, SeekSet);  // Reset to loop the audio
    }
}
