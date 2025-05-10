#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>


WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000); // 3600 = časový posun na Slovensku (UTC+1), 60000 = synchronizácia každú minútu

unsigned long lastDisplayTime = 0;
bool showingName = false;


#define SS_PIN 5
#define RST_PIN 22
#define BUZZER_PIN 16

MFRC522 mfrc522(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// WiFi
const char* ssid = "iPhone";
const char* password = "123456789000";

// Telegram
String telegramToken = "7969960838:AAFCGwY-zIgRupyLKUA0GMcDmtewNKiCuTA";
String chatId = "5650127102";

#define MAX_LOGS 10
String logs[MAX_LOGS];
int logIndex = 0;

// Lokálny PHP server
const char* serverUrl = "http://rabcan.home.sk/insertRFID.php";

// Lokálna "databáza" používateľov
struct User {
  String cardID;
  String meno;
  String priezvisko;
};

User users[] = {
  {"A3E0DF2C", "Bc.Marketa", "Brisova"},
  {"3305FC2C", "Bc.Janka", "Bernatakova"},
  {"D5C17130", "Liana", "Brisova"}
};

const int numUsers = sizeof(users) / sizeof(users[0]);

void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();

  lcd.init();
  lcd.backlight();
  pinMode(BUZZER_PIN, OUTPUT);

  lcd.print("Skenujte kartu");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Pripajam sa na WiFi...");
  }
  Serial.println("WiFi pripojene");
  timeClient.begin();

}

void loop() {
  timeClient.update();

  if (!showingName) {
    // Získaj aktuálny čas
    time_t rawTime = timeClient.getEpochTime();
    struct tm * timeInfo = localtime(&rawTime);

    lcd.setCursor(0, 0);
    lcd.print((timeInfo->tm_mday < 10 ? "0" : "") + String(timeInfo->tm_mday) + ".");
    lcd.print((timeInfo->tm_mon + 1 < 10 ? "0" : "") + String(timeInfo->tm_mon + 1) + ".");
    lcd.print(String(timeInfo->tm_year + 1900));

    lcd.setCursor(0, 1);
    lcd.print(timeClient.getFormattedTime()); // zobrazí HH:MM:SS
  } else {
    if (millis() - lastDisplayTime > 5000) { // ak uplynulo 5 sekúnd
      showingName = false;
      lcd.clear();
      // Hneď obnov dátum a čas
      time_t rawTime = timeClient.getEpochTime();
      struct tm * timeInfo = localtime(&rawTime);

      lcd.setCursor(0, 0);
      lcd.print((timeInfo->tm_mday < 10 ? "0" : "") + String(timeInfo->tm_mday) + ".");
      lcd.print((timeInfo->tm_mon + 1 < 10 ? "0" : "") + String(timeInfo->tm_mon + 1) + ".");
      lcd.print(String(timeInfo->tm_year + 1900));

      lcd.setCursor(0, 1);
      lcd.print(timeClient.getFormattedTime());
    }
  }

  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String cardID = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      cardID += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
      cardID += String(mfrc522.uid.uidByte[i], HEX);
    }
    cardID.toUpperCase();

    Serial.println("ID karty: " + cardID);

    // Pípnutie buzzera
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);

    String meno = "Nezname";
    String priezvisko = "";

    for (int i = 0; i < numUsers; i++) {
      if (users[i].cardID.equalsIgnoreCase(cardID)) {
        meno = users[i].meno;
        priezvisko = users[i].priezvisko;
        break;
      }
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(meno);
    lcd.setCursor(0, 1);
    lcd.print(priezvisko);

    showingName = true;
    lastDisplayTime = millis(); // zapamätaj si čas zobrazenia mena

    Serial.println("Meno: " + meno);
    Serial.println("Priezvisko: " + priezvisko);

    sendDataToServer(cardID, meno, priezvisko);

    // Zaznamenaj do zoznamu
    String zaznam = meno + " " + priezvisko + " – " + cardID;
    logs[logIndex] = zaznam;
    logIndex = (logIndex + 1) % MAX_LOGS;

    // Odošli na Telegram
    String sprava = "💳 " + meno + " " + priezvisko + " práve priložil/a kartu.";
    sendTelegramMessage(sprava);

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }
}



void sendDataToServer(String cardID, String meno, String priezvisko) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String postData = "meno=" + meno + "&priezvisko=" + priezvisko + "&cardID=" + cardID;
    Serial.println("POST data: " + postData);

    int httpResponseCode = http.POST(postData);

    if (httpResponseCode > 0) {
      Serial.print("HTTP kód: ");
      Serial.println(httpResponseCode);
      String odpoved = http.getString();
      Serial.println("Server odpovedal: " + odpoved);
    } else {
      Serial.print("HTTP chyba: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("WiFi nie je pripojená");
  }
}

void sendTelegramMessage(String message) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + telegramToken +
                 "/sendMessage?chat_id=" + chatId +
                 "&text=" + urlencode(message);
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode > 0) {
      Serial.println("✅ Telegram správa odoslaná");
    } else {
      Serial.println("❌ Chyba pri odosielaní na Telegram");
    }
    http.end();
  }
}

void checkTelegramCommands() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + telegramToken + "/getUpdates";
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      if (payload.indexOf("/zoznam") >= 0) {
        String zoznam = "📋 Posledné záznamy:\n";
        for (int i = 0; i < MAX_LOGS; i++) {
          int idx = (logIndex + i) % MAX_LOGS;
          if (logs[idx] != "") {
            zoznam += String(i + 1) + ". " + logs[idx] + "\n";
          }
        }
        sendTelegramMessage(zoznam);
      }
    }
    http.end();
  }
}

// Pomocná funkcia na URL encoding (Telegram potrebuje)
String urlencode(String str) {
  String encoded = "";
  char c;
  char code0, code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encoded += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
      code0 = ((c >> 4) & 0xf) + '0';
      if (((c >> 4) & 0xf) > 9) code0 = ((c >> 4) & 0xf) - 10 + 'A';
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}
