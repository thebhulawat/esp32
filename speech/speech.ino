#include "Arduino.h"
#include "driver/i2s.h"
#include "AudioSampleSample.h" // Include your audio sample header

// Define the I2S pins (according to your setup)
#define I2S_WS   6    // LRCLK (Word Select)
#define I2S_BCK  7    // Bit Clock
#define I2S_DATA 9    // I2S Data

#define SAMPLE_RATE 44100  // Make sure this matches your audio file's sample rate
#define BUFFER_SIZE 256

// Buffer to hold audio samples for processing
int16_t audioBuffer[BUFFER_SIZE];
unsigned int currentSampleIndex = 0;

void fillBuffer() {
    for (int i = 0; i < BUFFER_SIZE; i += 2) {
        if (currentSampleIndex < 79937) { // Use your actual array size
            // Convert 32-bit sample to 16-bit
            // Assuming the original sample is in the correct format, you might need to adjust this conversion
            int32_t sample = pgm_read_dword(&AudioSampleSample[currentSampleIndex]);
            
            // Convert to 16-bit and handle stereo
            audioBuffer[i] = (int16_t)(sample >> 16);     // Left channel
            audioBuffer[i + 1] = (int16_t)(sample >> 16); // Right channel
            
            currentSampleIndex++;
        } else {
            // If we've reached the end of the sample, reset or fill with silence
            audioBuffer[i] = 0;
            audioBuffer[i + 1] = 0;
            // Optionally reset the index to loop the audio
            // currentSampleIndex = 0;
        }
    }
}

void setup() {
    Serial.begin(115200);

    // Configure I2S interface
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

    Serial.println("Starting audio playback...");
}

void loop() {
    fillBuffer();  // Fill the buffer with audio data
    size_t bytes_written;

    // Write the buffer to I2S
    i2s_write(I2S_NUM_0, audioBuffer, sizeof(audioBuffer), &bytes_written, portMAX_DELAY);

    // Optional: Add some basic playback control
    if (currentSampleIndex >= 79937) {
        // End of audio reached
        delay(1000);  // Wait before looping or stopping
        // Uncomment the next line if you want to loop the audio
        // currentSampleIndex = 0;
    }
}