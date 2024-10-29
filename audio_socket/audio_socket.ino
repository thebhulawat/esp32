#include <WiFi.h>
#include <WebSocketsServer.h>
#include "driver/i2s.h"

// WiFi credentials
const char* ssid = "Yashwant jio 4";
const char* password = "17051969";

// I2S pins
#define I2S_WS   6    // LRCLK (Word Select)
#define I2S_BCK  7    // Bit Clock
#define I2S_DATA 9    // I2S Data

// Audio configuration
#define SAMPLE_RATE 16000
#define BUFFER_SIZE 512
#define GAIN 2.0

// Monitoring configuration
#define MONITOR_INTERVAL 1000  // Print stats every 1000ms
unsigned long lastMonitorTime = 0;
unsigned long totalBytesReceived = 0;
unsigned long bytesThisSecond = 0;
unsigned long packetsThisSecond = 0;
bool isStreaming = false;
unsigned long streamStartTime = 0;

// Global objects
WebSocketsServer webSocket = WebSocketsServer(81);
int16_t audioBuffer[BUFFER_SIZE];

void printStreamStats() {
    unsigned long currentTime = millis();
    float kbps = (bytesThisSecond * 8.0 / 1024.0);  // Convert to kilobits per second
    float streamDuration = (currentTime - streamStartTime) / 1000.0;  // Duration in seconds
    
    Serial.println("\n=== Streaming Statistics ===");
    Serial.printf("Bandwidth: %.2f kbps\n", kbps);
    Serial.printf("Packets/sec: %lu\n", packetsThisSecond);
    Serial.printf("Total data received: %.2f KB\n", totalBytesReceived / 1024.0);
    Serial.printf("Stream duration: %.1f seconds\n", streamDuration);
    Serial.printf("Buffer utilization: %lu bytes\n", bytesThisSecond);
    Serial.println("=========================\n");
    
    // Reset counters
    bytesThisSecond = 0;
    packetsThisSecond = 0;
}

void amplifyBuffer(int16_t *buffer, size_t size, float gain) {
    for (size_t i = 0; i < size; i++) {
        int32_t sample = buffer[i] * gain;
        // Clip the sample to prevent overflow
        buffer[i] = (sample > INT16_MAX) ? INT16_MAX : (sample < INT16_MIN) ? INT16_MIN : sample;
    }
}

void setupI2S() {
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

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_set_clk(I2S_NUM_0, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", num);
            if (isStreaming) {
                isStreaming = false;
                Serial.println("Streaming stopped");
                printStreamStats();  // Print final stats
            }
            break;
            
        case WStype_CONNECTED:
            {
                IPAddress ip = webSocket.remoteIP(num);
                Serial.printf("[%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
                Serial.println("Waiting for audio stream...");
            }
            break;
            
        case WStype_BIN:
            if (!isStreaming) {
                isStreaming = true;
                streamStartTime = millis();
                Serial.println("Audio streaming started!");
            }
            
            if (length >= sizeof(int16_t)) {
                // Update statistics
                totalBytesReceived += length;
                bytesThisSecond += length;
                packetsThisSecond++;
                
                // Calculate how many samples we received
                size_t samples = length / sizeof(int16_t);
                if (samples > BUFFER_SIZE) samples = BUFFER_SIZE;
                
                // Copy received data to audio buffer
                memcpy(audioBuffer, payload, samples * sizeof(int16_t));
                
                // Amplify the buffer
                amplifyBuffer(audioBuffer, samples, GAIN);
                
                // Write to I2S
                size_t bytesWritten;
                i2s_write(I2S_NUM_0, audioBuffer, samples * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
                
                // Check if it's time to print monitoring info
                unsigned long currentTime = millis();
                if (currentTime - lastMonitorTime >= MONITOR_INTERVAL) {
                    printStreamStats();
                    lastMonitorTime = currentTime;
                }
            }
            break;
    }
}

void setup() {
    Serial.begin(115200);
    
    // Print startup banner
    Serial.println("\n=================================");
    Serial.println("WebSocket Audio Streaming Monitor");
    Serial.println("=================================\n");

    // Connect to WiFi
    Serial.printf("Connecting to WiFi: %s\n", ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi");
    Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());

    // Setup I2S
    setupI2S();
    Serial.println("I2S initialized");

    // Start WebSocket server
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    Serial.println("WebSocket server started");
    Serial.println("\nReady for incoming connections...");
}

void loop() {
    webSocket.loop();
}