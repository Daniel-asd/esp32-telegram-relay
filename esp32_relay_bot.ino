#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <UniversalTelegramBot.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <time.h>
// ===================== ZXC =====================

// ===================== CONFIGURATION =====================
const char* ssid         = "YOUR_WIFI";
const char* password     = "PASSWORD_WIFI";
const char* botToken     = "TOKEN_BOT";
const int   relayPin     = 5;                   // GPIO-пин для реле
const char* scheduleFile = "/data.json";        // файл в SPIFFS

// Web-панель
WebServer server(80);
const char* webUser = "admin";
const char* webPass = "admin";

// ===================== ЧАСОВОЙ ПОЯС (в секундах) =====================
long gmtOffset_sec     = 0;   // смещение от UTC в секундах (например +3 часа -> +10800)
int  daylightOffset_sec = 0;  // поправка на DST (всегда 0)

// ===================== ВРЕМЯ =====================
void applyTimeSync() {
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for NTP time");
  time_t now = time(nullptr);
  while (now < 100000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println(" time synced");
}

String formatTime(int h, int m, int s) {
  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", h, m, s);
  return String(buf);
}

String getCurrentTimeStr() {
  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);
  return formatTime(ti.tm_hour, ti.tm_min, ti.tm_sec);
}

// ===================== ПАРСЕРЫ =====================
bool parseTime(String text, int &h, int &m, int &s) {
  if (text.length() != 8 || text.charAt(2) != ':' || text.charAt(5) != ':') return false;
  String sh = text.substring(0, 2), sm = text.substring(3, 5), ss = text.substring(6, 8);
  if (!isDigit(sh.charAt(0)) || !isDigit(sh.charAt(1)) ||
      !isDigit(sm.charAt(0)) || !isDigit(sm.charAt(1)) ||
      !isDigit(ss.charAt(0)) || !isDigit(ss.charAt(1))) return false;
  h = sh.toInt(); m = sm.toInt(); s = ss.toInt();
  return (h >= 0 && h <= 23 && m >= 0 && m <= 59 && s >= 0 && s <= 59);
}

bool parseOffset(String text, long &offsetSec) {
  // формат «±HH:MM», например «+03:00» или «-07:30»
  if (text.length() != 6 || (text.charAt(0) != '+' && text.charAt(0) != '-') || text.charAt(3) != ':')
    return false;
  String sh = text.substring(1, 3);
  String sm = text.substring(4, 6);
  if (!isDigit(sh.charAt(0)) || !isDigit(sh.charAt(1)) ||
      !isDigit(sm.charAt(0)) || !isDigit(sm.charAt(1))) return false;
  int hh = sh.toInt(), mm = sm.toInt();
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
  long total = hh * 3600L + mm * 60L;
  offsetSec = (text.charAt(0) == '+' ? total : -total);
  return true;
}

// ===================== ПОЛЬЗОВАТЕЛИ =====================
#define MAX_USERS 10
String allowedChatIds[MAX_USERS];
int    allowedUsersCount = 0;
String adminChatId       = "YOUR_ID"; // по умолчанию админ

struct TimeHMS { int hour, minute, second; };
TimeHMS onTime  = {-1, -1, -1};
TimeHMS offTime = {-1, -1, -1};

enum WaitInputState {
  NONE,
  WAIT_ON_TIME,
  WAIT_OFF_TIME,
  WAIT_ADD_CHATID
};
WaitInputState waitState = NONE;

bool isAllowedUser(const String &cid) {
  for (int i = 0; i < allowedUsersCount; i++) {
    if (allowedChatIds[i] == cid) return true;
  }
  return false;
}

bool addUser(const String &cid) {
  if (allowedUsersCount >= MAX_USERS) return false;
  if (isAllowedUser(cid)) return false;
  allowedChatIds[allowedUsersCount++] = cid;
  return true;
}

bool removeUser(const String &cid) {
  for (int i = 0; i < allowedUsersCount; i++) {
    if (allowedChatIds[i] == cid) {
      for (int j = i; j < allowedUsersCount - 1; j++) {
        allowedChatIds[j] = allowedChatIds[j + 1];
      }
      allowedUsersCount--;
      return true;
    }
  }
  return false;
}

// ===================== SPIFFS =====================
void saveData() {
  StaticJsonDocument<2048> doc;
  JsonArray usersArr = doc.createNestedArray("users");
  for (int i = 0; i < allowedUsersCount; i++) {
    usersArr.add(allowedChatIds[i]);
  }
  doc["admin"]        = adminChatId;
  doc["gmtOffset"]    = gmtOffset_sec;
  JsonObject sched    = doc.createNestedObject("schedule");
  sched["on"]  = formatTime(onTime.hour, onTime.minute, onTime.second);
  sched["off"] = formatTime(offTime.hour, offTime.minute, offTime.second);

  File file = SPIFFS.open(scheduleFile, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open data.json for writing");
    return;
  }
  serializeJson(doc, file);
  file.close();
  Serial.println("Data saved to SPIFFS");
}

void loadData() {
  if (!SPIFFS.exists(scheduleFile)) {
    Serial.println("No data.json found. Initializing defaults.");
    allowedUsersCount = 1;
    allowedChatIds[0] = adminChatId;
    gmtOffset_sec = 0;
    return;
  }
  File file = SPIFFS.open(scheduleFile, FILE_READ);
  if (!file) {
    Serial.println("Failed to open data.json");
    return;
  }
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    Serial.print("Failed to parse JSON: ");
    Serial.println(err.c_str());
    return;
  }
  allowedUsersCount = 0;
  if (doc.containsKey("users")) {
    for (JsonVariant v : doc["users"].as<JsonArray>()) {
      if (allowedUsersCount < MAX_USERS) {
        allowedChatIds[allowedUsersCount++] = String((const char*)v.as<const char*>());
      }
    }
  }
  if (doc.containsKey("admin")) {
    adminChatId = String((const char*)doc["admin"].as<const char*>());
  }
  if (doc.containsKey("gmtOffset")) {
    gmtOffset_sec = doc["gmtOffset"].as<long>();
  }
  if (doc.containsKey("schedule")) {
    String onStr  = String((const char*)doc["schedule"]["on"].as<const char*>());
    String offStr = String((const char*)doc["schedule"]["off"].as<const char*>());
    parseTime(onStr, onTime.hour, onTime.minute, onTime.second);
    parseTime(offStr, offTime.hour, offTime.minute, offTime.second);
  }
  Serial.println("Loaded data from SPIFFS:");
  Serial.printf(" Admin Chat ID: %s\n", adminChatId.c_str());
  Serial.printf(" GMT Offset: %+ld sec\n", gmtOffset_sec);
  Serial.print(" Users: ");
  for (int i = 0; i < allowedUsersCount; i++) {
    Serial.print(allowedChatIds[i]);
    if (allowedChatIds[i] == adminChatId) Serial.print(" (admin)");
    Serial.print(" ");
  }
  Serial.println();
  Serial.print(" Schedule ON:  "); Serial.println(formatTime(onTime.hour, onTime.minute, onTime.second));
  Serial.print(" Schedule OFF: "); Serial.println(formatTime(offTime.hour, offTime.minute, offTime.second));
}

// ===================== TELEGRAM BOT =====================
WiFiClientSecure securedClient;
UniversalTelegramBot bot(botToken, securedClient);
unsigned long lastTimeBotRan       = 0;
const unsigned long botScanInterval = 2000;

void sendMainMenu(const String &cid) {
  String msg =
    "Команды:\n"
    "/on          – включить реле\n"
    "/off         – выключить реле\n"
    "/time        – текущее время\n"
    "/ip          – IP для веб-панели\n"
    "/admin       – админ-панель (только админ)";
  bot.sendMessage(cid, msg, "");
}

void sendAdminMenu(const String &cid) {
  String msg =
    "Админ-панель:\n"
    "/add <chat_id>     – добавить пользователя\n"
    "/remove <chat_id>  – удалить пользователя\n"
    "/list_users        – список пользователей\n"
    "/set_time          – установить расписание";
  bot.sendMessage(cid, msg, "");
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String text   = bot.messages[i].text;
    String fromId = bot.messages[i].chat_id;

    if (!isAllowedUser(fromId)) {
      bot.sendMessage(fromId, "Извините, доступ запрещён.", "");
      continue;
    }

    // Админ вводит время включения
    if (waitState == WAIT_ON_TIME && fromId == adminChatId) {
      int h, m, s;
      if (parseTime(text, h, m, s)) {
        onTime.hour   = h;
        onTime.minute = m;
        onTime.second = s;
        waitState = WAIT_OFF_TIME;
        bot.sendMessage(fromId, "Время включения: " + formatTime(h, m, s) +
                                "\nВведите время выключения (HH:MM:SS)", "");
      } else {
        bot.sendMessage(fromId, "Неверный формат! HH:MM:SS", "");
      }
      continue;
    }
    // Админ вводит время выключения
    if (waitState == WAIT_OFF_TIME && fromId == adminChatId) {
      int h, m, s;
      if (parseTime(text, h, m, s)) {
        offTime.hour   = h;
        offTime.minute = m;
        offTime.second = s;
        waitState = NONE;
        saveData();
        bot.sendMessage(fromId, "Время выключения: " + formatTime(h, m, s) + "\nРасписание сохранено.", "");
      } else {
        bot.sendMessage(fromId, "Неверный формат! HH:MM:SS", "");
      }
      continue;
    }
    // Админ добавляет пользователя
    if (text.startsWith("/add ") && fromId == adminChatId) {
      String newId = text.substring(5);
      if (addUser(newId)) {
        saveData();
        bot.sendMessage(fromId, "Пользователь " + newId + " добавлен.", "");
      } else {
        bot.sendMessage(fromId, "Не удалось добавить. Либо уже есть, либо лимит.", "");
      }
      continue;
    }
    // Админ удаляет пользователя
    if (text.startsWith("/remove ") && fromId == adminChatId) {
      String remId = text.substring(8);
      if (remId == adminChatId) {
        bot.sendMessage(fromId, "Нельзя удалить админа.", "");
      } else if (removeUser(remId)) {
        saveData();
        bot.sendMessage(fromId, "Пользователь " + remId + " удалён.", "");
      } else {
        bot.sendMessage(fromId, "Пользователь " + remId + " не найден.", "");
      }
      continue;
    }
    // Обработка команд
    if (text == "/start" || text == "/menu") {
      sendMainMenu(fromId);
    }
    else if (text == "/on") {
      digitalWrite(relayPin, HIGH);
      bot.sendMessage(fromId, "Реле включено", "");
    }
    else if (text == "/off") {
      digitalWrite(relayPin, LOW);
      bot.sendMessage(fromId, "Реле выключено", "");
    }
    else if (text == "/time") {
      bot.sendMessage(fromId, "Текущее время: " + getCurrentTimeStr(), "");
    }
    else if (text == "/ip") {
      bot.sendMessage(fromId, "Веб-панель: http://" + WiFi.localIP().toString(), "");
    }
    else if (text == "/admin") {
      if (fromId == adminChatId) {
        sendAdminMenu(fromId);
      } else {
        bot.sendMessage(fromId, "Доступ запрещён.", "");
      }
    }
    else if (text == "/list_users" && fromId == adminChatId) {
      String list = "Пользователи:\n";
      for (int j = 0; j < allowedUsersCount; j++) {
        list += allowedChatIds[j];
        if (allowedChatIds[j] == adminChatId) list += " (admin)";
        list += "\n";
      }
      bot.sendMessage(fromId, list, "");
    }
    else if (text == "/set_time" && fromId == adminChatId) {
      waitState = WAIT_ON_TIME;
      bot.sendMessage(fromId, "Введите время включения (HH:MM:SS)", "");
    }
    else {
      bot.sendMessage(fromId, "Неизвестная команда. Используйте /menu", "");
    }
  }
}

// ===================== ОБРАБОТКА РАСПИСАНИЯ =====================
void checkSchedule() {
  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);

  if (onTime.hour >= 0 && offTime.hour >= 0) {
    if (ti.tm_hour == onTime.hour && ti.tm_min == onTime.minute && ti.tm_sec == onTime.second) {
      digitalWrite(relayPin, HIGH);
      Serial.println("Scheduled ON");
    }
    if (ti.tm_hour == offTime.hour && ti.tm_min == offTime.minute && ti.tm_sec == offTime.second) {
      digitalWrite(relayPin, LOW);
      Serial.println("Scheduled OFF");
    }
  }
}

// ===================== WEB-АДМИНКА =====================
void handleRoot() {
  if (!server.authenticate(webUser, webPass)) {
    return server.requestAuthentication();
  }
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html lang="ru">
    <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <title>ESP32 Admin Panel</title>
      <style>
        body {
          background: #f5f6fa;
          font-family: 'Arial', sans-serif;
          margin: 0; padding: 0;
          color: #2f3640;
        }
        header {
          background: #273c75;
          color: #f5f6fa;
          padding: 20px;
          text-align: center;
        }
        main {
          max-width: 800px;
          margin: 20px auto;
          padding: 0 20px;
        }
        section {
          background: #ffffff;
          border-radius: 8px;
          box-shadow: 0 2px 4px rgba(0,0,0,0.1);
          margin-bottom: 20px;
          padding: 20px;
        }
        .section-title {
          font-size: 1.2em;
          margin-bottom: 10px;
          color: #192a56;
        }
        label {
          display: block;
          margin: 10px 0 5px;
          font-weight: bold;
        }
        input[type="text"] {
          width: 100%;
          padding: 10px;
          border: 1px solid #dcdde1;
          border-radius: 4px;
          box-sizing: border-box;
          margin-bottom: 10px;
          font-size: 1em;
        }
        button {
          background: #44bd32;
          color: #ffffff;
          padding: 10px 20px;
          border: none;
          border-radius: 4px;
          cursor: pointer;
          font-size: 1em;
        }
        button:hover {
          background: #4cd137;
        }
        ul {
          list-style: none;
          padding: 0;
        }
        li {
          padding: 8px 0;
          border-bottom: 1px solid #dcdde1;
        }
        li:last-child {
          border-bottom: none;
        }
        .btn-danger {
          background: #e84118;
        }
        .btn-danger:hover {
          background: #ff3f34;
        }
      </style>
    </head>
    <body>
      <header>
        <h2>ESP32 Админ-панель</h2>
      </header>
      <main>
        <section>
          <div class="section-title">Admin Chat ID</div>
          <form method="POST" action="/setAdmin">
            <label>Текущий: )rawliteral" 
      + adminChatId 
      + R"rawliteral(</label>
            <input type="text" name="adminid" placeholder="Новый Admin Chat ID">
            <button type="submit">Сохранить</button>
          </form>
        </section>
        <section>
          <div class="section-title">Пользователи</div>
          <ul>
  )rawliteral";

  for (int i = 0; i < allowedUsersCount; i++) {
    html += "<li>" + allowedChatIds[i];
    if (allowedChatIds[i] == adminChatId) html += " (admin)";
    html += "</li>";
  }

  html += R"rawliteral(
          </ul>
          <form method="POST" action="/addUser">
            <label for="userid_add">Добавить Chat ID:</label>
            <input type="text" id="userid_add" name="userid" placeholder="Chat ID для добавления">
            <button type="submit">Добавить</button>
          </form>
          <form method="POST" action="/removeUser">
            <label for="userid_remove">Удалить Chat ID:</label>
            <input type="text" id="userid_remove" name="userid" placeholder="Chat ID для удаления">
            <button type="submit" class="btn-danger">Удалить</button>
          </form>
        </section>
        <section>
          <div class="section-title">Расписание</div>
          <p>ON: )rawliteral"
      + formatTime(onTime.hour, onTime.minute, onTime.second) +
    R"rawliteral(</p>
          <p>OFF: )rawliteral"
      + formatTime(offTime.hour, offTime.minute, offTime.second) +
    R"rawliteral(</p>
          <form method="POST" action="/setSchedule">
            <label for="ontime">Время включения (HH:MM:SS):</label>
            <input type="text" id="ontime" name="ontime" placeholder="например 07:30:00">
            <label for="offtime">Время выключения (HH:MM:SS):</label>
            <input type="text" id="offtime" name="offtime" placeholder="например 22:00:00">
            <button type="submit">Сохранить</button>
          </form>
        </section>
        <section>
          <div class="section-title">Смещение от UTC</div>
          <p>Текущее: )rawliteral"
      + (gmtOffset_sec >= 0 ? "+" : "-")
      + formatTime(abs(gmtOffset_sec)/3600, (abs(gmtOffset_sec)%3600)/60, 0) +
    R"rawliteral(</p>
          <form method="POST" action="/setTZ">
            <label for="tz">Введите смещение UTC (±HH:MM):</label>
            <input type="text" id="tz" name="tz" placeholder="например +03:00">
            <button type="submit">Сохранить</button>
          </form>
        </section>
      </main>
    </body>
    </html>
  )rawliteral";

  server.send(200, "text/html", html);
}

void handleSetAdmin() {
  if (!server.authenticate(webUser, webPass)) return server.requestAuthentication();
  if (server.hasArg("adminid")) {
    adminChatId = server.arg("adminid");
    saveData();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleAddUser() {
  if (!server.authenticate(webUser, webPass)) return server.requestAuthentication();
  if (server.hasArg("userid")) {
    addUser(server.arg("userid"));
    saveData();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleRemoveUser() {
  if (!server.authenticate(webUser, webPass)) return server.requestAuthentication();
  if (server.hasArg("userid")) {
    removeUser(server.arg("userid"));
    saveData();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSetSchedule() {
  if (!server.authenticate(webUser, webPass)) return server.requestAuthentication();
  if (server.hasArg("ontime") && server.hasArg("offtime")) {
    parseTime(server.arg("ontime"),  onTime.hour,  onTime.minute,  onTime.second);
    parseTime(server.arg("offtime"), offTime.hour, offTime.minute, offTime.second);
    saveData();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSetTZ() {
  if (!server.authenticate(webUser, webPass)) return server.requestAuthentication();
  if (server.hasArg("tz")) {
    long newOffset;
    if (parseOffset(server.arg("tz"), newOffset)) {
      gmtOffset_sec = newOffset;
      saveData();
      applyTimeSync();
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// ===================== SETUP & LOOP =====================
void setup() {
  Serial.begin(115200);
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);

  if (!SPIFFS.begin(true)) {
    Serial.println("Не удалось примонтировать SPIFFS");
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected!");
  securedClient.setInsecure();

  loadData();
  applyTimeSync();

  server.on("/",            HTTP_GET,  handleRoot);
  server.on("/setAdmin",    HTTP_POST, handleSetAdmin);
  server.on("/addUser",     HTTP_POST, handleAddUser);
  server.on("/removeUser",  HTTP_POST, handleRemoveUser);
  server.on("/setSchedule", HTTP_POST, handleSetSchedule);
  server.on("/setTZ",       HTTP_POST, handleSetTZ);
  server.begin();
  Serial.println("Web server started");

  lastTimeBotRan = 0;
}

void loop() {
  server.handleClient();

  if (millis() - lastTimeBotRan > botScanInterval) {
    int newMessages = bot.getUpdates(bot.last_message_received + 1);
    while (newMessages) {
      handleNewMessages(newMessages);
      newMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }

  checkSchedule();
  delay(50);
}
