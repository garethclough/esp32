#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <time.h>
#include "esp_sntp.h"
#include <DHT.h>
#include "esp_task_wdt.h"
#include "esp_system.h"

#define DHT_PIN 4
#define DHT_TYPE DHT11
#define DURATION_HIGH_MIN 40
#define RECORD_LIST_START 13
#define EEPROM_SIZE 4096
#define MODE_PIN 5
#define EEPROM_MAGIC_NUMBER 0
#define EEPROM_MODE 4
#define EEPROM_NEXT_RECORD 5
#define EEPROM_RECORD_COUNT 9

// change this to reset eeprom
#define MAGIC_NUMBER 666671

struct Reading {
    float temperature;
    float humidity;
    unsigned long timestamp;
};

String ssid = "Livebox6-419D";
String password = "63FTosLRGK5X";
String ntpServer = "pool.ntp.org";
int currentEepromAddress = 0;
int previousButtonState = LOW;
int recordCount = 0;
DHT dht(DHT_PIN, DHT_TYPE);

float temperature = 0.0;
float humidity = 0.0;
bool readingError = false;
bool readingSuccess = false;
float lastTemperature = 0;
float lastHumidity = 0;
bool deepSleepMode = false;

unsigned long previousMillis = 0;
const unsigned long readingInterval = 10000; // 10 sec

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

void handleHomepage() {

    String html = "<html><body>";

    html += "<h1>ESP32 Sensor Monitor</h1>";

    Reading r[10]; 
    int numReadings = getPreviousReadings(r, 10, currentEepromAddress, recordCount);
    if (numReadings > 0) {
      html += "<p>Temperature: ";
      html += String(r[0].temperature, 1);
      html += " &deg;C</p>";
      html += "<p>Humidity: ";
      html += String(r[0].humidity, 1);
      html += " %</p>";
      time_t t = (time_t)r[0].timestamp;
      html += "<p>";
      html += ctime(&t);
      html += "</p>";

      if (numReadings > 1) {
        html += "<h4>Previous Readings:</h4><table cellpadding='5'>"; 
        html += "<tr><th>Time</th><th>Temperature</th><th>Humidity</th></tr>";   
        for (int i=1; i < numReadings; i++) {
          time_t t = (time_t)r[i].timestamp;
          String dateTime = String(ctime(&t));
          dateTime.trim();
          html += "<tr><td>" + dateTime + "</td><td>" + r[i].temperature + " &deg;C</td><td>" + r[i].humidity + "%</td></tr>";
        }
        html += "</table>";              
      }
    }
    if (readingError) {
      html += "<p>Warning: Last reading failed.</p>";
    }

    html += "</body></html>";

    server.send(200, "text/html", html);
}

void startDeepSleep() {
  pinMode(33, INPUT_PULLUP);
  esp_sleep_enable_timer_wakeup(20ULL * 1000000ULL); // set to 20s for demo (normally would be longer)
  // Wake when GPIO 33 goes LOW - with pull up its 1 when not connected and 0 if connected to grounf
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 0);
  esp_deep_sleep_start();     
}

void initWatchdog() {
  esp_task_wdt_config_t wdt_config = {
      .timeout_ms = 10000,
      .idle_core_mask = 0,
      .trigger_panic = true
  };

  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
}

void disableWatchdog() {
    esp_task_wdt_delete(NULL);
    esp_task_wdt_deinit();  
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);

	unsigned int magic;
	EEPROM.get(EEPROM_MAGIC_NUMBER, magic);

	if (magic != MAGIC_NUMBER) {
			// First run: initialise EEPROM
			Serial.println("Initialising EEPROM");

			currentEepromAddress = RECORD_LIST_START;
			recordCount = 0;
      EEPROM.put(EEPROM_MAGIC_NUMBER, (unsigned int)MAGIC_NUMBER);
      EEPROM.put(EEPROM_MODE, deepSleepMode);
			EEPROM.put(EEPROM_NEXT_RECORD, currentEepromAddress);
			EEPROM.put(EEPROM_RECORD_COUNT, recordCount);
			EEPROM.commit();
	} else {
			// Previously initialised: retrieve saved values
			Serial.println("Loading EEPROM data");
      EEPROM.get(EEPROM_MODE, deepSleepMode);
			EEPROM.get(EEPROM_NEXT_RECORD, currentEepromAddress);
			EEPROM.get(EEPROM_RECORD_COUNT, recordCount);
	}

  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  if (wakeupReason == ESP_SLEEP_WAKEUP_EXT0) {
      Serial.println("Woken by button press!");
      deepSleepMode = false;
      EEPROM.put(EEPROM_MODE, deepSleepMode);
      EEPROM.commit();
  }

  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT) {
      Serial.println("Watchdog restart detected!");
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

  if (deepSleepMode) {
    Serial.println("Deep Sleep Mode");
    takeReading();
    startDeepSleep();
  }

  // not executed in deep sleep mode
  pinMode(MODE_PIN, INPUT_PULLUP);
  previousButtonState = digitalRead(MODE_PIN);
  initWatchdog();
}

void saveReading(float temperature, float humidity) {
  Serial.println(temperature);
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
			currentEepromAddress += sizeof(Reading);
			if (currentEepromAddress + sizeof(Reading) > EEPROM_SIZE) {
				currentEepromAddress = RECORD_LIST_START;
			}
			recordCount++;

			EEPROM.put(EEPROM_RECORD_COUNT, recordCount);
			EEPROM.put(EEPROM_NEXT_RECORD, currentEepromAddress);
      EEPROM.commit();
    }    
}

int getPreviousReadings(Reading *r, int size, int recordAddress, int numRecords) {
  int counter = 0;
  int maxRecords = EEPROM_SIZE - RECORD_LIST_START /  sizeof(Reading);

  while (counter < numRecords && counter < size) {
    recordAddress -= sizeof(Reading);

    if (recordAddress < RECORD_LIST_START) {
      recordAddress = RECORD_LIST_START + ((maxRecords - 1) * sizeof(Reading));
    } 
    EEPROM.get(recordAddress, r[counter]);
    counter++;
  }
  return counter;
}

void takeReading() {
  humidity = dht.readHumidity();
  temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
      Serial.println("Failed to read sensor!");
      readingError = true;
  } else {
    // this means that is was successfull at least once 
    readingSuccess = true;
    readingError = false;
    /*
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" °C");

    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
*/
    saveReading(temperature, humidity);
/*
    Reading r[10]; 
    int numReadings = getPreviousReadings(r, 10, currentEepromAddress, recordCount);
    for (int i=0; i < numReadings; i++) {
      time_t t = (time_t)r[i].timestamp;
      Serial.print(ctime(&t));
      Serial.print(" - ");
      Serial.println(r[i].temperature);   
    }*/   
  }
}

void loop() {
  esp_task_wdt_reset();

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= readingInterval || previousMillis == 0) {
    previousMillis = currentMillis;

    Serial.println("Taking reading...");
    takeReading();
  }
  
  int currentState = digitalRead(MODE_PIN);

  if (currentState == HIGH && previousButtonState == LOW) {
    disableWatchdog();
    deepSleepMode = true;
    Serial.println("Entering Deep Sleep Mode");
    delay(100); // Allow serial message to print
    EEPROM.put(EEPROM_MODE, deepSleepMode);
    EEPROM.commit();
    // Wake after 2 mins
    startDeepSleep();
  }

  previousButtonState = currentState;

  server.handleClient();
  delay(100);
}
