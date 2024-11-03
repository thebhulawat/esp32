#include "ESP_I2S.h"
#include <WiFi.h>
#include <WebSocketsServer.h>

const char* ssid = "TP-Link_5B38";
const char* password = "83110122";

// Create WebSocket server
WebSocketsServer webSocket = WebSocketsServer(81);

// I2S configuration
I2SClass I2S;
const int BUFFER_SIZE = 1024;
int16_t audioBuffer[BUFFER_SIZE];
bool isRecording = false;

// Function to read from I2S
size_t readI2S() {
    size_t bytesRead = 0;
    uint8_t* ptr = (uint8_t*)audioBuffer;
    
    // Read sample by sample
    for(int i = 0; i < BUFFER_SIZE; i++) {
        int sample = I2S.read();
        if(sample == -1) {
            Serial.println("I2S read error");
            break;
        }
        
        ptr[bytesRead++] = sample & 0xFF;
        ptr[bytesRead++] = (sample >> 8) & 0xFF;
        
        if(bytesRead >= BUFFER_SIZE * 2) break;
    }
    
    if(bytesRead > 0) {
        Serial.printf("Read %d bytes from I2S\n", bytesRead);
    }
    return bytesRead;
}

void onWebSocketEvent(uint8_t client_num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[WebSocket] Client #%u Disconnected\n", client_num);
            isRecording = false;
            break;
            
        case WStype_CONNECTED:
            Serial.printf("[WebSocket] Client #%u Connected from %d.%d.%d.%d\n", client_num,
                webSocket.remoteIP(client_num)[0], webSocket.remoteIP(client_num)[1],
                webSocket.remoteIP(client_num)[2], webSocket.remoteIP(client_num)[3]);
            break;
            
        case WStype_TEXT:
            Serial.printf("[WebSocket] Received text: %s\n", payload);
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
    delay(1000);  // Give serial time to initialize
    
    Serial.println("\n\nStarting ESP32 Audio WebSocket Server...");
    
    // We start by connecting to a WiFi network
    Serial.println();
    Serial.println();
    
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println();
    
    Serial.println("Connected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    

    // Initialize I2S
    Serial.println("Initializing I2S...");
    I2S.setPinsPdmRx(42, 41);
    if (!I2S.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        Serial.println("Failed to initialize I2S!");
        return;
    }
    Serial.println("I2S initialized successfully");
    
    // Start WebSocket server
    webSocket.begin();
    webSocket.onEvent(onWebSocketEvent);
    Serial.println("WebSocket server started on port 81");
    Serial.println("Setup complete! Ready for connections...");
}

void loop() {
    webSocket.loop();
    
    if (isRecording) {
        size_t bytesRead = readI2S();
        
        if (bytesRead > 0) {
            // Send audio data to all connected clients
            webSocket.broadcastBIN((uint8_t*)audioBuffer, bytesRead);
            
            // Print every 100th buffer to avoid flooding serial
            static int bufferCount = 0;
            if(++bufferCount % 100 == 0) {
                Serial.printf("Sent buffer #%d (%d bytes)\n", bufferCount, bytesRead);
            }
        }
    }
    

}