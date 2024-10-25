#include "FS.h"
#include "SD.h"
#include "SPI.h"

// SD Card pins for ESP32
#define MOSI_PIN 23
#define MISO_PIN 19
#define SCK_PIN  18
#define CS_PIN   21

void setup() {
    Serial.begin(115200);
    delay(1000); // Give time for serial to connect
    
    Serial.println("\n=== SD Card Test ===");
    
    // Initialize SPI
    Serial.println("\n1. Testing SPI Connection...");
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
    Serial.println("SPI Pins:");
    Serial.printf("- MOSI: GPIO%d\n", MOSI_PIN);
    Serial.printf("- MISO: GPIO%d\n", MISO_PIN);
    Serial.printf("- SCK:  GPIO%d\n", SCK_PIN);
    Serial.printf("- CS:   GPIO%d\n", CS_PIN);
    
    // Try to mount SD card
    Serial.println("\n2. Mounting SD Card...");
    if (!SD.begin(CS_PIN)) {
        Serial.println("Mount Failed! Check:");
        Serial.println("- SD card inserted?");
        Serial.println("- Wiring correct?");
        return;
    }
    Serial.println("Mount Successful!");
    
    // Check card type
    Serial.println("\n3. Card Type:");
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No card found!");
        return;
    } else if (cardType == CARD_MMC) {
        Serial.println("MMC Card");
    } else if (cardType == CARD_SD) {
        Serial.println("SDSC Card");
    } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC Card");
    } else {
        Serial.println("Unknown Card");
    }
    
    // Print card size
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("\n4. Card Size: %lluMB\n", cardSize);
    
    // Simple file test
    Serial.println("\n5. Testing File Write/Read:");
    
    // Write test
    File file = SD.open("/test.txt", FILE_WRITE);
    if(!file) {
        Serial.println("- Failed to open file for writing");
        return;
    }
    if(file.println("Hello SD Card!")) {
        Serial.println("- Write: Success");
    } else {
        Serial.println("- Write: Failed");
    }
    file.close();
    
    // Read test
    file = SD.open("/test.txt");
    if(!file) {
        Serial.println("- Failed to open file for reading");
        return;
    }
    Serial.println("- Read: ");
    while(file.available()) {
        Serial.write(file.read());
    }
    file.close();
    
    Serial.println("\nTest Complete!");
}

void loop() {
    // Nothing to do here
}