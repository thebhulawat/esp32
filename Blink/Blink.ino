#include "driver/ledc.h"

#define PWM_PIN 43           // GPIO pin for PWM
#define PWM_FREQUENCY 16000  // 16 kHz frequency for the PWM
#define PWM_RESOLUTION 8     // 8-bit resolution (values between 0 and 255)
#define PWM_CHANNEL 0        // Use PWM channel 0

void setup() {
  // Attach PWM to the pin and configure the frequency and resolution in one step
  ledcAttach(PWM_PIN, PWM_FREQUENCY, PWM_RESOLUTION);
}

void loop() {
  // Set a 50% duty cycle
  ledcWrite(PWM_PIN, 128);  // 50% duty cycle (mid-point of 0-255)

  delay(1000);
  
  // Optionally vary the duty cycle to see if the LED dims and glows
  for (int duty = 0; duty <= 255; duty += 5) {
    ledcWrite(PWM_PIN, duty);
    delay(20);  // Adjust the delay to control how fast it dims and glows
  }
}
