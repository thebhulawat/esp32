#include <WiFi.h>
#include <WebSocketsServer.h>

const char* ssid = "Yashwant jio 4";
const char* password = "17051969";

WebSocketsServer webSocket = WebSocketsServer(81);

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
      }
      break;
    case WStype_TEXT:
      {
        Serial.printf("[%u] Received text: %s\n", num, payload);
        
        // Create a new string with the received payload
        String message = String((char*)payload);
        
        // Attach "naman" to the received message
        message += " naman";
        
        // Send the modified message back to the client
        webSocket.sendTXT(num, message);
        
        Serial.printf("[%u] Sent back: %s\n", num, message.c_str());
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started");
}

void loop() {
  webSocket.loop();
}
