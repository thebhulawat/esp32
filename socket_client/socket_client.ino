#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>

// WiFi credentials
const char* ssid = "TP-Link_5B38";
const char* password = "83110122";

//ngrok WebSocket details
const char* wsHost = "4bfd-49-205-32-225.ngrok-free.app";
const uint16_t wsPort = 443;  // ngrok uses secure WebSocket (wss://) which runs on port 443
const char* wsPath = "/socket.io/?EIO=4&transport=websocket";

// const char* wsHost = "192.168.0.101";  // Your Mac's IP 
// const uint16_t wsPort = 8080;
// const char* wsPath = "/";

WebSocketsClient webSocket;

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println("[WSc] Disconnected!");
            break;
        case WStype_CONNECTED:
            Serial.println("[WSc] Connected!");
            break;
        case WStype_TEXT:
            Serial.printf("[WSc] Received text: %s\n", payload);
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(5000);  // Initial 5 second delay
    
    // Connect to WiFi
    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected");
    
    // Configure WebSocket client
    webSocket.beginSSL(wsHost, wsPort, wsPath);  // Use SSL for ngrok
    //webSocket.begin(wsHost, wsPort, wsPath);
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);
}

void loop() {
    webSocket.loop();
}