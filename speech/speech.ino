const int SPEAKER_PIN = 9;  // GPIO9 for the speaker
const int FREQUENCY = 440;  // 440 Hz tone (A4 note)

void setup() {
  Serial.begin(115200);
  pinMode(SPEAKER_PIN, OUTPUT);
}

void loop() {
  Serial.println("Playing tone");
  playTone(1000);  // Play tone for 1 second

  Serial.println("Stopping tone");
  noTone();        // Stop the tone
  delay(1000);     // Pause for 1 second
}

void playTone(unsigned long duration) {
  unsigned long startTime = millis();
  while (millis() - startTime < duration) {
    digitalWrite(SPEAKER_PIN, HIGH);
    delayMicroseconds(1000000 / FREQUENCY / 2);
    digitalWrite(SPEAKER_PIN, LOW);
    delayMicroseconds(1000000 / FREQUENCY / 2);
  }
}

void noTone() {
  digitalWrite(SPEAKER_PIN, LOW);
}