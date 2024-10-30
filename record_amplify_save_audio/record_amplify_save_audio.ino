#include "ESP_I2S.h"
#include "FS.h"
#include "SPIFFS.h"

#define FORMAT_SPIFFS_IF_FAILED true
#define RECORD_TIME_SECONDS 10  // Recording duration in seconds
#define AMPLIFICATION_FACTOR 2.0  // Adjust this value to increase/decrease amplification

I2SClass I2S;

void printSpiffsInfo() {
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    Serial.println("===== SPIFFS Info =====");
    Serial.printf("Total space: %u bytes\n", totalBytes);
    Serial.printf("Used space: %u bytes\n", usedBytes);
    Serial.printf("Free space: %u bytes\n", totalBytes - usedBytes);
    Serial.println("=====================");
}

void clearSpiffs() {
    Serial.println("Clearing all files from SPIFFS...");
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    
    while(file) {
        String fileName = file.name();
        Serial.printf("Deleting: %s\n", fileName.c_str());
        SPIFFS.remove(fileName);
        file = root.openNextFile();
    }
    Serial.println("All files deleted from SPIFFS");
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("Failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if(levels){
                listDir(fs, file.path(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

// Function to amplify audio data
void amplifyAudio(uint8_t* wav_buffer, size_t wav_size) {
    // Skip the WAV header (44 bytes)
    const int WAV_HEADER_SIZE = 44;
    
    // Process audio data as 16-bit samples
    int16_t* samples = (int16_t*)(wav_buffer + WAV_HEADER_SIZE);
    size_t sample_count = (wav_size - WAV_HEADER_SIZE) / 2;  // Divide by 2 because each sample is 2 bytes
    
    // Amplify each sample
    for(size_t i = 0; i < sample_count; i++) {
        // Convert to float for processing
        float sample = samples[i] * AMPLIFICATION_FACTOR;
        
        // Clamp the values to prevent overflow
        if(sample > 32767.0f) sample = 32767.0f;
        if(sample < -32768.0f) sample = -32768.0f;
        
        // Convert back to int16_t
        samples[i] = (int16_t)sample;
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }
    
    // Initialize SPIFFS
    if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
        Serial.println("SPIFFS Mount Failed");
        return;
    }

    // Clear all files from SPIFFS
    clearSpiffs();

    // Print initial SPIFFS info
    printSpiffsInfo();
    
    // Initialize I2S
    Serial.println("Initializing I2S...");
    I2S.setPinsPdmRx(42, 41);
    if (!I2S.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        Serial.println("Failed to initialize I2S!");
        return;
    }
    
    const char* filename = "/recording.wav";
    
    Serial.printf("Recording for %d seconds...\n", RECORD_TIME_SECONDS);
    
    // Record audio using the built-in recordWAV function
    uint8_t *wav_buffer;
    size_t wav_size;
    wav_buffer = I2S.recordWAV(RECORD_TIME_SECONDS, &wav_size);
    
    if (wav_buffer == nullptr) {
        Serial.println("Failed to record audio!");
        return;
    }
    
    Serial.printf("Recording complete! WAV size: %d bytes\n", wav_size);

    // Amplify the recorded audio
    Serial.println("Amplifying audio...");
    amplifyAudio(wav_buffer, wav_size);
    Serial.println("Audio amplification complete");

    // Check if we have enough space
    if (wav_size > (SPIFFS.totalBytes() - SPIFFS.usedBytes())) {
        Serial.println("Not enough space in SPIFFS!");
        free(wav_buffer);
        return;
    }
    
    // Save to SPIFFS
    File file = SPIFFS.open(filename, FILE_WRITE);
    if(!file){
        Serial.println("Failed to open file for writing");
        free(wav_buffer);
        return;
    }
    
    // Write the WAV data in chunks
    const size_t chunk_size = 1024; // Write 1KB at a time
    size_t bytes_written = 0;
    
    while (bytes_written < wav_size) {
        size_t bytes_to_write = min(chunk_size, wav_size - bytes_written);
        size_t bytes = file.write(wav_buffer + bytes_written, bytes_to_write);
        
        if (bytes == 0) {
            Serial.println("Write failed!");
            file.close();
            free(wav_buffer);
            return;
        }
        
        bytes_written += bytes;
        
        // Print progress
        if (bytes_written % 32768 == 0) { // Print every 32KB
            Serial.printf("Written %u of %u bytes\n", bytes_written, wav_size);
        }
    }
    
    file.close();
    Serial.println("Amplified audio saved to SPIFFS successfully!");
    
    // Free the WAV buffer
    free(wav_buffer);
    
    // Print final SPIFFS info
    printSpiffsInfo();
    
    // List files in SPIFFS
    listDir(SPIFFS, "/", 0);
    
    // Clean up
    I2S.end();
}

void loop() {
    delay(1000);
}