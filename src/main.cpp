#include <Arduino.h>
#include <WiFiManager.h>
#include <time.h>
#include <Preferences.h>
#include <esp_sntp.h>

const char* ntpServer = "pool.ntp.org";

const int LED_PIN = 18;
const int RESET_PIN = 19;
const int RAISE_PIN = 16;
const int LOWER_PIN = 17;

int raiseHour = 20;
int raiseMinute = 35;
int lowerHour = 20;
int lowerMinute = 34;

bool shouldSaveConfig = false;
Preferences preferences;
char timezone[64];
char raiseHourStr[3];
char raiseMinStr[3];
char lowerHourStr[3];
char lowerMinStr[3];

int timeFailCount = 0;
unsigned long lastTimeSync = 0;
const unsigned long maxTimeSyncDelay = 1800 * 1000; // 30 min

void raise() {
  Serial.println("Raising");
  digitalWrite(RAISE_PIN, LOW);
  pinMode(RAISE_PIN, OUTPUT);
  delay(500);
  pinMode(RAISE_PIN, INPUT);
}

void lower() {
  Serial.println("Lowering");
  digitalWrite(LOWER_PIN, LOW);
  pinMode(LOWER_PIN, OUTPUT);
  delay(500);
  pinMode(LOWER_PIN, INPUT);
}

bool tryParseHour(const char* input, int& out) {
  int x = atoi(input);
  if (x >= 0 && x <= 23) {
    out = x;
    return true;
  }
  return false;
}

bool tryParseMinute(const char* input, int& out) {
  int x = atoi(input);
  if (x >= 0 && x <= 59) {
    out = x;
    return true;
  }
  return false;
}

void saveConfigCallback() {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void timeSyncCallback(struct timeval *tv) {
  lastTimeSync = millis();
  digitalWrite(LED_PIN, HIGH);
}

void setup() {
    WiFiManager wm;
    preferences.begin("projector", false); 

    pinMode(RAISE_PIN, INPUT);
    pinMode(LOWER_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(RESET_PIN, INPUT_PULLUP);
    digitalWrite(LED_PIN, LOW);
    Serial.begin(115200);
    delay(500);
    if (digitalRead(RESET_PIN) == LOW) {
      for(int i = 0; i < 10; ++i) {
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        delay(200);
      }

      wm.resetSettings();
      preferences.clear();
      ESP.restart();
    }

    strncpy(timezone, preferences.getString("timezone", "NZST-12NZDT,M9.5.0,M4.1.0/3").c_str(), sizeof(timezone));
    raiseHour = preferences.getInt("raise_hour", 12);
    raiseMinute = preferences.getInt("raise_minute", 0);
    lowerHour = preferences.getInt("lower_hour", 13);
    lowerMinute = preferences.getInt("lower_minute", 0);

    snprintf(raiseHourStr, sizeof(raiseHourStr), "%d", raiseHour);
    snprintf(raiseMinStr, sizeof(raiseMinStr), "%d", raiseMinute);
    snprintf(lowerHourStr, sizeof(lowerHourStr), "%d", lowerHour);
    snprintf(lowerMinStr, sizeof(lowerMinStr), "%d", lowerMinute);

    WiFi.mode(WIFI_STA);

    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setConfigPortalTimeout(300);
    WiFiManagerParameter timezoneParam("timezone", "Posix Timezone", timezone, sizeof(timezone) - 1);
    WiFiManagerParameter raiseHourParam("raise_hour", "Raise Hour", raiseHourStr, 2, " type='number' min='0' max='23' step='1'");
    WiFiManagerParameter raiseMinuteParam("raise_minute", "Raise Minute", raiseMinStr, 2, " type='number' min='0' max='59' step='1'");
    WiFiManagerParameter lowerHourParam("lower_hour", "Lower Hour", lowerHourStr, 2, " type='number' min='0' max='23' step='1'");
    WiFiManagerParameter lowerMinuteParam("lower_minute", "Lower Minute", lowerMinStr, 2, " type='number' min='0' max='59' step='1'");

    wm.addParameter(&timezoneParam);
    wm.addParameter(&raiseHourParam);
    wm.addParameter(&raiseMinuteParam);
    wm.addParameter(&lowerHourParam);
    wm.addParameter(&lowerMinuteParam);

    char ssid[34]; // 21 format chars, 12 mac chars and 1 null terminator
    snprintf(ssid, sizeof(ssid), "ProjectorScreenTimer-%llX", ESP.getEfuseMac());

    if (!wm.autoConnect(ssid)) {
      Serial.println("Restarting: Could not connect to WiFi");
      delay(3000);
      ESP.restart();
    }

    if (shouldSaveConfig) {
      bool fail = false;
      if (!tryParseHour(raiseHourParam.getValue(), raiseHour)) {
        fail = true;
        Serial.println("Invalid raise hour.");
      }
      if (!tryParseMinute(raiseMinuteParam.getValue(), raiseMinute)) {
        fail = true;
        Serial.println("Invalid raise minute.");
      }
      if (!tryParseHour(lowerHourParam.getValue(), lowerHour)) {
        fail = true;
        Serial.println("Invalid lower hour.");
      }
      if (!tryParseMinute(lowerMinuteParam.getValue(), lowerMinute)) {
        fail = true;
        Serial.println("Invalid lower minute.");
      }
      if (fail) {
        ESP.restart();
      }

      preferences.putString("timezone", timezoneParam.getValue());
      preferences.putInt("raise_hour", raiseHour);
      preferences.putInt("raise_minute", raiseMinute);
      preferences.putInt("lower_hour", lowerHour);
      preferences.putInt("lower_minute", lowerMinute);
      preferences.end();
      Serial.println("Parameters saved");
    }

    Serial.println("WiFi connected");
    sntp_set_time_sync_notification_cb(timeSyncCallback);
    configTzTime(timezone, ntpServer);
    sntp_set_sync_interval(600 * 1000); // 10 minutes

    Serial.printf("Raise Hour: %d\n", raiseHour);
    Serial.printf("Raise Minute: %d\n", raiseMinute);
    Serial.printf("Lower Hour: %d\n", lowerHour);
    Serial.printf("Lower Minute: %d\n", lowerMinute);
}

void loop() {
  delay(1000);
  if (millis() - lastTimeSync > maxTimeSyncDelay) {
    Serial.println("Restarting: Could not contact NTP server after multiple retries");
    ESP.restart(); // NTP not synced, restart.
  }

  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    if(timeFailCount++ > 10) {
      Serial.println("Restarting: Failed to obtain time 10 times");
      ESP.restart();
    }
    return;
  }
  timeFailCount = 0;
  unsigned long currentTime = millis();
  if (timeinfo.tm_hour == raiseHour && timeinfo.tm_min == raiseMinute) {
    raise();
    delay(60000);
  } else if (timeinfo.tm_hour == lowerHour && timeinfo.tm_min == lowerMinute) {
    lower();
    delay(60000);
  }
}