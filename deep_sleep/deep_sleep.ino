#include <esp_sleep.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  // Despertar después de 10 segundos - in microseconds
  esp_sleep_enable_timer_wakeup(10 * 1000000ULL);

  // Entrar en sueño profundo
  esp_deep_sleep_start();
}

void loop() {
  // No se ejecuta mientras está en sueño profundo
  Serial.print("I am awake!");
  delay(500);
}
