#define BLYNK_TEMPLATE_ID "TMPL3o0XO3vQR"
#define BLYNK_TEMPLATE_NAME "2 pin smart plug"
#define BLYNK_AUTH_TOKEN    "UjPHfGcf9kO9fiyV3THlLGqcvkQzzuM4"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

Preferences preferences;

const int relay1 = 26;
const int relay2 = 27;

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

String savedSSID = "";
String savedPASS = "";
bool requestWifiScan = false; 

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { deviceConnected = true; }
    void onDisconnect(BLEServer* pServer) { 
      deviceConnected = false; 
      BLEDevice::startAdvertising(); 
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String value = pCharacteristic->getValue().c_str();
        
        if (value.length() > 0) {
            if (value == "1ON") {
                digitalWrite(relay1, LOW);
                Blynk.virtualWrite(V1, 1);
            } else if (value == "1OFF") {
                digitalWrite(relay1, HIGH);
                Blynk.virtualWrite(V1, 0);
            } else if (value == "2ON") {
                digitalWrite(relay2, LOW);
                Blynk.virtualWrite(V2, 1);
            } else if (value == "2OFF") {
                digitalWrite(relay2, HIGH);
                Blynk.virtualWrite(V2, 0);
            } 
            else if (value == "WIFI_DISC") {
                WiFi.disconnect(true, true);
                preferences.begin("wifi", false);
                preferences.clear(); 
                preferences.end();
                savedSSID = "";
                savedPASS = "";
                Serial.println("Wi-Fi Disconnected & Saved Credentials Cleared.");
            } 
            else if (value == "WIFI_SCAN") {
                requestWifiScan = true; 
            }
            // NEW: Respond to the HTML Dashboard's status request
            else if (value == "SYS_STATUS") {
                if (WiFi.status() == WL_CONNECTED) {
                    String ipStr = "IP:" + WiFi.localIP().toString();
                    pCharacteristic->setValue(ipStr.c_str());
                    pCharacteristic->notify();
                } else {
                    pCharacteristic->setValue("NOWIFI");
                    pCharacteristic->notify();
                }
            }
            else if (value.startsWith("WIFI:")) {
                int firstColon = value.indexOf(':');
                int secondColon = value.indexOf(':', firstColon + 1);
                
                if (firstColon != -1 && secondColon != -1) {
                    savedSSID = value.substring(firstColon + 1, secondColon);
                    savedPASS = value.substring(secondColon + 1);
                    
                    preferences.begin("wifi", false);
                    preferences.putString("ssid", savedSSID);
                    preferences.putString("pass", savedPASS);
                    preferences.end();
                    
                    Serial.println("New credentials saved. Connecting...");
                    WiFi.disconnect();
                    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
                }
            }
        }
    }
};

BLYNK_CONNECTED() {
  Blynk.syncVirtual(V1, V2);
}

BLYNK_WRITE(V1) {
  int value = param.asInt(); 
  digitalWrite(relay1, value == 1 ? LOW : HIGH);
}

BLYNK_WRITE(V2) {
  int value = param.asInt();
  digitalWrite(relay2, value == 1 ? LOW : HIGH);
}

void setup() {
  Serial.begin(115200);
  
  pinMode(relay1, OUTPUT);
  pinMode(relay2, OUTPUT);
  digitalWrite(relay1, HIGH); 
  digitalWrite(relay2, HIGH);

  // NEW: Boot-up Wi-Fi Connection Logic
  preferences.begin("wifi", true);
  savedSSID = preferences.getString("ssid", "");
  savedPASS = preferences.getString("pass", "");
  preferences.end();

  if (savedSSID != "") {
      Serial.println("Attempting to connect to saved Wi-Fi: " + savedSSID);
      WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
      
      int attempts = 0;
      // Wait up to 10 seconds (20 attempts * 500ms) for a connection
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
          delay(500);
          Serial.print(".");
          attempts++;
      }
      
      if (WiFi.status() == WL_CONNECTED) {
          Serial.println("\nWi-Fi Connected Successfully!");
          Serial.print("IP Address: ");
          Serial.println(WiFi.localIP());
      } else {
          Serial.println("\nFailed to connect to saved Wi-Fi. Awaiting BLE Setup.");
      }
  } else {
      Serial.println("No saved Wi-Fi credentials. Awaiting BLE Setup.");
  }

  // Initialize Blynk (non-blocking)
  Blynk.config(BLYNK_AUTH_TOKEN);

  // Start BLE Server (Always running so you can control it locally or configure Wi-Fi if it drops)
  BLEDevice::init("ESP32_SmartPlug");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
                    
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new MyCallbacks());
  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }
  
  if (requestWifiScan && deviceConnected) {
      requestWifiScan = false;
      pCharacteristic->setValue("SCAN:START");
      pCharacteristic->notify();

      int n = WiFi.scanNetworks();
      
      if (n == 0) {
          pCharacteristic->setValue("SCAN:NONE");
          pCharacteristic->notify();
      } else {
          for (int i = 0; i < n; ++i) {
              String netInfo = "NET:" + WiFi.SSID(i);
              pCharacteristic->setValue(netInfo.c_str());
              pCharacteristic->notify();
              delay(40);
          }
      }
      pCharacteristic->setValue("SCAN:DONE");
      pCharacteristic->notify();
      WiFi.scanDelete(); 
  }
  
  delay(10); 
}
