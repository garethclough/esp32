const int touchPin = 4;

void setup() {
  Serial.begin(115200);
}

void loop() {
  int value = touchRead(touchPin);

  Serial.println(value);

  delay(100);
}