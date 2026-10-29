#include <esp_sleep.h>

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Set the touch threshold
  touchSleepWakeUpEnable(4, 40);

  Serial.println("Touch GPIO 4 to wake up");


  // Enter deep sleep
  esp_deep_sleep_start();
}

void loop() {
  Serial.println("I am awake");
}
