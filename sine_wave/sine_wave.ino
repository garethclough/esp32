int counter = 0;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(1000);
}

void loop() {
  // 10 deg intervals
  float ang = (2 * M_PI / 36) * counter;
  float sine_1 = sin(ang);
  counter++;
  Serial.println(sine_1);
  delay(100);
}
