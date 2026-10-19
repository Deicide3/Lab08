#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <MPU6050_light.h>

// --- Configuration ---
char WIFI_SSID[] = "Wokwi-GUEST";
char WIFI_PASSWORD[] = "";      
const char* token = "rexbShf2rLxzDHG4bk19"; // <-- Replace with your Access Token
const char* thingsboard_server = "thingsboard.cloud";

const int port = 1883;

const char STUDENT_ID[]      = "68093647";   
const char FIRMWARE_VERSION[] = "1.0"; 

// --- Hardware Pins ---
#define pinLED 4       // LED cathode → GPIO 4
#define LED_ON  LOW    // LED wired 3V3 → R → Anode → Cathode → GPIO: LOW = ON
#define LED_OFF HIGH

bool ledState = false;  

#define pinSDA 8       
#define pinSCL 9       

// --- MPU6050 Configuration ---
#define NUM_MPUS 1
MPU6050 mpu[NUM_MPUS] = { MPU6050(Wire) };
const int AD0pin[NUM_MPUS] = {5};   

// --- Peripheral Objects ---
WiFiClient espClient;
PubSubClient client(espClient);

// --- Timing ---
unsigned long lastInferenceTime = 0;
const unsigned long InferenceInterval = 1000; // ทำ Inference ทุกๆ 1 วินาที

// --- WiFi / MQTT ---
unsigned long lastWiFiRetry = 0;
const unsigned long wifiRetryInterval = 500;
unsigned long lastMQTTRetry = 0;
const unsigned long mqttRetryInterval = 5000;

// --- Prototypes ---
void onMessage(char* topic, byte* payload, unsigned int length);
void connectToWiFi();
void connectToMQTTBroker();
void setupMPU();
void updateMPU();
void SelectMPU(int selection);
void publishAttributes();
void runInferenceAndHandleResults();

// ====================================================================
//  setup()
// ====================================================================
void setup() {
  Serial.begin(115200);  
  pinMode(pinLED, OUTPUT);
  digitalWrite(pinLED, LED_OFF); // เริ่มต้นให้ LED ดับ

  setupMPU();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Connected to WiFi");
  }

  client.setServer(thingsboard_server, port);
  client.setBufferSize(512);  
  client.setCallback(onMessage);
}

// ====================================================================
//  loop()
// ====================================================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
    return;  
  }

  if (!client.connected()) {
    connectToMQTTBroker();
    return;  
  }
  client.loop();

  updateMPU();

  // ทำการประมวลผล Inference แทนการอ่านค่าและส่งค่า MPU ดิบ
  if (millis() - lastInferenceTime >= InferenceInterval) {
    lastInferenceTime = millis();
    runInferenceAndHandleResults();
  }
}

// ====================================================================
//  Inference & Logic Handling (ส่วนที่เพิ่มใหม่ตามโจทย์)
// ====================================================================
void runInferenceAndHandleResults() {
  // 1. อ่านค่า MPU เพื่อเตรียมป้อนเข้า Model (ถ้ามี Model จริง)
  SelectMPU(0);
  float accX = mpu[0].getAccX();
  
  // ---------------------------------------------------------
  // MOCK INFERENCE: จำลองผลลัพธ์จาก Model (สุ่มค่าเพื่อทดสอบ)
  // ให้แก้ไขส่วนนี้เป็นการเรียกใช้โมเดล AI ของคุณจริงๆ
  // ---------------------------------------------------------
  bool isAnomaly = random(0, 100) > 70; // สุ่มให้ผิดปกติ 30% ของเวลาทั้งหมด
  float normalProb = isAnomaly ? random(0, 400)/1000.0 : random(800, 1000)/1000.0;
  float unbalanceProb = 1.0 - normalProb;
  float anomalyScore = isAnomaly ? random(0, 500)/1000.0 : random(-500, -100)/1000.0;
  String faultType = unbalanceProb > 0.5 ? "unbalance" : "unknown_anomaly";
  // ---------------------------------------------------------

  // 2. จัดการ LED ตามสถานะ
  if (isAnomaly) {
    digitalWrite(pinLED, LED_ON);
  } else {
    digitalWrite(pinLED, LED_OFF);
  }

  // 3. แสดงผลทาง Serial ตลอดเวลาที่ Inference เสร็จ
  if (isAnomaly) {
    Serial.printf("🔴 Machine state: anomaly | fault=%s | normal=%.3f unbalance=%.3f anomaly_score=%.3f\n", 
                  faultType.c_str(), normalProb, unbalanceProb, anomalyScore);
  } else {
    Serial.printf("🟢 Machine state: normal   | normal=%.3f unbalance=%.3f anomaly_score=%.3f\n", 
                  normalProb, unbalanceProb, anomalyScore);
  }

  // 4. ส่งข้อมูลไป ThingsBoard "เฉพาะตอนผิดปกติ" เท่านั้น
  if (isAnomaly) {
    JsonDocument doc;
    doc["machine_state"] = "anomaly"; //
    doc["fault"] = faultType; //[cite: 1]
    doc["anomaly_score"] = anomalyScore; //[cite: 1]
    doc["unbalance_prob"] = unbalanceProb; //[cite: 1]

    char payload[256];
    size_t len = serializeJson(doc, payload, sizeof(payload));
    if (len > 0) {
      client.publish("v1/devices/me/telemetry", payload);
      Serial.print(F("📨 Anomaly Telemetry Sent: "));
      Serial.println(payload);
    }
  }
  // ถ้าปกติ จะไม่มีการส่ง telemetry ตามเงื่อนไขข้อ 1.3[cite: 1]
}

// ====================================================================
//  MPU6050 Functions (ตัด readMPU ออก)
// ====================================================================
void setupMPU() {
  Wire.begin(pinSDA, pinSCL);
  Wire.setTimeOut(1000);  

  for (int i = 0; i < NUM_MPUS; i++) {
    pinMode(AD0pin[i], OUTPUT);
    digitalWrite(AD0pin[i], HIGH);  
  }

  Serial.println(F("Initializing MPU-6050 sensors. Do not move the sensor."));
  delay(500);

  for (int i = 0; i < NUM_MPUS; i++) {
    SelectMPU(i);
    byte status = mpu[i].begin();
  }
}

void updateMPU() {
  for (int i = 0; i < NUM_MPUS; i++) {
    SelectMPU(i);
    mpu[i].update();
  }
}

void SelectMPU(int selection) {
  for (int i = 0; i < NUM_MPUS; i++) {
    if (i == selection)
      digitalWrite(AD0pin[i], LOW);   
    else
      digitalWrite(AD0pin[i], HIGH);  
  }
}

// ====================================================================
//  RPC Callback & Networking Functions (คงเดิม)
// ====================================================================
void onMessage(char* topic, byte* payload, unsigned int length) {
  char responseTopic[128];
  strncpy(responseTopic, topic, sizeof(responseTopic) - 1);
  responseTopic[sizeof(responseTopic) - 1] = '\0';
  char* reqPtr = strstr(responseTopic, "request");
  if (reqPtr) {
    size_t tailLen = strlen(reqPtr + 7);
    memmove(reqPtr + 8, reqPtr + 7, tailLen + 1);
    memcpy(reqPtr, "response", 8);
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) return;

  const char* methodName = doc["method"];  
  bool params = doc["params"];             

  bool handled = false;
  if (methodName && strcmp(methodName, "setLed") == 0) {
    handled = true;
    ledState = params;
    digitalWrite(pinLED, ledState ? LED_ON : LED_OFF);
  }

  JsonDocument resp;
  resp["success"] = handled;
  if (handled) resp["led_state"] = ledState;
  char respPayload[64];
  serializeJson(resp, respPayload, sizeof(respPayload));
  client.publish(responseTopic, respPayload);

  if (handled) publishAttributes(); 
}

void connectToWiFi() {
  if (millis() - lastWiFiRetry < wifiRetryInterval) return;
  lastWiFiRetry = millis();
  if (WiFi.status() == WL_CONNECTED) {}
}

void connectToMQTTBroker() {
  if (millis() - lastMQTTRetry < mqttRetryInterval) return;
  lastMQTTRetry = millis();
  if (client.connect("ESP32_Client", token, NULL)) {
    client.subscribe("v1/devices/me/rpc/request/+");
    publishAttributes();
  }
}

void publishAttributes() {
  JsonDocument doc;
  doc["student_id"] = STUDENT_ID;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["led_state"] = ledState;
  char payload[128];
  size_t len = serializeJson(doc, payload, sizeof(payload));
  if (len > 0) client.publish("v1/devices/me/attributes", payload);
}