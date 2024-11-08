#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <ESP_I2S.h>
#include <Base64.h>

#define USE_SERIAL Serial

// Audio configuration
#define SAMPLE_RATE 16000U
#define SAMPLE_BITS 16
#define VOLUME_GAIN 2
#define RX_BUFFER_SIZE 3200  // For recording
#define TX_BUFFER_SIZE 2048  // For playback
#define BUFFER_COUNT 8       // Number of playback buffers

// I2S pins
#define I2S_BCK  7          // Playback pins
#define I2S_WS   44
#define I2S_DATA 8
#define PDM_CLK_PIN 42      // Recording pins
#define PDM_DATA_PIN 41

WiFiMulti WiFiMulti;
SocketIOclient socketIO;
I2SClass I2S_TX;  // For playback
I2SClass I2S_RX;  // For recording

// WebSocket configuration
const char* ssid = "TP-Link_5B38";         
const char* password = "83110122";     
const char* wsHost = "6496-49-205-35-178.ngrok-free.app";
const uint16_t wsPort = 443;
const char* wsPath = "/socket.io/?EIO=4";

// Audio buffers for recording
int16_t* recordBuffer = NULL;
uint8_t* transmitBuffer = NULL;

// Audio buffers for playback
struct AudioBuffer {
    uint8_t* data;
    size_t size;
    bool ready;
};

AudioBuffer playbackBuffers[BUFFER_COUNT];
int currentPlayBuffer = 0;
int currentFillBuffer = 0;

// State management
bool isConnected = false;
bool isRecording = false;
bool conversationStarted = false;

void initI2S() {
    USE_SERIAL.println("Initializing I2S...");
    
    // Initialize I2S for playback (TX)
    I2S_TX.end();
    I2S_TX.setPins(I2S_BCK, I2S_WS, I2S_DATA);
    if (!I2S_TX.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        USE_SERIAL.println("Failed to initialize I2S TX!");
        return;
    }
    
    // Initialize I2S for recording (RX)
    I2S_RX.end();
    I2S_RX.setPinsPdmRx(PDM_CLK_PIN, PDM_DATA_PIN);
    if (!I2S_RX.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        USE_SERIAL.println("Failed to initialize I2S RX!");
        return;
    }
    
    delay(100);
    USE_SERIAL.println("I2S initialized successfully");
}

void initBuffers() {
    // Allocate recording buffers in PSRAM
    recordBuffer = (int16_t*)ps_malloc(RX_BUFFER_SIZE * sizeof(int16_t));
    transmitBuffer = (uint8_t*)ps_malloc(RX_BUFFER_SIZE * 2);
    
    // Allocate playback buffers
    for (int i = 0; i < BUFFER_COUNT; i++) {
        playbackBuffers[i].data = (uint8_t*)malloc(TX_BUFFER_SIZE);
        playbackBuffers[i].size = 0;
        playbackBuffers[i].ready = false;
        if (!playbackBuffers[i].data) {
            USE_SERIAL.printf("Failed to allocate playback buffer %d\n", i);
        }
    }
    
    if (!recordBuffer || !transmitBuffer) {
        USE_SERIAL.println("Failed to allocate recording buffers!");
        return;
    }
    
    USE_SERIAL.println("Audio buffers allocated successfully");
}

void cleanupBuffers() {
    // Clean up recording buffers
    if (recordBuffer) free(recordBuffer);
    if (transmitBuffer) free(transmitBuffer);
    recordBuffer = NULL;
    transmitBuffer = NULL;
    
    // Clean up playback buffers
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (playbackBuffers[i].data) {
            free(playbackBuffers[i].data);
            playbackBuffers[i].data = NULL;
        }
    }
}

void processRecordedAudio(int16_t* buffer, size_t length) {
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

void amplifyPlaybackBuffer(int16_t* buffer, size_t size) {
    for (size_t i = 0; i < size/2; i++) {
        int32_t sample = buffer[i] * VOLUME_GAIN;
        buffer[i] = (sample > INT16_MAX) ? INT16_MAX : (sample < INT16_MIN) ? INT16_MIN : sample;
    }
}

size_t readAndProcessAudio() {
    size_t bytesRead = I2S_RX.readBytes((char*)recordBuffer, RX_BUFFER_SIZE * sizeof(int16_t));
    if (bytesRead > 0) {
        processRecordedAudio(recordBuffer, RX_BUFFER_SIZE);
        memcpy(transmitBuffer, recordBuffer, bytesRead);
        
        static int bufferCount = 0;
        if (++bufferCount % 100 == 0) {
            USE_SERIAL.printf("Record Buffer #%d, Bytes read: %d\n", bufferCount, bytesRead);
        }
    }
    return bytesRead;
}

void playAudioBuffer(const uint8_t* data, size_t length) {
    size_t bytesWritten = 0;
    uint8_t *byteBuffer = (uint8_t *)data;
    while (bytesWritten < length) {
        size_t written = I2S_TX.write(byteBuffer + bytesWritten, length - bytesWritten);
        if (written == 0) {
            delay(1);
            continue;
        }
        bytesWritten += written;
    }
}

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
                DynamicJsonDocument doc(4096);
                DeserializationError error = deserializeJson(doc, payload, length);
                
                if (error) {
                    USE_SERIAL.print(F("deserializeJson() failed: "));
                    USE_SERIAL.println(error.f_str());
                    return;
                }
                
                String eventName = doc[0];
                
                // Handle incoming audio data
                if (eventName == "sendAudioToClient") {
                    JsonObject audioData = doc[1];
                    if (!audioData.isNull()) {
                        int chunkNumber = audioData["chunkNumber"];
                        const char* base64Data = audioData["data"];
                        
                        AudioBuffer* buffer = &playbackBuffers[currentFillBuffer];
                        if (!buffer->ready) {
                            size_t decodedLength = decode_base64(
                                (unsigned char*)base64Data, 
                                strlen(base64Data), 
                                buffer->data
                            );
                            
                            buffer->size = decodedLength;
                            buffer->ready = true;
                            
                            amplifyPlaybackBuffer((int16_t*)buffer->data, decodedLength);
                            currentFillBuffer = (currentFillBuffer + 1) % BUFFER_COUNT;
                            
                            if (chunkNumber % 10 == 0) {
                                USE_SERIAL.printf("Received chunk %d\n", chunkNumber);
                            }
                        }
                    }
                }
                // Handle conversation start response
                else if (eventName == "conversationStarted") {
                    JsonObject response = doc[1];
                    if (response["status"] == "success") {
                        USE_SERIAL.println("Conversation started successfully");
                        conversationStarted = true;
                        isRecording = true;
                    }
                }
            }
            break;
    }
}

void sendAudioData(size_t bytesRead) {
    if (!isConnected || !conversationStarted) return;
    
    String base64Data = base64::encode(transmitBuffer, bytesRead);
    
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
    
    isRecording = false;
    conversationStarted = false;
}

void processTasks() {
    // Handle playback
    AudioBuffer* buffer = &playbackBuffers[currentPlayBuffer];
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
    USE_SERIAL.println("Type 'r' to start recording, 's' to stop...");
    
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
    
    // Handle audio playback
    processTasks();
    
    // Small delay to prevent overwhelming the system
    delay(10);
}