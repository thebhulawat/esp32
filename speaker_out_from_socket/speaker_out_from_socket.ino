#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <ESP_I2S.h>

#define USE_SERIAL Serial

// Audio configuration
#define SAMPLE_RATE 16000U
#define SAMPLE_BITS 16
#define BUFFER_SIZE 2048
#define BUFFER_COUNT 8
#define VOLUME_GAIN 2

// I2S pins
#define I2S_BCK  7
#define I2S_WS   44
#define I2S_DATA 8

WiFiMulti WiFiMulti;
SocketIOclient socketIO;
I2SClass I2S;

// WebSocket configuration
const char* ssid = "TP-Link_5B38";         
const char* password = "83110122";     
const char* wsHost = "6496-49-205-35-178.ngrok-free.app";
const uint16_t wsPort = 443;
const char* wsPath = "/socket.io/?EIO=4";

// Audio buffer management
struct AudioBuffer {
    uint8_t* data;
    size_t size;
    bool ready;
};

AudioBuffer audioBuffers[BUFFER_COUNT];
int currentPlayBuffer = 0;
int currentFillBuffer = 0;
bool isConnected = false;

void initI2S() {
    USE_SERIAL.println("Initializing I2S...");
    
    I2S.end();
    I2S.setPins(I2S_BCK, I2S_WS, I2S_DATA);
    
    if (!I2S.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        USE_SERIAL.println("Failed to initialize I2S!");
        return;
    }
    
    delay(100);
    USE_SERIAL.println("I2S initialized successfully");
}

void initBuffers() {
    for (int i = 0; i < BUFFER_COUNT; i++) {
        audioBuffers[i].data = (uint8_t*)malloc(BUFFER_SIZE);
        audioBuffers[i].size = 0;
        audioBuffers[i].ready = false;
        if (!audioBuffers[i].data) {
            USE_SERIAL.printf("Failed to allocate buffer %d\n", i);
        }
    }
}

void cleanupBuffers() {
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (audioBuffers[i].data) {
            free(audioBuffers[i].data);
            audioBuffers[i].data = NULL;
        }
    }
}

void amplifyBuffer(int16_t* buffer, size_t size) {
    for (size_t i = 0; i < size/2; i++) {
        int32_t sample = buffer[i] * VOLUME_GAIN;
        buffer[i] = (sample > INT16_MAX) ? INT16_MAX : (sample < INT16_MIN) ? INT16_MIN : sample;
    }
}

void playAudioBuffer(const uint8_t* data, size_t length) {
    size_t bytesWritten = 0;
    uint8_t *byteBuffer = (uint8_t *)data;
    while (bytesWritten < length) {
        size_t written = I2S.write(byteBuffer + bytesWritten, length - bytesWritten);
        if (written == 0) {
            delay(1);
            continue;
        }
        bytesWritten += written;
    }
}

// Base64 decoding function
size_t decode_base64(unsigned char* input, size_t inputLen, uint8_t* output) {
    const char base64_chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    size_t outputLen = 0;
    int tmp = 0;
    int bits = 0;
    
    for (size_t i = 0; i < inputLen; i++) {
        if (input[i] == '=') break;
        
        tmp = tmp << 6;
        for (int j = 0; j < 64; j++) {
            if (base64_chars[j] == input[i]) {
                tmp |= j;
                break;
            }
        }
        bits += 6;
        
        if (bits >= 8) {
            bits -= 8;
            output[outputLen++] = (tmp >> bits) & 0xFF;
        }
    }
    
    return outputLen;
}

void socketIOEvent(socketIOmessageType_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case sIOtype_DISCONNECT:
            USE_SERIAL.printf("[IOc] Disconnected!\n");
            isConnected = false;
            break;
            
        case sIOtype_CONNECT:
            USE_SERIAL.printf("[IOc] Connected to url: %s\n", payload);
            socketIO.send(sIOtype_CONNECT, "/");
            isConnected = true;
            break;
            
        case sIOtype_EVENT:
            {
                DynamicJsonDocument doc(4096);
                DeserializationError error = deserializeJson(doc, payload, length);
                
                if (error) {
                    USE_SERIAL.print(F("deserializeJson() failed: "));
                    USE_SERIAL.println(error.f_str());
                    return;
                }
                
                String eventName = doc[0];
                if (eventName == "sendAudioToClient") {
                    JsonObject audioData = doc[1];
                    if (!audioData.isNull()) {
                        int chunkNumber = audioData["chunkNumber"];
                        const char* base64Data = audioData["data"];
                        
                        AudioBuffer* buffer = &audioBuffers[currentFillBuffer];
                        if (!buffer->ready) {
                            size_t decodedLength = decode_base64(
                                (unsigned char*)base64Data, 
                                strlen(base64Data), 
                                buffer->data
                            );
                            
                            buffer->size = decodedLength;
                            buffer->ready = true;
                            
                            amplifyBuffer((int16_t*)buffer->data, decodedLength);
                            currentFillBuffer = (currentFillBuffer + 1) % BUFFER_COUNT;
                            
                            if (chunkNumber % 10 == 0) {
                                USE_SERIAL.printf("Received chunk %d\n", chunkNumber);
                            }
                        }
                    }
                }
            }
            break;
    }
}

void processTasks() {
    AudioBuffer* buffer = &audioBuffers[currentPlayBuffer];
    if (buffer->ready) {
        playAudioBuffer(buffer->data, buffer->size);
        buffer->ready = false;
        buffer->size = 0;
        currentPlayBuffer = (currentPlayBuffer + 1) % BUFFER_COUNT;
    }
}

void setup() {
    USE_SERIAL.begin(115200);
    USE_SERIAL.setDebugOutput(true);
    delay(1000);
    
    USE_SERIAL.println("\nStarting up...");
    
    initBuffers();
    initI2S();
    
    WiFiMulti.addAP(ssid, password);
    while (WiFiMulti.run() != WL_CONNECTED) {
        delay(100);
    }
    USE_SERIAL.printf("[SETUP] WiFi Connected %s\n", WiFi.localIP().toString().c_str());
    
    socketIO.beginSSL(wsHost, wsPort, wsPath, "arduino");
    socketIO.onEvent(socketIOEvent);
}

void loop() {
    socketIO.loop();
    processTasks();
}