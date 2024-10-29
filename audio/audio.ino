#include "Arduino.h"
#include "FS.h"
#include "SPIFFS.h"
#include "driver/i2s.h"

// Define the I2S pins (adjust as needed)
#define I2S_WS   6    // LRCLK (Word Select)
#define I2S_BCK  7    // Bit Clock
#define I2S_DATA 9    // I2S Data

#define SAMPLE_RATE 16000    // Ensure this matches your audio file's sample rate
#define BUFFER_SIZE 512      // Buffer size for reading data
#define GAIN 2.0             // Amplification factor (increase or decrease as needed)

File audioFile;
int16_t audioBuffer[BUFFER_SIZE];  // Buffer to hold audio samples

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

    // Configure I2S
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_DATA,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    // Install and start I2S driver
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_set_clk(I2S_NUM_0, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);

    Serial.println("Starting audio playback with amplification...");
}

void loop() {
    if (audioFile.available()) {
        size_t bytesRead = audioFile.read((uint8_t *)audioBuffer, BUFFER_SIZE * sizeof(int16_t));

        // Amplify the buffer
        amplifyBuffer(audioBuffer, BUFFER_SIZE, GAIN);

        size_t bytesWritten;
        // Write the amplified buffer to I2S
        i2s_write(I2S_NUM_0, audioBuffer, bytesRead, &bytesWritten, portMAX_DELAY);

        if (bytesRead < BUFFER_SIZE * sizeof(int16_t)) {
            // If less data read than buffer size, end of file reached
            audioFile.seek(44, SeekSet);  // Reset to loop the audio
        }
    } else {
        Serial.println("Audio playback finished");
        audioFile.seek(44, SeekSet);  // Reset to loop the audio
    }
}
