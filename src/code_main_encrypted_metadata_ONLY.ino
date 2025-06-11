#include <Arduino.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <Adafruit_Fingerprint.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <map>
#include <time.h>
#include <AESLib.h>
#include <base64.h>
#include <ArduinoJson.h>

/* WiFi Configuration */
#define WIFI_SSID "Your_Network_Name"
#define WIFI_PASSWORD "*********"

/* Firebase Configuration */
#define DATABASE_URL "https://biometric***************.firebaseio.com/"
#define LEGACY_TOKEN "**************************************"

/* NTP Server Details */
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;   // UTC+1 for Nigeria
const int daylightOffset_sec = 0;  // No daylight saving in Nigeria

/* GPIO Pin Definitions */
#define WiFiLed 15
#define validateLED 2
#define EnrollLED 4
#define buzzer 32
#define reGister 13
#define back 27
#define deLete 25
#define ok 14
#define forward 12
#define reverse 26
#define RXD2 16
#define TXD2 17

/* Constants */
#define records 127  // Maximum number of fingerprint templates
#define USER_COUNT_ADDRESS 0
#define USER_STATE_BASE_ADDRESS 1
#define CLASS_NAME_LENGTH 6  // Length of "EIE52X"

/* AES Configuration */
const uint8_t AES_KEY[16] = { '*', '8', '1', '*', '2', '4', '*', '5', '9', '*' };                         // 16-byte key
uint8_t AES_IV[16] = { '*', 'c', 'h', 'i', 'd', 'a', 'e', '*', 'g', 'l', 'o', 'p', '*', 'n', 'j', '*' };  // 16-byte IV

/* Global Objects */
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&Serial2);
// Modified: Store isSignedIn, className, and presentTime
std::map<int, std::tuple<bool, String, String>> userState;  // Stores sign-in status, class name, and present time
int totalValidatedUsers = 0;
uint8_t id = 1;
int8_t classNum = 0;  // 0 to 9 for EIE520 to EIE529
unsigned long previousMillis = 0;
const long interval = 2000;
int currentState = 0;
unsigned long wifiStartTime;
const unsigned long wifiTimeout = 10000;
String currentDate = "";
String currentTime = "";
String fullDateTime = "";

/* Matriculation Numbers Array */
String sortedMatricNo[] = {
  "********", "********", "********", "********", "********", "********", "********", "********", //this list will contain the matric numbers of students.
};

/* Function Prototypes */
void connectToWiFi();
void setupFirebase();
void updateTimeStrings();
String getMatricNo(int fingerprintID);
void saveUserStates();
void loadUserStates();
void transitionLCD();
void checkKeys();
void Enroll();
void delet();
uint8_t getFingerprintEnroll();
int getFingerprintIDez();
uint8_t deleteFingerprint(uint8_t id);
void markAttendance(int fingerprintID);
void updateSignOutAttendance(int fingerprintID, String className);
void reconnectWiFi();
String encryptData(int id, String matricNo, String className, String date, int present, String presentTime, String absentTime);

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
  lcd.init();
  lcd.backlight();
  pinMode(WiFiLed, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(validateLED, OUTPUT);
  pinMode(EnrollLED, OUTPUT);
  pinMode(reGister, INPUT_PULLUP);
  pinMode(deLete, INPUT_PULLUP);
  pinMode(back, INPUT_PULLUP);
  pinMode(ok, INPUT_PULLUP);
  pinMode(forward, INPUT_PULLUP);
  pinMode(reverse, INPUT_PULLUP);
  digitalWrite(buzzer, LOW);

  lcd.clear();
  lcd.print(" Fingerprint ");
  lcd.setCursor(0, 1);
  lcd.print("AttendanceSystem");
  delay(2000);

  connectToWiFi();
  setupFirebase();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Waiting for NTP time sync...");
  time_t now = time(nullptr);
  while (now < 8 * 3600) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nTime synchronized");

  digitalWrite(buzzer, HIGH);
  delay(500);
  digitalWrite(buzzer, LOW);
  finger.begin(57600);
  lcd.clear();
  lcd.print("Finding Module..");
  delay(2000);
  if (finger.verifyPassword()) {
    Serial.println("Found fingerprint sensor!");
    lcd.clear();
    lcd.print("Module Found");
    delay(2000);
  } else {
    Serial.println("Did not find fingerprint sensor :(");
    lcd.clear();
    lcd.print("Module Not Found");
    lcd.setCursor(0, 1);
    lcd.print("Check Connections");
    while (1)
      ;
  }

  finger.getTemplateCount();
  if (finger.templateCount == 0) {
    Serial.println("Sensor doesn't contain any fingerprint data.");
  } else {
    Serial.print("Sensor contains ");
    Serial.print(finger.templateCount);
    Serial.println(" templates");
  }

  lcd.clear();
  lcd.print("System Starting!");
  lcd.setCursor(0, 1);
  lcd.print("Please Wait...");
  delay(3000);
  lcd.clear();
  digitalWrite(validateLED, HIGH);

  EEPROM.begin(512);
  loadUserStates();
  Serial.println("totalValidatedUsers: " + String(totalValidatedUsers));
}

void loop() {
  updateTimeStrings();
  if (millis() - previousMillis >= interval) {
    previousMillis = millis();
    transitionLCD();
  }

  int result = getFingerprintIDez();
  if (result > 0) {
    digitalWrite(validateLED, LOW);
    digitalWrite(buzzer, HIGH);
    delay(100);
    digitalWrite(buzzer, LOW);
    lcd.clear();
    lcd.print("ID:");
    lcd.print(result);
    lcd.setCursor(0, 1);
    lcd.print("Please Wait....");
    delay(1000);
    lcd.clear();
    lcd.print("Print Recognised");
    delay(1000);
    digitalWrite(validateLED, HIGH);

    bool isSignedIn = std::get<0>(userState[result]);
    String className = std::get<1>(userState[result]);
    lcd.clear();
    if (isSignedIn) {
      std::get<0>(userState[result]) = false;
      totalValidatedUsers--;
      lcd.print("User Signed Out");
      if (className != "") {
        updateSignOutAttendance(result, className);  // Update encrypted_data
      } else {
        lcd.clear();
        lcd.print("No Class Record");
        delay(2000);
      }
    } else {
      std::get<0>(userState[result]) = true;
      totalValidatedUsers++;
      lcd.print("User Signed In");
      markAttendance(result);  // Mark attendance in Firebase
    }
    saveUserStates();
    Serial.println("totalValidatedUsers: " + String(totalValidatedUsers));
    delay(2000);
    lcd.clear();
  }
  checkKeys();
}

void connectToWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  wifiStartTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    lcd.clear();
    lcd.print("Searching for ");
    lcd.setCursor(0, 1);
    lcd.print("WiFi...");
  }
  lcd.clear();
  lcd.print("WiFi Connected.");
  lcd.setCursor(0, 1);
  lcd.print("AttendanceSystem");
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  digitalWrite(WiFiLed, HIGH);
  delay(2000);
  lcd.clear();
}

void reconnectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, attempting to reconnect...");
    WiFi.reconnect();
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nReconnected to WiFi");
      digitalWrite(WiFiLed, HIGH);
    } else {
      Serial.println("\nFailed to reconnect to WiFi");
      digitalWrite(WiFiLed, LOW);
    }
  }
}

void setupFirebase() {
  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);
  config.database_url = DATABASE_URL;
  config.signer.tokens.legacy_token = LEGACY_TOKEN;
  config.timeout.networkReconnect = 15 * 1000;
  config.timeout.serverResponse = 15 * 1000;
  config.timeout.wifiReconnect = 15 * 1000;
  Firebase.setDoubleDigits(5);
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Firebase.setMaxRetry(fbdo, 3);
  Firebase.setMaxErrorQueue(fbdo, 10);
}

void updateTimeStrings() {
  if (WiFi.status() == WL_CONNECTED) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      Serial.println("Failed to obtain NTP time");
      currentDate = "0000-00-00";
      currentTime = "00:00:00";
      fullDateTime = "0000-00-00 00:00:00";
      lcd.clear();
      lcd.print("NTP Time Failed");
      lcd.setCursor(0, 1);
      lcd.print("Check WiFi");
      return;
    }
    char dateStr[11];
    sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    currentDate = String(dateStr);
    char timeStr[9];
    sprintf(timeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    currentTime = String(timeStr);
    char fullStr[20];
    sprintf(fullStr, "%04d-%02d-%02d %02d:%02d:%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    fullDateTime = String(fullStr);
  } else {
    currentDate = "0000-00-00";
    currentTime = "00:00:00";
    fullDateTime = "0000-00-00 00:00:00";
    lcd.clear();
    lcd.print("No WiFi");
    lcd.setCursor(0, 1);
    lcd.print("Time Unavailable");
  }
}

String getMatricNo(int fingerprintID) {
  if (fingerprintID < 1 || fingerprintID > 82) {
    return "Invalid ID";
  }
  return sortedMatricNo[fingerprintID - 1];
}

void saveUserStates() {
  EEPROM.write(USER_COUNT_ADDRESS, totalValidatedUsers);
  for (const auto& pair : userState) {
    int userId = pair.first;
    bool isSignedIn = std::get<0>(pair.second);
    String className = std::get<1>(pair.second);
    String presentTime = std::get<2>(pair.second);
    EEPROM.write(USER_STATE_BASE_ADDRESS + userId, isSignedIn ? 1 : 0);
    for (int i = 0; i < CLASS_NAME_LENGTH; i++) {
      char c = i < className.length() ? className[i] : '\0';
      EEPROM.write(USER_STATE_BASE_ADDRESS + records + userId * CLASS_NAME_LENGTH + i, c);
    }
    // Save present_time (max 8 chars for HH:MM:SS)
    for (int i = 0; i < 8; i++) {
      char c = i < presentTime.length() ? presentTime[i] : '\0';
      EEPROM.write(USER_STATE_BASE_ADDRESS + records + records * CLASS_NAME_LENGTH + userId * 8 + i, c);
    }
  }
  EEPROM.commit();
}

void loadUserStates() {
  totalValidatedUsers = EEPROM.read(USER_COUNT_ADDRESS);
  for (int i = 1; i <= records; i++) {
    int savedState = EEPROM.read(USER_STATE_BASE_ADDRESS + i);
    String className = "";
    for (int j = 0; j < CLASS_NAME_LENGTH; j++) {
      char c = EEPROM.read(USER_STATE_BASE_ADDRESS + records + i * CLASS_NAME_LENGTH + j);
      if (c != '\0') className += c;
      else break;
    }
    String presentTime = "";
    for (int j = 0; j < 8; j++) {
      char c = EEPROM.read(USER_STATE_BASE_ADDRESS + records + records * CLASS_NAME_LENGTH + i * 8 + j);
      if (c != '\0') presentTime += c;
      else break;
    }
    userState[i] = std::make_tuple(savedState == 1, className, presentTime);
  }
}

void transitionLCD() {
  switch (currentState) {
    case 0:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Time: ");
      lcd.print(currentTime);
      lcd.setCursor(0, 1);
      lcd.print("Date: ");
      lcd.print(currentDate);
      currentState = 1;
      break;
    case 1:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Enrolled: ");
      finger.getTemplateCount();
      lcd.print(finger.templateCount);
      lcd.setCursor(0, 1);
      lcd.print("Validated: ");
      lcd.print(totalValidatedUsers);
      currentState = 2;
      break;
    case 2:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Please Scan Your");
      lcd.setCursor(0, 1);
      lcd.print("Finger, Now!!!");
      currentState = 0;
      break;
  }
}

void checkKeys() {
  if (digitalRead(reGister) == 0) {
    digitalWrite(buzzer, HIGH);
    delay(100);
    digitalWrite(buzzer, LOW);
    digitalWrite(EnrollLED, HIGH);
    digitalWrite(validateLED, LOW);
    lcd.clear();
    lcd.print("Please Wait");
    delay(1000);
    while (digitalRead(reGister) == 0)
      ;
    Enroll();
  } else if (digitalRead(deLete) == 0) {
    digitalWrite(buzzer, HIGH);
    delay(100);
    digitalWrite(buzzer, LOW);
    lcd.clear();
    lcd.print("Please Wait");
    delay(1000);
    delet();
  }
}

void Enroll() {
  id = 1;
  classNum = 0;
  lcd.clear();
  lcd.print("Enter Finger ID:");
  lcd.setCursor(0, 1);
  lcd.print(id);
  bool idSelected = false;

  while (!idSelected) {
    if (digitalRead(forward) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      id++;
      if (id > records) id = 1;
      lcd.setCursor(0, 1);
      lcd.print("   ");
      lcd.setCursor(0, 1);
      lcd.print(id);
      delay(150);
    } else if (digitalRead(reverse) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      id--;
      if (id < 1) id = records;
      lcd.setCursor(0, 1);
      lcd.print("   ");
      lcd.setCursor(0, 1);
      lcd.print(id);
      delay(150);
    } else if (digitalRead(ok) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      idSelected = true;
      delay(150);
    } else if (digitalRead(back) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      digitalWrite(EnrollLED, LOW);
      digitalWrite(validateLED, HIGH);
      lcd.clear();
      return;
    }
  }

  lcd.clear();
  lcd.print("Enter Class 0-9:");
  lcd.setCursor(0, 1);
  lcd.print(classNum);
  bool classSelected = false;

  while (!classSelected) {
    if (digitalRead(forward) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      classNum++;
      if (classNum > 9 || classNum > 10) classNum = 0;
      lcd.setCursor(0, 1);
      lcd.print("   ");
      lcd.setCursor(0, 1);
      lcd.print(classNum);
      delay(150);
    } else if (digitalRead(reverse) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      classNum--;
      if (classNum < 0) classNum = 9;
      lcd.setCursor(0, 1);
      lcd.print("   ");
      lcd.setCursor(0, 1);
      lcd.print(classNum);
      delay(150);
    } else if (digitalRead(ok) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      classSelected = true;
      delay(150);
    } else if (digitalRead(back) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      digitalWrite(EnrollLED, LOW);
      digitalWrite(validateLED, HIGH);
      lcd.clear();
      return;
    }
  }

  if (getFingerprintEnroll() == FINGERPRINT_OK) {
    if (WiFi.status() == WL_CONNECTED && !fullDateTime.startsWith("0000")) {
      reconnectWiFi();
      FirebaseJson studentData;
      studentData.set("fingerprint_id", id);
      studentData.set("matric_number", getMatricNo(id));
      studentData.set("timestamp", fullDateTime);
      String path = "/registered_student_records/" + String(id);
      if (Firebase.ready()) {
        Serial.printf("Sending Student Data... %s\n", Firebase.set(fbdo, path, studentData) ? "Success" : fbdo.errorReason().c_str());
        if (!Firebase.set(fbdo, path, studentData)) {
          Serial.printf("HTTP Code: %d, Error: %s\n", fbdo.httpCode(), fbdo.errorReason().c_str());
        }
      } else {
        Serial.println("Firebase not ready for student data");
        lcd.clear();
        lcd.print("Firebase Error");
        delay(2000);
      }
    } else {
      lcd.clear();
      lcd.print("No WiFi/Time");
      lcd.setCursor(0, 1);
      lcd.print("Cannot Enroll");
      delay(2000);
    }
  }
  digitalWrite(EnrollLED, LOW);
  digitalWrite(validateLED, HIGH);
}

void delet() {
  int count = 1;
  lcd.clear();
  lcd.print("Enter Finger ID");
  while (1) {
    lcd.setCursor(0, 1);
    lcd.print(count);
    if (digitalRead(forward) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      count++;
      if (count > records) count = 1;
      delay(150);
    } else if (digitalRead(reverse) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      count--;
      if (count < 1) count = records;
      delay(150);
    } else if (digitalRead(ok) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      id = count;
      deleteFingerprint(id);
      finger.getTemplateCount();
      if (finger.templateCount < totalValidatedUsers) {
        totalValidatedUsers = 0;
      }
      if (WiFi.status() == WL_CONNECTED) {
        reconnectWiFi();
        String path = "/registered_student_records/" + String(id);
        if (Firebase.ready()) {
          Serial.printf("Deleting Student Data... %s\n", Firebase.deleteNode(fbdo, path) ? "Success" : fbdo.errorReason().c_str());
          if (!Firebase.deleteNode(fbdo, path)) {
            Serial.printf("HTTP Code: %d, Error: %s\n", fbdo.httpCode(), fbdo.errorReason().c_str());
          }
        }
      }
      return;
    } else if (digitalRead(back) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      return;
    }
  }
}

uint8_t getFingerprintEnroll() {
  int p = -1;
  lcd.clear();
  lcd.print("finger ID:");
  lcd.print(id);
  lcd.setCursor(0, 1);
  lcd.print("Place Finger");
  delay(2000);
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        lcd.clear();
        lcd.print("Image taken");
        delay(400);
        break;
      case FINGERPRINT_NOFINGER:
        Serial.println("No Finger");
        lcd.clear();
        lcd.print("No Finger Found");
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        lcd.clear();
        lcd.print("Comm Error");
        break;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        lcd.clear();
        lcd.print("Imaging Error");
        break;
      default:
        Serial.println("Unknown error");
        lcd.clear();
        lcd.print("Unknown Error");
        break;
    }
  }

  p = finger.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      lcd.clear();
      lcd.print("Image converted");
      delay(400);
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      lcd.clear();
      lcd.print("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      lcd.clear();
      lcd.print("Comm Error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      lcd.clear();
      lcd.print("Feature Not Found");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      lcd.clear();
      lcd.print("Feature Not Found");
      return p;
    default:
      Serial.println("Unknown error");
      lcd.clear();
      lcd.print("Unknown Error");
      return p;
  }

  Serial.println("Remove finger");
  lcd.clear();
  lcd.print("Remove Finger");
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }
  Serial.print("ID ");
  Serial.println(id);
  p = -1;
  Serial.println("Place same finger again");
  lcd.clear();
  lcd.print("Place Finger");
  lcd.setCursor(0, 1);
  lcd.print(" Again");
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        delay(400);
        break;
      case FINGERPRINT_NOFINGER:
        Serial.print(".");
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        break;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        break;
      default:
        Serial.println("Unknown error");
        break;
    }
  }

  p = finger.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      delay(400);
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return p;
    default:
      Serial.println("Unknown error");
      return p;
  }

  Serial.print("Creating model for #");
  Serial.println(id);
  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    return p;
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    Serial.println("Fingerprints did not match");
    return p;
  } else {
    Serial.println("Unknown error");
    return p;
  }

  Serial.print("ID ");
  Serial.println(id);
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.println("Stored!");
    lcd.clear();
    lcd.print("Finger Stored!");
    delay(2000);
    lcd.clear();
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    return p;
  } else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not store in that location");
    return p;
  } else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash");
    return p;
  } else {
    Serial.println("Unknown error");
    return p;
  }
  return p;
}

int getFingerprintIDez() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;
  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;
  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK) {
    lcd.clear();
    lcd.print("Finger Not Found");
    lcd.setCursor(0, 1);
    lcd.print("Try Later");
    delay(2000);
    lcd.clear();
    return -1;
  }
  Serial.print("Found ID #");
  Serial.print(finger.fingerID);
  return finger.fingerID;
}

uint8_t deleteFingerprint(uint8_t id) {
  uint8_t p = -1;
  lcd.clear();
  lcd.print("Please wait");
  p = finger.deleteModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.println("Deleted!");
    lcd.clear();
    lcd.print("Finger Deleted");
    lcd.setCursor(0, 1);
    lcd.print("Successfully");
    delay(1000);
  } else {
    Serial.print("Something Wrong");
    lcd.clear();
    lcd.print("Something Wrong");
    lcd.setCursor(0, 1);
    lcd.print("Try Again Later");
    delay(2000);
    return p;
  }
  return p;
}

String encryptData(int id, String matricNo, String className, String date, int present, String presentTime, String absentTime) {
  DynamicJsonDocument doc(512);
  doc["id"] = id;
  doc["matric_number"] = matricNo;
  doc["course"] = className;
  doc["date"] = date;
  doc["present"] = present;
  doc["present_time"] = presentTime;
  if (absentTime == "") {
    doc["absent_time"] = nullptr;  // Set null for absent_time
  } else {
    doc["absent_time"] = absentTime;
  }

  String jsonData;
  serializeJson(doc, jsonData);

  // Pad data to multiple of 16 bytes (PKCS#7 padding)
  size_t len = jsonData.length();
  size_t paddedLen = ((len / 16) + 1) * 16;
  uint8_t paddedData[paddedLen] = { 0 };  // Initialize to zero
  memcpy(paddedData, jsonData.c_str(), len);
  uint8_t padding = paddedLen - len;
  for (size_t i = len; i < paddedLen; i++) {
    paddedData[i] = padding;  // PKCS#7 padding
  }

  // Encrypt data
  uint8_t encryptedData[paddedLen];
  AESLib aesLib;
  aesLib.set_paddingmode(paddingMode::CMS);  // Use CMS (PKCS#7) padding
  uint8_t iv[16];
  memcpy(iv, AES_IV, 16);  // Copy IV to avoid modifying original
  aesLib.encrypt(paddedData, paddedLen, encryptedData, AES_KEY, 128, iv);

  // Encode to Base64
  String base64Encoded = base64::encode(encryptedData, paddedLen);
  return base64Encoded;
}

void markAttendance(int fingerprintID) {
  id = fingerprintID;
  classNum = 0;
  lcd.clear();
  lcd.print("Enter Class 0-9:");
  lcd.setCursor(0, 1);
  lcd.print(classNum);
  bool classSelected = false;

  while (!classSelected) {
    if (digitalRead(forward) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      classNum++;
      if (classNum > 9) classNum = 0;
      lcd.setCursor(0, 1);
      lcd.print("   ");
      lcd.setCursor(0, 1);
      lcd.print(classNum);
      delay(150);
    } else if (digitalRead(reverse) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      classNum--;
      if (classNum < 0) classNum = 9;
      lcd.setCursor(0, 1);
      lcd.print("   ");
      lcd.setCursor(0, 1);
      lcd.print(classNum);
      delay(150);
    } else if (digitalRead(ok) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      classSelected = true;
      delay(150);
    } else if (digitalRead(back) == 0) {
      digitalWrite(buzzer, HIGH);
      delay(100);
      digitalWrite(buzzer, LOW);
      lcd.clear();
      return;
    }
  }

  if (WiFi.status() == WL_CONNECTED && !fullDateTime.startsWith("0000")) {
    reconnectWiFi();
    String className = "EIE52" + String(classNum);
    // Store class name and present time
    userState[id] = std::make_tuple(true, className, currentTime);
    String matricNo = getMatricNo(id);

    // Encrypt data
    String encrypted = encryptData(id, matricNo, className, currentDate, 1, currentTime, "");

    // Write only encrypted_data as a string
    String path = "/attendance_records/" + className + "/" + currentDate + "/id_" + String(id);
    Serial.print("Attendance Path: ");
    Serial.println(path);
    if (Firebase.ready()) {
      bool success = Firebase.setString(fbdo, path, encrypted);
      Serial.printf("Sending Attendance Data... %s\n", success ? "Success" : fbdo.errorReason().c_str());
      if (!success) {
        Serial.printf("HTTP Code: %d, Error: %s\n", fbdo.httpCode(), fbdo.errorReason().c_str());
      }
    } else {
      Serial.println("Firebase not ready for attendance data");
      lcd.clear();
      lcd.print("Firebase Error");
      delay(2000);
    }
  } else {
    lcd.clear();
    lcd.print("No WiFi/Time");
    lcd.setCursor(0, 1);
    lcd.print("Cannot Mark");
    delay(2000);
  }
}

void updateSignOutAttendance(int fingerprintID, String className) {
  if (WiFi.status() != WL_CONNECTED || fullDateTime.startsWith("0000")) {
    lcd.clear();
    lcd.print("No WiFi/Time");
    lcd.setCursor(0, 1);
    lcd.print("Cannot Sign Out");
    delay(2000);
    return;
  }

  reconnectWiFi();
  String path = "/attendance_records/" + className + "/" + currentDate + "/id_" + String(fingerprintID);
  String matricNo = getMatricNo(fingerprintID);
  // Get stored present_time
  String presentTime = std::get<2>(userState[fingerprintID]);

  // Check if present_time exists
  if (presentTime == "") {
    Serial.println("No stored present_time found");
    lcd.clear();
    lcd.print("No Sign-In Record");
    delay(2000);
    return;
  }

  // Encrypt data with stored present_time and current absent_time
  String encrypted = encryptData(fingerprintID, matricNo, className, currentDate, 0, presentTime, currentTime);

  // Update with only encrypted_data as a string
  if (Firebase.ready()) {
    bool success = Firebase.setString(fbdo, path, encrypted);
    Serial.printf("Updating Sign-Out Attendance... %s\n", success ? "Success" : fbdo.errorReason().c_str());
    if (!success) {
      Serial.printf("HTTP Code: %d, Error: %s\n", fbdo.httpCode(), fbdo.errorReason().c_str());
      lcd.clear();
      lcd.print("Firebase Error");
      delay(2000);
    }
  } else {
    Serial.println("Firebase not ready for sign-out update");
    lcd.clear();
    lcd.print("Firebase Error");
    delay(2000);
  }
}
