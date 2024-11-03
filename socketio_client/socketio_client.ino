#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>

const char* ssid = "TP-Link_5B38";
const char* password = "83110122";

// Local 
const char* wsHost = "192.168.0.101";  
const uint16_t wsPort = 3000;
const char* wsPath = "/socket.io/?EIO=4";

// Ngrok configuration
// const char* wsHost = "a7e4-49-205-34-186.ngrok-free.app";
// const uint16_t wsPort = 443;  // Always use 443 for secure WebSocket with ngrok
// const char* wsPath = "/socket.io/?EIO=4";

SocketIOclient socketIO;
WiFiClientSecure client;

void socketIOEvent(socketIOmessageType_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case sIOtype_DISCONNECT:
            Serial.println("[IOc] Disconnected!");
            break;
        case sIOtype_CONNECT:
            Serial.println("[IOc] Connected!");
            // Send a test event after connection
            socketIO.sendEVENT("[\"test\",{\"message\":\"Connected from ESP32!\"}]");
            break;
        case sIOtype_EVENT:
            Serial.print("[IOc] Got event: ");
            Serial.write(payload, length);
            Serial.println();
            break;
        case sIOtype_ERROR:
            Serial.print("[IOc] Got error: ");
            Serial.write(payload, length);
            Serial.println();
            break;
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\nStarting...");
    
    // Connect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Important: Disable SSL verification for ngrok
   // client.setInsecure();
    
    // Configure Socket.IO client with more debugging
    socketIO.setReconnectInterval(5000);
    
    
    // Begin Socket.IO connection
    //socketIO.beginSSL(wsHost, wsPort, wsPath, "arduino");
    socketIO.begin(wsHost, wsPort, wsPath, "arduino");
    socketIO.onEvent(socketIOEvent);
    
    Serial.println("Socket.IO setup complete");
}

void loop() {
    socketIO.loop();
    
    static unsigned long lastPing = 0;
    if (millis() - lastPing > 10000) {
        lastPing = millis();
        
        // Send a ping message to keep connection alive
        socketIO.sendEVENT("[\"ping\",{}]");
        Serial.println("Sent ping");
        
        // Print connection status
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi still connected");
            Serial.print("Signal strength (RSSI): ");
            Serial.println(WiFi.RSSI());
        }
    }
}