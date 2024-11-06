#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <Base64.h>
//#include "driver/i2s.h"
#include <ESP_I2S.h>

#define USE_SERIAL Serial

// Audio configuration
#define SAMPLE_RATE 16000U
#define BUFFER_SIZE 3200
#define VOLUME_GAIN 2
#define GAIN 2.0            // Amplification factor


// I2S Pins
#define PDM_CLK_PIN 42
#define PDM_DATA_PIN 41

// I2S output pins 
#define I2S_WS   44    // LRCLK (Word Select)
#define I2S_BCK  7    // Bit Clock
#define I2S_DATA 8    // I2S Data


WiFiMulti WiFiMulti;
SocketIOclient socketIO;
I2SClass I2S;
I2SClass I2SOut;

// WebSocket configuration
const char* ssid = "TP-Link_5B38";         
const char* password = "83110122";     
const char* wsHost = "9526-49-205-34-186.ngrok-free.app";
const uint16_t wsPort = 443;
const char* wsPath = "/socket.io/?EIO=4";

// Audio buffers
int16_t* audioBuffer = NULL;
uint8_t* transmitBuffer = NULL;
int16_t* receiveBuffer = NULL;

// State management
bool isConnected = false;
bool isRecording = false;
bool conversationStarted = false;


void initI2S() {
    USE_SERIAL.println("Initializing I2S for input...");
    
    I2S.end();  // End any existing I2S operations
    I2S.setPinsPdmRx(PDM_CLK_PIN, PDM_DATA_PIN);
    
    if (!I2S.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        USE_SERIAL.println("Failed to initialize I2S input!");
        return;
    }
    
    delay(100);
    USE_SERIAL.println("I2S initialized successfully for input");
    
    // Initialize second I2S instance for output
    USE_SERIAL.println("Initializing I2S for output...");
    
    I2SOut.end();  // End any existing operations on output instance
    
    // Set the output pins
    I2SOut.setPins(I2S_BCK, I2S_WS, I2S_DATA);
    
    // Initialize I2S output
    if (!I2SOut.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
        USE_SERIAL.println("Failed to initialize I2S output!");
        return;
    }
    
    delay(100);
    USE_SERIAL.println("I2S initialized successfully for output");
}

void amplifyBuffer(int16_t *buffer, size_t size, float gain) {
    for (size_t i = 0; i < size; i++) {
        int32_t sample = buffer[i] * gain;
        // Clip the sample to prevent overflow
        buffer[i] = (sample > INT16_MAX) ? INT16_MAX : (sample < INT16_MIN) ? INT16_MIN : sample;
    }
}

void playReceivedAudio(const uint8_t* data, size_t length) {
    // Calculate number of samples, ensuring we don't overflow the buffer
    size_t samplesRead = length / sizeof(int16_t);
    // if (samplesRead > BUFFER_SIZE) {
    //     samplesRead = BUFFER_SIZE;  // Limit to buffer size
    // }

    uint8_t *byteBuffer = (uint8_t *)data;
    size_t bytesToWrite = samplesRead * sizeof(int16_t);  
    size_t bytesWritten = 0;
        
    while (bytesWritten < bytesToWrite) {
        size_t written = I2SOut.write(byteBuffer + bytesWritten, 
                                    bytesToWrite - bytesWritten);
        if (written == 0) {
            break;  // Error or buffer full
        }
        bytesWritten += written;
    }
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
    size_t bytesRead = I2S.readBytes((char*)audioBuffer, BUFFER_SIZE * sizeof(int16_t));
    if (bytesRead > 0) {
        processAudio(audioBuffer, BUFFER_SIZE);
        memcpy(transmitBuffer, audioBuffer, bytesRead);
        
        // Debug output every 100 buffers
        static int bufferCount = 0;
        if (++bufferCount % 100 == 0) {
            USE_SERIAL.printf("Buffer #%d, Bytes read: %d\n", bufferCount, bytesRead);
        }
    }
    return bytesRead;
}

void sendStartConversationEvent() {
    if (!isConnected) return;
    
    DynamicJsonDocument doc(1024);
    JsonArray array = doc.to<JsonArray>();
    array.add("startConversation");
    JsonObject params = array.createNestedObject();
    params["assistantId"] = "688247b8-9476-4fc7-b5f2-5818524622ec";
    
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

void initBuffers() {
    // Allocate buffers in PSRAM
    audioBuffer = (int16_t*)ps_malloc(BUFFER_SIZE * sizeof(int16_t));
    transmitBuffer = (uint8_t*)ps_malloc(BUFFER_SIZE * 2);
    receiveBuffer = (int16_t*)ps_malloc(BUFFER_SIZE * sizeof(int16_t));  // New receive buffer
    
    if (!audioBuffer || !transmitBuffer || !receiveBuffer) {
        USE_SERIAL.println("Failed to allocate audio buffers!");
        return;
    }
    
    USE_SERIAL.println("Audio buffers allocated successfully");
}

void cleanupBuffers() {
    if (audioBuffer) free(audioBuffer);
    if (transmitBuffer) free(transmitBuffer);
    if (receiveBuffer) free(receiveBuffer);
    
    audioBuffer = NULL;
    transmitBuffer = NULL;
    receiveBuffer = NULL;
}

void processJsonAudioArray(JsonArray& array) {
    // Create a temporary buffer to hold the audio data
    size_t arraySize = array.size();
    if (arraySize == 0) return;
    
    // Clear the receive buffer
    memset(receiveBuffer, 0, BUFFER_SIZE * sizeof(int16_t));
    
    // Copy data from JsonArray to receiveBuffer
    size_t sampleCount = min(arraySize, (size_t)BUFFER_SIZE);
    for (size_t i = 0; i < sampleCount; i++) {
        // Get each sample from the JsonArray and convert to int16_t
        receiveBuffer[i] = (int16_t)array[i].as<int>();
    }
    
    // Play the audio using the receiveBuffer
    playReceivedAudio((uint8_t*)receiveBuffer, sampleCount * sizeof(int16_t));
}


void socketIOEvent(socketIOmessageType_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case sIOtype_DISCONNECT:
            USE_SERIAL.printf("[IOc] Disconnected!\n");
            isConnected = false;
            conversationStarted = false;
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
                    }
                }
                
                else if (eventName == "sendAudioToClient") {
                    USE_SERIAL.println("Received audio from server");
                    // Extract the raw audio buffer from the event
                    JsonVariant audioData = doc[1];
                    if (audioData.is<JsonArray>()) {
                        JsonArray audioArray = doc[1].as<JsonArray>();
                        if (!audioArray.isNull()) {
                            processJsonAudioArray(audioArray);
                            // After processing audio, properly end the conversation
                            isRecording = false;
                            conversationStarted = false;
                            USE_SERIAL.println("Audio playback completed");
                        }
                    }
                }
            }
            break;
    }
}


void setup() {
    USE_SERIAL.begin(115200);
    USE_SERIAL.setDebugOutput(true);
    delay(1000);
    
    USE_SERIAL.println("\nStarting up...");
    USE_SERIAL.println("Type 'r' to start recording, 's' to stop...");
    
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
            USE_SERIAL.println("Ending conversation...");
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