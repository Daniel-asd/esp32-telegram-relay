# esp32-telegram-relay
Управление реле через ESP32: Telegram-бот (привилегии по Chat ID) и защитная веб-панель (admin/admin) для настройки расписания (HH:MM:SS) и UTC-смещения (±HH:MM), все данные хранятся в SPIFFS.
````markdown
# ESP32 Telegram Relay & Web Admin

Кратко: ESP32 управляет реле через Telegram-бота и через веб-панель (admin/admin). Поддерживается расписание (HH:MM:SS) и смещение от UTC (±HH:MM). Настройки сохраняются в SPIFFS.

## Возможности
- **Telegram-бот**  
  • `/on` / `/off` — включить/выключить реле сразу  
  • `/time` — показать текущее локальное время  
  • `/ip` — узнать IP веб-панели  
  • `/admin` — меню админа:
    - `/add <chat_id>` — добавить пользователя  
    - `/remove <chat_id>` — удалить пользователя  
    - `/list_users` — список разрешённых ID  
    - `/set_time` — задать расписание включения/выключения  

- **Веб-панель (HTTP, логин/пароль `admin`/`admin`)**  
  • Просмотр/смена Admin Chat ID  
  • Добавление/удаление Chat ID  
  • Установка расписания (`HH:MM:SS`)  
  • Задание смещения UTC (`±HH:MM`)  

- **Расписание**  
  ESP32 синхронизируется по NTP с учётом заданного смещения и включает/выключает реле точно в указанное время.

## Быстрый старт

1. Клонировать репозиторий:
   ```bash
   git clone https://github.com/Daniel-asd/esp32-telegram-relay.git
   cd esp32-telegram-relay
````

2. Открыть `sketch_jun03a.ino` в Arduino IDE.
3. В начале файла указать:

   ```cpp
   const char* ssid     = "ВАШ_SSID";
   const char* password = "ВАШ_ПАРОЛЬ_WIFI";
   const char* botToken = "ВАШ_TELEGRAM_BOT_TOKEN";
   String adminChatId   = "ВАШ_CHAT_ID"; // ваш Telegram ID
   ```
4. Загрузить на ESP32. В Serial Monitor взять IP-адрес.
5. В Telegram отправить `/start` боту, убедиться в работе.
6. Перейти в браузере по `http://<IP_ESP32>/`, ввести `admin`/`admin` и настроить расписание и смещение.

---

Все изменения сохраняются в SPIFFS (файл `data.json`). После правки расписания или смещения ESP32 автоматически синхронизирует время и применит новые параметры.

===================================================================================================

````markdown
# ESP32 Telegram Relay & Web Admin

**Short:** ESP32 controls a relay via a Telegram bot and a protected web panel (admin/admin). Supports scheduling (HH:MM:SS) and UTC offset (±HH:MM). Settings are stored in SPIFFS.

## Features
- **Telegram Bot**  
  • `/on` / `/off` — turn relay on/off immediately  
  • `/time` — display current local time  
  • `/ip` — get web panel IP  
  • `/admin` — admin menu:  
    - `/add <chat_id>` — add user  
    - `/remove <chat_id>` — remove user  
    - `/list_users` — list allowed IDs  
    - `/set_time` — set on/off schedule  

- **Web Panel (HTTP, admin/admin)**  
  • View/change Admin Chat ID  
  • Add/remove Chat IDs  
  • Set relay schedule (`HH:MM:SS`)  
  • Set UTC offset (`±HH:MM`)  

- **Scheduling**  
  ESP32 syncs with NTP (using the specified UTC offset) and switches the relay at the exact times.

## Quick Start

1. Clone the repo:
   ```bash
   git clone https://github.com/Daniel-asd/esp32-telegram-relay.git
   cd esp32-telegram-relay
````

2. Open `sketch_jun03a.ino` in Arduino IDE.
3. At the top of the file, set your credentials:

   ```cpp
   const char* ssid     = "YOUR_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   const char* botToken = "YOUR_TELEGRAM_BOT_TOKEN";
   String adminChatId   = "YOUR_TELEGRAM_CHAT_ID";
   ```
4. Upload to ESP32. Note the IP address in Serial Monitor.
5. In Telegram, send `/start` to your bot to confirm it’s working.
6. Visit `http://<ESP32_IP>/` in a browser, log in with `admin`/`admin`, and configure schedule and UTC offset.

---

All settings (users, schedule, UTC offset) are saved in SPIFFS (`data.json`). ESP32 will automatically re-sync NTP and apply changes after any update.
