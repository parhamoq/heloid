#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <ESP32Servo.h>
#include <time.h>
#include <math.h>

const char* SUPABASE_URL = "wurdazvtovxjogurmhbe.supabase.co";
const char* SUPABASE_ANON_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Ind1cmRhenZ0b3Z4am9ndXJtaGJlIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODI3MzY4NTgsImV4cCI6MjA5ODMxMjg1OH0.hjzfvLIbF4QGOOfRemkkfYb9RG_N5p7moFyJ8MR23ao"; 

const long TEMPERA_ROW_ID = 1;
const int STATUS_ROW_ID = 1;
const int SERVO_ROW_ID = 1;
const int MIXER_ROW_ID = 1;

const int LED_PIN = 2;
const int SERVO_PIN = 18;
#define ONE_WIRE_BUS 4
const int MOTOR_IN1_PIN = 19;
const int MOTOR_IN2_PIN = 21;

const int MAX_RECONNECT_ATTEMPTS = 10;

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c0c6c47c8a99"
#define SSID_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define PASS_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define DEVICE_NAME "SuperSmartOven"

Preferences preferences;
Servo myServo;
String wifiSsid = "";
String wifiPass = "";
unsigned long lastCheckTime = 0;
const long checkInterval = 500;
bool connectedToWiFi = false;
bool provisioningActive = false;
int reconnectAttempts = 0;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

float lastSentTemp = -1000.0;
bool firstTempSent = false;
const unsigned long TEMP_CHECK_INTERVAL_MS = 500;
unsigned long lastTempCheckTime = 0;
const float TEMP_CHANGE_THRESHOLD = 0.1;

bool timeSynced = false;

String log_mixer = "STOPPED";
String log_oven = "IDLE";
String log_temp = "N/A";
String log_supabase = "Waiting for data...";
unsigned long lastReportTime = 0;
const unsigned long REPORT_INTERVAL_MS = 2000; 

void printLog(String tag, String message) {
    Serial.println("=======================");
    Serial.print("["); Serial.print(tag); Serial.println("]");
    Serial.println(message);
    Serial.println("=======================");
}

class ProvisioningCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String value = pCharacteristic->getValue();
        if (pCharacteristic->getUUID().toString() == SSID_CHARACTERISTIC_UUID) {
            wifiSsid = String(value.c_str());
            printLog("BLE PROVISIONING", "SSID Received via Bluetooth: " + wifiSsid);
        } else if (pCharacteristic->getUUID().toString() == PASS_CHARACTERISTIC_UUID) {
            wifiPass = String(value.c_str());
            printLog("BLE PROVISIONING", "Password Received via Bluetooth.");
        }
        if (!wifiSsid.isEmpty() && !wifiPass.isEmpty()) {
            printLog("WIFI", "Both credentials received. Saving to flash memory and connecting...");
            preferences.begin("wifi-creds", false);
            preferences.putString("ssid", wifiSsid);
            preferences.putString("pass", wifiPass);
            preferences.end();
            for (int i = 0; i < 3; i++) {
                digitalWrite(LED_PIN, HIGH); delay(200);
                digitalWrite(LED_PIN, LOW); delay(200);
            }
            WiFi.disconnect(true);
            reconnectAttempts = 0;
            WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
            provisioningActive = true;
        }
    }
};

void startProvisioning() {
    provisioningActive = true;
    printLog("SYSTEM", "Entering BLE Provisioning Mode...\nDevice Name: " + String(DEVICE_NAME) + "\nWaiting for WiFi credentials from phone app...");
    WiFi.mode(WIFI_OFF);
    BLEDevice::init(DEVICE_NAME);
    BLEServer *pServer = BLEDevice::createServer();
    BLEService *pService = pServer->createService(SERVICE_UUID);
    BLECharacteristic *pSsidCharacteristic = pService->createCharacteristic(SSID_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE);
    pSsidCharacteristic->setCallbacks(new ProvisioningCallbacks());
    BLECharacteristic *pPassCharacteristic = pService->createCharacteristic(PASS_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE);
    pPassCharacteristic->setCallbacks(new ProvisioningCallbacks());
    for (int i = 0; i < 2; i++) {
        digitalWrite(LED_PIN, HIGH); delay(200);
        digitalWrite(LED_PIN, LOW); delay(100);
        digitalWrite(LED_PIN, HIGH); delay(200);
        digitalWrite(LED_PIN, LOW); delay(500);
    }
    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();
}

void syncTime() {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    int attempts = 0;
    while (attempts < 20) {
        time_t now = time(nullptr);
        if (now > 1609459200) {
            timeSynced = true;
            break;
        }
        attempts++;
        delay(500);
    }
}

String getISOTime() {
    time_t now = time(nullptr);
    if (now < 100000) return String("");
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    char buf[25];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return String(buf);
}

void checkProvisioningStatus() {
    if (provisioningActive && WiFi.status() == WL_CONNECTED) {
        connectedToWiFi = true;
        provisioningActive = false;
        BLEDevice::stopAdvertising();
        reconnectAttempts = 0;
        printLog("WIFI STATUS", "Successfully connected to new WiFi network!\nSSID: " + wifiSsid + "\nIP Address: " + WiFi.localIP().toString());
        syncTime();
    }
}

int fetchServoAngle() {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = "https://" + String(SUPABASE_URL) + "/rest/v1/servo?select=ang&id=eq." + String(SERVO_ROW_ID);
    http.begin(client, url);
    http.addHeader("apikey", SUPABASE_ANON_KEY);
    http.addHeader("Authorization", "Bearer " + String(SUPABASE_ANON_KEY));
    int httpCode = http.GET();
    int angle = -1;
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(256);
        if (!deserializeJson(doc, payload) && doc.as<JsonArray>().size() > 0) {
            angle = doc.as<JsonArray>()[0]["ang"];
            angle = constrain(angle, 0, 180);
        }
    }
    http.end();
    return angle;
}

void checkMixerStatus() {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = "https://" + String(SUPABASE_URL) + "/rest/v1/mixer?select=is_on,power&id=eq." + String(MIXER_ROW_ID);
    http.begin(client, url);
    http.addHeader("apikey", SUPABASE_ANON_KEY);
    http.addHeader("Authorization", "Bearer " + String(SUPABASE_ANON_KEY));
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(256);
        if (!deserializeJson(doc, payload) && doc.as<JsonArray>().size() > 0) {
            bool isOn = doc.as<JsonArray>()[0]["is_on"];
            int power = doc.as<JsonArray>()[0]["power"];

            if (isOn) {
                power = constrain(power, 0, 100);
                int pwmValue = map(power, 0, 100, 0, 255);
                analogWrite(MOTOR_IN1_PIN, pwmValue);
                analogWrite(MOTOR_IN2_PIN, 0); 
                log_mixer = "RUNNING (Power: " + String(power) + "% | PWM: " + String(pwmValue) + ")";
            } else {
                analogWrite(MOTOR_IN1_PIN, 0);
                analogWrite(MOTOR_IN2_PIN, 0);
                log_mixer = "STOPPED";
            }
        }
    } else {
        log_mixer = "ERROR (HTTP Code: " + String(httpCode) + ")";
    }
    http.end();
}

void checkSystemStatus(String ssid, String pass) {
    if (WiFi.status() != WL_CONNECTED) {
        reconnectAttempts++;
        log_oven = "WIFI DISCONNECTED (Reconnecting... " + String(reconnectAttempts) + ")";
        if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
            connectedToWiFi = false;
            printLog("WIFI CRITICAL", "Max reconnection attempts reached. Re-entering BLE mode.");
            startProvisioning();
            return;
        }
        WiFi.begin(ssid.c_str(), pass.c_str());
        return;
    }
    reconnectAttempts = 0;
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = "https://" + String(SUPABASE_URL) + "/rest/v1/toggle_status?select=is_active&id=eq." + String(STATUS_ROW_ID);
    http.begin(client, url);
    http.addHeader("apikey", SUPABASE_ANON_KEY);
    http.addHeader("Authorization", "Bearer " + String(SUPABASE_ANON_KEY));
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(256);
        if (!deserializeJson(doc, payload) && doc.as<JsonArray>().size() > 0) {
            bool isActive = doc.as<JsonArray>()[0]["is_active"];
            digitalWrite(LED_PIN, isActive ? HIGH : LOW);
            if (isActive) {
                int targetAngle = fetchServoAngle();
                if (targetAngle != -1) {
                    myServo.write(targetAngle);
                    log_oven = "ACTIVE (Valve Servo: " + String(targetAngle) + "°)";
                }
            } else {
                myServo.write(0);
                log_oven = "IDLE (Valve forced to 0°)";
            }
        }
    } else {
        log_oven = "ERROR (HTTP Code: " + String(httpCode) + ")";
    }
    http.end();
}

void sendTemperatureToSupabase(float temperature) {
    if (WiFi.status() != WL_CONNECTED) return;
    if (!timeSynced) syncTime();
    String ts = getISOTime();
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = "https://" + String(SUPABASE_URL) + "/rest/v1/tempera?id=eq." + String(TEMPERA_ROW_ID);
    http.begin(client, url);
    http.addHeader("apikey", SUPABASE_ANON_KEY);
    http.addHeader("Authorization", "Bearer " + String(SUPABASE_ANON_KEY));
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Prefer", "return=representation");
    String body;
    if (ts.length() > 0) body = String("{\"temperature\":") + String(temperature, 2) + String(",\"updated_at\":\"") + ts + String("\"}");
    else body = String("{\"temperature\":") + String(temperature, 2) + String("}");
    
    int httpCode = http.sendRequest("PATCH", body);
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_NO_CONTENT) {
        firstTempSent = true;
        lastSentTemp = temperature;
        log_supabase = "Success (Uploaded: " + String(temperature, 2) + " C)";
    } else {
        log_supabase = "Failed (HTTP Code: " + String(httpCode) + ")";
    }
    http.end();
}

void setup() {
    Serial.begin(115200);
    delay(1500); 
    
    printLog("WELCOME", "Welcome to Super Smart Oven System!\nInitializing hardware components...");
    delay(3000); 
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    pinMode(MOTOR_IN1_PIN, OUTPUT);
    pinMode(MOTOR_IN2_PIN, OUTPUT);
    digitalWrite(MOTOR_IN1_PIN, LOW);
    digitalWrite(MOTOR_IN2_PIN, LOW);

    myServo.attach(SERVO_PIN);
    myServo.write(0);
    printLog("SERVO INIT", "Servo motor successfully attached to Pin 18.\nInitial valve position set to 0 degrees (Closed).");
    delay(1500); 

    preferences.begin("wifi-creds", true);
    wifiSsid = preferences.getString("ssid", "");
    wifiPass = preferences.getString("pass", "");
    preferences.end();
    
    sensors.begin();
    int deviceCount = sensors.getDeviceCount();
    String sensorLog = "Initializing 1-Wire Bus on Pin 4...\nFound Devices: " + String(deviceCount);
    if (deviceCount == 0) {
        sensorLog += "\nWARNING: No DS18B20 sensor detected! Check pull-up or wiring.";
    } else {
        sensorLog += "\nDS18B20 Thermometer is ONLINE and ready.";
    }
    printLog("SENSOR INIT", sensorLog);
    delay(1500); 

    if (!wifiSsid.isEmpty()) {
        printLog("WIFI INIT", "Saved network found in memory.\nAttempting auto-connect to SSID: " + wifiSsid);
        WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
            delay(500);
            attempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            connectedToWiFi = true;
            digitalWrite(LED_PIN, HIGH); delay(200); digitalWrite(LED_PIN, LOW);
            printLog("WIFI INIT", "Auto-connection successful!\nIP Address assigned: " + WiFi.localIP().toString());
            syncTime();
        } else {
            printLog("WIFI WARNING", "Could not auto-connect to saved network. Timeout.");
        }
    } else {
        printLog("WIFI INFO", "No WiFi credentials found stored in memory storage.");
    }
    delay(1500); 
    
    if (!connectedToWiFi) startProvisioning();
    
    printLog("SYSTEM STATUS", "Initialization complete. Entering main monitoring loop...");
    delay(2000); 
}

void loop() {
    if (connectedToWiFi) {
        if (millis() - lastCheckTime >= checkInterval) {
            lastCheckTime = millis();
            checkSystemStatus(wifiSsid, wifiPass);
            checkMixerStatus();
        }
    } else if (provisioningActive) {
        checkProvisioningStatus();
    }
    
    if (millis() - lastTempCheckTime >= TEMP_CHECK_INTERVAL_MS) {
        lastTempCheckTime = millis();
        sensors.requestTemperatures();
        float tempC = sensors.getTempCByIndex(0);
        
        if (tempC != DEVICE_DISCONNECTED_C) {
            log_temp = String(tempC, 2) + " C";
            
            bool shouldSend = false;
            if (!firstTempSent) shouldSend = true;
            else if (fabs(tempC - lastSentTemp) >= TEMP_CHANGE_THRESHOLD) shouldSend = true;
            
            if (shouldSend) {
                sendTemperatureToSupabase(tempC);
            }
        } else {
            log_temp = "CRITICAL: Sensor Disconnected from Pin 4";
        }
    }

    if (millis() - lastReportTime >= REPORT_INTERVAL_MS) {
        lastReportTime = millis();
        
        String networkStatus = "";
        if (connectedToWiFi) {
            networkStatus = "Connected to " + wifiSsid + " (IP: " + WiFi.localIP().toString() + ")";
        } else if (provisioningActive) {
            networkStatus = "BLE Provisioning Mode Active";
        } else {
            networkStatus = "Disconnected";
        }

        String totalReport = "Network: " + networkStatus + "\n" +
                             "Oven State: " + log_oven + "\n" +
                             "Mixer State: " + log_mixer + "\n" +
                             "Temperature: " + log_temp + "\n" +
                             "Cloud Sync:  " + log_supabase;

        printLog("SYSTEM LIVE REPORT", totalReport);
    }
}
