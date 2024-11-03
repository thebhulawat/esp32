#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>

// Replace with your network credentials
// const char* ssid = "Yashwant jio 4";
// const char* password = "17051969";
const char* ssid = "TP-Link_5B38";
const char* password = "83110122";

#define FORMAT_SPIFFS_IF_FAILED true

// Create WebServer object on port 80
WebServer server(80);

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("Failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if(levels){
                listDir(fs, file.path(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

void handleRoot() {
    String html = "<html><body style='font-family: Arial, sans-serif; margin: 20px;'>";
    html += "<h1>ESP32 File Server</h1>";
    
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    html += "<h2>Available Files:</h2>";
    html += "<ul style='list-style-type: none; padding: 0;'>";
    
    while(file){
        if(!file.isDirectory()){
            String fileName = file.name();
            html += "<li style='margin: 10px 0; padding: 10px; background-color: #f0f0f0; border-radius: 5px;'>";
            html += "<strong>" + fileName + "</strong><br>";
            html += "Size: " + String(file.size()) + " bytes<br>";
            html += "<a href='" + fileName + "' style='color: #0066cc; text-decoration: none;'>";
            html += "Download File</a>";
            html += "</li>";
        }
        file = root.openNextFile();
    }
    
    html += "</ul></body></html>";
    server.send(200, "text/html", html);
}

void handleNotFound() {
    String path = server.uri();
    Serial.println("Requested path: " + path);
    
    if(SPIFFS.exists(path)) {
        File file = SPIFFS.open(path, "r");
        if(file) {
            String contentType = "application/octet-stream";
            
            // Set content type based on file extension
            if(path.endsWith(".wav")) contentType = "audio/wav";
            else if(path.endsWith(".mp3")) contentType = "audio/mpeg";
            else if(path.endsWith(".txt")) contentType = "text/plain";
            
            server.streamFile(file, contentType);
            file.close();
            return;
        }
    }
    
    server.send(404, "text/plain", "File Not Found");
}

void setup() {
    // Initialize Serial port
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }
    
    // Initialize SPIFFS
    if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
        Serial.println("SPIFFS Mount Failed");
        return;
    }

    // Print the files in SPIFFS
    listDir(SPIFFS, "/", 0);
    
    // Connect to Wi-Fi
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

    // Set up server routes
    server.on("/", HTTP_GET, handleRoot);
    server.onNotFound(handleNotFound);

    // Start server
    server.begin();
    Serial.println("HTTP server started");
}

void loop() {
    server.handleClient();
    delay(2); // Small delay to prevent watchdog timeout
}