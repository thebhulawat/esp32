#include "ESP_I2S.h"
#include <WiFi.h>
#include <WebSocketsServer.h>

// WiFi credentials
const char* ssid = "Yashwant jio 4";
const char* password = "17051969";

// Create WebSocket server
WebSocketsServer webSocket = WebSocketsServer(81);

// I2S configuration
I2SClass I2S;
const int BUFFER_SIZE = 1024;  // Size of each audio chunk
int16_t audioBuffer[BUFFER_SIZE];
bool isRecording = false;

// Function to read from I2S
size_t readI2S() {
    size_t bytesRead = 0;
    uint8_t* ptr = (uint8_t*)audioBuffer;
    
    // Read sample by sample
    for(int i = 0; i < BUFFER_SIZE; i++) {
        int sample = I2S.read();
        if(sample == -1) break;  // No more data
        
        // Store the 16-bit sample
        ptr[bytesRead++] = sample & 0xFF;
        ptr[bytesRead++] = (sample >> 8) & 0xFF;
        
        if(bytesRead >= BUFFER_SIZE * 2) break;  // Buffer full
    }
    
    return bytesRead;
}

void onWebSocketEvent(uint8_t client_num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", client_num);
            isRecording = false;
            break;
            
        case WStype_CONNECTED:
            Serial.printf("[%u] Connected!\n", client_num);
            break;
            
        case WStype_TEXT:
            if (strcmp((char*)payload, "START") == 0) {
                isRecording = true;
                Serial.println("Starting audio stream...");
            } else if (strcmp((char*)payload, "STOP") == 0) {
                isRecording = false;
                Serial.println("Stopping audio stream...");
            }
            break;
    }
}

void setup() {
    Serial.begin(115200);
    
    // Connect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Initialize I2S
    I2S.setPinsPdmRx(42, 41);
    if (!I2S.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        Serial.println("Failed to initialize I2S!");
        return;
    }
    
    // Start WebSocket server
    webSocket.begin();
    webSocket.onEvent(onWebSocketEvent);
    
    Serial.println("WebSocket server started on port 81");
}

void loop() {
    webSocket.loop();
    
    if (isRecording) {
        // Read audio data from I2S
        size_t bytesRead = readI2S();
        
        if (bytesRead > 0) {
            // Send audio data to all connected clients
            webSocket.broadcastBIN((uint8_t*)audioBuffer, bytesRead);
        }
    }
    
    delay(1); // Small delay to prevent watchdog timeout
}