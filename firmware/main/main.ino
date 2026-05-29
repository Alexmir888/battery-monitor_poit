#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h" 


const int ledPins[6] = {13, 14, 16, 17, 18, 19};
const int relayPins[6] = {21, 22, 23, 25, 26, 27};
const int analogPins[6] = {36, 39, 34, 35, 32, 33};

const float dividerRatio = 0.64;

const float minVoltageThreshold = 2.9;  
const float fullVoltageThreshold = 3.9; 

float batteryVoltages[6] = {0, 0, 0, 0, 0, 0};

unsigned long previousMillisVolt = 0;
long voltInterval = 1000; 

unsigned long previousMillisBlink = 0;
const long blinkInterval = 500; 
bool blinkState = false;        

float getStableVoltage(int pin) {
  const int NUM_SAMPLES = 10; 
  int samples[NUM_SAMPLES];

  for (int i = 0; i < NUM_SAMPLES; i++) {
    samples[i] = analogRead(pin);
    delay(2); 
  }

  for (int i = 0; i < NUM_SAMPLES - 1; i++) {
    for (int j = i + 1; j < NUM_SAMPLES; j++) {
      if (samples[i] > samples[j]) {
        int temp = samples[i];
        samples[i] = samples[j];
        samples[j] = temp;
      }
    }
  }

  long sum = 0;
  for (int i = 2; i < NUM_SAMPLES - 2; i++) {
    sum += samples[i];
  }
  
  float averageRaw = sum / (float)(NUM_SAMPLES - 4);

  float pinVoltage = (averageRaw / 4095.0) * 3.3;
  float realVoltage = pinVoltage / dividerRatio;

  return realVoltage;
}

void setup() {
  delay(2000); 

  Serial.begin(115200);
  
  for (int i = 0; i < 6; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], HIGH); 
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], HIGH); 
  }

  Serial.print("Connecting to Wi-Fi");
  
  WiFi.mode(WIFI_STA); 
  WiFi.disconnect();   
  delay(100);          

  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
}

void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  }


  unsigned long currentMillis = millis();

  if (currentMillis - previousMillisBlink >= blinkInterval) {
    previousMillisBlink = currentMillis;
    blinkState = !blinkState; 
  }

  for (int i = 0; i < 6; i++) {
    if (batteryVoltages[i] == 0.0) {
      digitalWrite(ledPins[i], HIGH); 
    } 
    else if (batteryVoltages[i] >= fullVoltageThreshold) {
      digitalWrite(ledPins[i], LOW);  
    } 
    else {
      digitalWrite(ledPins[i], blinkState ? LOW : HIGH); 
    }
  }

  if (currentMillis - previousMillisVolt >= voltInterval) {
    previousMillisVolt = currentMillis;
    
    String jsonPayload = "{";
    
    for (int i = 0; i < 6; i++) {
      float realVoltage = getStableVoltage(analogPins[i]);
      
      if (realVoltage < minVoltageThreshold) {
        realVoltage = 0.0;
      }
      
      batteryVoltages[i] = realVoltage;
      
      jsonPayload += "\"v" + String(i+1) + "\":" + String(realVoltage);
      if(i < 5) jsonPayload += ", ";
    }
    jsonPayload += "}";

    if(WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(serverName);
      http.addHeader("Content-Type", "application/json");
      
      int httpResponseCode = http.POST(jsonPayload);
      
      if (httpResponseCode > 0) {
        String response = http.getString();
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, response);

        if (!error) {
          if (doc.containsKey("interval")) {
            voltInterval = doc["interval"].as<long>();
          }

          String command = doc["command"]; 
          if (command != "none") {
            int relayNum = command.toInt(); 
            if (relayNum >= 1 && relayNum <= 6) {
              int relayIndex = relayNum - 1; 
              
              Serial.print(">>> RELAY PULSE: ");
              Serial.println(relayNum);

              digitalWrite(relayPins[relayIndex], LOW);  
              delay(100);                                
              digitalWrite(relayPins[relayIndex], HIGH); 
            }
          }
        }
      }
      http.end(); 
    }
  }
}