#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <ESP_I2S.h>
#include <Base64.h>
#include "FS.h"
#include "SPIFFS.h"

#define FORMAT_SPIFFS_IF_FAILED true
#define USE_SERIAL Serial

// Audio configuration
#define SAMPLE_RATE 16000U
#define SAMPLE_BITS 16
#define VOLUME_GAIN 2
#define BUFFER_SIZE 2048  // Smaller buffer for real-time streaming
#define PDM_CLK_PIN 42
#define PDM_DATA_PIN 41

WiFiMulti WiFiMulti;
SocketIOclient socketIO;
I2SClass I2S;

// WebSocket configuration
const char* ssid = "TP-Link_5B38";         
const char* password = "83110122";     
const char* wsHost = "9526-49-205-34-186.ngrok-free.app";
const uint16_t wsPort = 443;
const char* wsPath = "/socket.io/?EIO=4";

// Audio buffers
int16_t* audioBuffer = NULL;
uint8_t* transmitBuffer = NULL;

// State management
bool isConnected = false;
bool isRecording = false;
bool conversationStarted = false;

// WAV file handling
File wavFile;
const char* WAV_FILENAME = "/recording.wav";
size_t totalBytesWritten = 0;

struct WavHeader {
    char riff_header[4] = {'R', 'I', 'F', 'F'};
    uint32_t wav_size;
    char wave_header[4] = {'W', 'A', 'V', 'E'};
    char fmt_header[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_chunk_size = 16;
    uint16_t audio_format = 1;
    uint16_t num_channels = 1;
    uint32_t sample_rate = SAMPLE_RATE;
    uint32_t byte_rate = SAMPLE_RATE * SAMPLE_BITS/8;
    uint16_t sample_alignment = SAMPLE_BITS/8;
    uint16_t bit_depth = SAMPLE_BITS;
    char data_header[4] = {'d', 'a', 't', 'a'};
    uint32_t data_bytes;
};

void initI2S() {
    USE_SERIAL.println("Initializing I2S...");
    
    I2S.end();
    I2S.setPinsPdmRx(PDM_CLK_PIN, PDM_DATA_PIN);
    
    if (!I2S.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        USE_SERIAL.println("Failed to initialize I2S!");
        return;
    }
    
    delay(100);
    USE_SERIAL.println("I2S initialized successfully");
}

void initBuffers() {
    // Allocate buffers in PSRAM
    audioBuffer = (int16_t*)ps_malloc(BUFFER_SIZE * sizeof(int16_t));
    transmitBuffer = (uint8_t*)ps_malloc(BUFFER_SIZE * 2);
    
    if (!audioBuffer || !transmitBuffer) {
        USE_SERIAL.println("Failed to allocate audio buffers!");
        return;
    }
    
    USE_SERIAL.println("Audio buffers allocated successfully");
}

void cleanupBuffers() {
    if (audioBuffer) free(audioBuffer);
    if (transmitBuffer) free(transmitBuffer);
    
    audioBuffer = NULL;
    transmitBuffer = NULL;
}

void createWavFile() {
    if (SPIFFS.exists(WAV_FILENAME)) {
        SPIFFS.remove(WAV_FILENAME);
    }
    
    wavFile = SPIFFS.open(WAV_FILENAME, FILE_WRITE);
    if (!wavFile) {
        USE_SERIAL.println("Failed to open WAV file for writing");
        return;
    }
    
    WavHeader header;
    header.data_bytes = 0;  // Will be updated later
    header.wav_size = 0;    // Will be updated later
    wavFile.write((const uint8_t*)&header, sizeof(WavHeader));
    totalBytesWritten = 0;
}

void finalizeWavFile() {
    if (!wavFile) return;
    
    // Update WAV header with final sizes
    WavHeader header;
    header.data_bytes = totalBytesWritten;
    header.wav_size = totalBytesWritten + sizeof(WavHeader) - 8;
    
    wavFile.seek(0);
    wavFile.write((const uint8_t*)&header, sizeof(WavHeader));
    wavFile.close();
    
    USE_SERIAL.printf("WAV file saved. Total size: %u bytes\n", totalBytesWritten + sizeof(WavHeader));
}

void processAudio(int16_t* buffer, size_t length) {
    // Apply volume gain
    for (size_t i = 0; i < length; i++) {
        buffer[i] = buffer[i] << VOLUME_GAIN;
    }
    
    // Simple noise gate
    const int16_t threshold = 500;
    for (size_t i = 0; i < length; i++) {
        if (abs(buffer[i]) < threshold) {
            buffer[i] = 0;
        }
    }
}

size_t readAndProcessAudio() {
    // Fix: Cast the buffer to char* as required by I2S.readBytes
    size_t bytesRead = I2S.readBytes((char*)audioBuffer, BUFFER_SIZE * sizeof(int16_t));
    if (bytesRead > 0) {
        processAudio(audioBuffer, BUFFER_SIZE);
        
        // Copy to transmit buffer
        memcpy(transmitBuffer, audioBuffer, bytesRead);
        
        // Write to WAV file if recording
        if (wavFile) {
            wavFile.write(transmitBuffer, bytesRead);
            totalBytesWritten += bytesRead;
        }
        
        // Debug output every 100 buffers
        static int bufferCount = 0;
        if (++bufferCount % 100 == 0) {
            USE_SERIAL.printf("Buffer #%d, Bytes read: %d, Total written: %d\n", 
                            bufferCount, bytesRead, totalBytesWritten);
        }
    }
    return bytesRead;
}

void socketIOEvent(socketIOmessageType_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case sIOtype_DISCONNECT:
            USE_SERIAL.printf("[IOc] Disconnected!\n");
            isConnected = false;
            conversationStarted = false;
            cleanupBuffers(); 
            break;
            
        case sIOtype_CONNECT:
            USE_SERIAL.printf("[IOc] Connected to url: %s\n", payload);
            socketIO.send(sIOtype_CONNECT, "/");
            isConnected = true;
            break;
            
        case sIOtype_EVENT:
            {
                USE_SERIAL.printf("[IOc] got event: %s\n", payload);
                
                DynamicJsonDocument doc(1024);
                DeserializationError error = deserializeJson(doc, payload, length);
                if (error) {
                    USE_SERIAL.println(F("deserializeJson() failed"));
                    return;
                }
                
                String eventName = doc[0];
                if (eventName == "conversationStarted") {
                    JsonObject response = doc[1];
                    if (response["status"] == "success") {
                        USE_SERIAL.println("Conversation started successfully");
                        conversationStarted = true;
                        isRecording = true;
                        createWavFile();
                    }
                }
            }
            break;
    }
}

void sendAudioData(size_t bytesRead) {
    if (!isConnected || !conversationStarted) return;
    
    // Convert to base64
    String base64Data = base64::encode(transmitBuffer, bytesRead);
    
    // Prepare and send socket.io event
    DynamicJsonDocument doc(1024 + bytesRead);
    JsonArray array = doc.to<JsonArray>();
    array.add("sendAudioToServer");
    array.add(base64Data);
    
    String output;
    serializeJson(doc, output);
    socketIO.sendEVENT(output);
}

void sendStartConversationEvent() {
    if (!isConnected) return;
    
    DynamicJsonDocument doc(1024);
    JsonArray array = doc.to<JsonArray>();
    array.add("startConversation");
    JsonObject params = array.createNestedObject();
    params["assistantId"] = "c47eec22-e20f-43c3-ba90-40c4b97f6e53";
    
    String output;
    serializeJson(doc, output);
    socketIO.sendEVENT(output);
    USE_SERIAL.println("Sent startConversation event");
}

void sendEndConversationEvent() {
    if (!isConnected) return;
    
    DynamicJsonDocument doc(1024);
    JsonArray array = doc.to<JsonArray>();
    array.add("endConversation");
    
    String output;
    serializeJson(doc, output);
    socketIO.sendEVENT(output);
    USE_SERIAL.println("Sent endConversation event");
    
    isRecording = false;
    conversationStarted = false;
    finalizeWavFile();
}

void setup() {
    USE_SERIAL.begin(115200);
    USE_SERIAL.setDebugOutput(true);
    delay(1000);
    
    USE_SERIAL.println("\nStarting up...");
    USE_SERIAL.println("Type 'r' to start recording, 's' to stop...");
    
    // Initialize SPIFFS
    if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
        USE_SERIAL.println("SPIFFS Mount Failed");
        return;
    }
    
    // Initialize audio buffers
    initBuffers();
    
    // Initialize I2S
    initI2S();
    
    // Connect to WiFi
    WiFiMulti.addAP(ssid, password);
    while (WiFiMulti.run() != WL_CONNECTED) {
        delay(100);
    }
    USE_SERIAL.printf("[SETUP] WiFi Connected %s\n", WiFi.localIP().toString().c_str());
    
    // Initialize Socket.IO connection
    socketIO.beginSSL(wsHost, wsPort, wsPath, "arduino");
    socketIO.onEvent(socketIOEvent);
}

void loop() {
    socketIO.loop();
    
    // Check for serial commands
    if (USE_SERIAL.available() > 0) {
        char cmd = USE_SERIAL.read();
        if (cmd == 'r' && !isRecording) {
            USE_SERIAL.println("Starting conversation and recording...");
            sendStartConversationEvent();
        } else if (cmd == 's' && isRecording) {
            USE_SERIAL.println("Ending conversation and saving recording...");
            sendEndConversationEvent();
        }
    }
    
    // Handle audio recording and transmission
    if (isRecording && conversationStarted) {
        size_t bytesRead = readAndProcessAudio();
        if (bytesRead > 0) {
            sendAudioData(bytesRead);
        }
    }
    
    // Add a small delay to prevent overwhelming the system
    delay(10);
}