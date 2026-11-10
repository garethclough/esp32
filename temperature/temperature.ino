#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <time.h>
#include "esp_sntp.h"
#include <DHT.h>

#define DHT_PIN 4
#define DHT_TYPE DHT11
#define DURATION_HIGH_MIN 40
#define RECORD_LIST_START 8
#define EEPROM_SIZE 4096
// change this to reset eeprom
#define MAGIC_NUMBER 12345678

struct Reading {
    float temperature;
    float humidity;
    unsigned long timestamp;
};

String ssid = "Livebox6-419D";
String password = "63FTosLRGK5X";
String ntpServer = "pool.ntp.org";
int currentEepromAddress = 0;
int recordCount = 0;
DHT dht(DHT_PIN, DHT_TYPE);

float temperature = 0.0;
float humidity = 0.0;
bool readingError = false;
bool readingSuccess = false;
float lastTemperature = 0;
float lastHumidity = 0;

WebServer server(80);

// volatile tells compiler to always read value from memory
volatile unsigned long edgeDuration[100];
volatile byte edgeLevel[100];
volatile int edgeCount = 0;
volatile unsigned long lastEdgeTime = 0;

bool ntpSynced = false;

void timeSyncCallback(struct timeval *tv) {
    ntpSynced = true;
    Serial.println("NTP synchronisation successful!");
}

void synchroniseTime() {
  // connects to remote server
  // Register callback BEFORE starting NTP
  esp_sntp_set_time_sync_notification_cb(timeSyncCallback);
  configTzTime("GMT0BST,M3.5.0/1,M10.5.0", ntpServer.c_str());
  Serial.println("Waiting for NTP synchronisation...");
  Serial.println("Time synchronised!");

  int attempts = 0;

  while (!ntpSynced && attempts < 20) {
      delay(1000);
      attempts++;
  }

  if (ntpSynced) {
      Serial.println("NTP confirmed.");
  } else {
      Serial.println("NTP synchronisation failed.");
  }  
}

void IRAM_ATTR recordEdge() {
    unsigned long now = micros();

    if (edgeCount < 100) {
        edgeDuration[edgeCount] = now - lastEdgeTime;
        edgeLevel[edgeCount] = digitalRead(DHT_PIN);
        edgeCount++;
    }

    lastEdgeTime = now;
}

void handleHomepage() {

    String html = "<html><body>";

    html += "<h1>ESP32 Sensor Monitor</h1>";

    if (readingSuccess) {
      html += "<p>Temperature: ";
      html += String(temperature, 1);
      html += " &deg;C</p>";

      html += "<p>Humidity: ";
      html += String(humidity, 1);
      html += " %</p>";
    } else {
      html += "<p>No successfull reading obtained.<p>";
    }

    if (readingError) {
      html += "<p>Warning: Last reading failed.</p>";
    }

    html += "</body></html>";

    server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);

	unsigned int magic;
	EEPROM.get(0, magic);

	if (magic != MAGIC_NUMBER) {
			// First run: initialise EEPROM
			Serial.println("Initialising EEPROM");

			currentEepromAddress = RECORD_LIST_START;
			recordCount = 0;

			EEPROM.put(0, (unsigned int)MAGIC_NUMBER);
			EEPROM.put(4, currentEepromAddress);
			EEPROM.put(6, recordCount);
			EEPROM.commit();
	} else {
			// Previously initialised: retrieve saved values
			Serial.println("Loading EEPROM data");
			EEPROM.get(4, currentEepromAddress);
			EEPROM.get(6, recordCount);
	}



	Serial.println("Connecting to WiFi...");
	WiFi.begin(ssid, password);

	while (WiFi.status() != WL_CONNECTED) {
			delay(500);
			Serial.print(".");
	}

	Serial.println();
	Serial.println("Connected!");

	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());

  synchroniseTime();

	server.on("/", handleHomepage);
	server.begin();
	Serial.println("Web server started");

  dht.begin();
}

// if x = 20 and y = 2 return 20.3, if y = 33 return 20.33 etc.
float convertToFloat(byte x, byte y) {
    if (y == 0) {
        return x;
    }

    int digits = (int)log10(y) + 1;

    return x + y / pow(10, digits);
}

void saveReading(float temperature, float humidity) {

    struct tm timeinfo;

    if (getLocalTime(&timeinfo)) {
        unsigned long timestamp = (unsigned long)mktime(&timeinfo);
        Serial.println(&timeinfo, "%d/%m/%Y %H:%M:%S");

      Reading currentReading;

      currentReading.temperature = temperature;
      currentReading.humidity = humidity;
      currentReading.timestamp = timestamp;

			EEPROM.put(currentEepromAddress, currentReading);

			// loop round and overwrite old records
			currentEepromAddress += sizeof(currentReading);
			if (currentEepromAddress + sizeof(currentEepromAddress) > EEPROM_SIZE) {
				currentEepromAddress = RECORD_LIST_START;
			}
			recordCount++;

			EEPROM.put(4, recordCount);
			EEPROM.put(6, currentEepromAddress);

			currentEepromAddress += sizeof(Reading);
      EEPROM.commit();
    }    
}

void printSavedReading() {
    Reading previousReading;

    EEPROM.get(0, previousReading);
    Serial.println("Previous Reading");
    Serial.print("Temperature: ");
    Serial.println(previousReading.temperature);

    Serial.print("Humidity: ");
    Serial.println(previousReading.humidity);

    Serial.print("Timestamp: ");
    Serial.println(previousReading.timestamp);    
}

bool getReadings(float &temperature, float &pressure) {

  // Send start signal
  pinMode(DHT_PIN, OUTPUT);
  digitalWrite(DHT_PIN, LOW);
  delay(20);

  // Release the data line
  pinMode(DHT_PIN, INPUT_PULLUP);

  edgeCount = 0;
  lastEdgeTime = micros();

  // Record every change of the data signal
  attachInterrupt(
    digitalPinToInterrupt(DHT_PIN),
    recordEdge,
    CHANGE
  );

  // Allow time for the sensor to transmit
  delay(6);
  detachInterrupt(digitalPinToInterrupt(DHT_PIN));

  byte data[5] = {0, 0, 0, 0, 0};
  int bitCount = 0;

  // note that the first 4 bits are the initial response pulses
  for (int i = 4; i < edgeCount && bitCount < 40; i++) {

      // Only process HIGH pulse durations
      if (edgeLevel[i] == LOW) {

          int bitValue =
              edgeDuration[i] > DURATION_HIGH_MIN ? 1 : 0;

          int byteIndex = bitCount / 8;
          int bitIndex = 7 - (bitCount % 8);

          if (bitValue == 1) {
              data[byteIndex] |= (1 << bitIndex);
          }

          bitCount++;
      }
  }

  Serial.print("Bits received: ");
  Serial.println(bitCount);

  for (int i = 0; i < 5; i++) {
    Serial.print(i);
    Serial.print(": ");
    Serial.println(data[i]);
  }

  temperature = convertToFloat(data[2], data[3]);
  pressure = convertToFloat(data[0], data[1]);

  byte calculatedChecksum = data[0] + data[1] + data[2] + data[3];
  if (calculatedChecksum == data[4]) {
      Serial.println("Data is valid");
  } else {
      Serial.println("Checksum error");
      return false;
  }  
  return true;
}

void loop() {

    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();

    if (isnan(humidity) || isnan(temperature)) {
        Serial.println("Failed to read sensor!");
    } else {
        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.println(" °C");

        Serial.print("Humidity: ");
        Serial.print(humidity);
        Serial.println(" %");
    }

    delay(2000);

/*
  server.handleClient();
  readingError = !getReadings(temperature, humidity);
  if (!readingError) {
    readingSuccess = true;
    Serial.print("Temp: ");
    Serial.println(temperature);
    Serial.print("Humidity: ");
    Serial.println(humidity);
    saveReading(temperature, humidity);
  } else {
    Serial.println("Sensor reading failed.");
  }

  delay(20000);
  */
}
