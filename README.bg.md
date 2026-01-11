# ESP8266 Метеостанция 🌤️

🇬🇧 [Read in English](README.md)

Компактна метеостанция на базата на ESP8266 с 2.4" TFT дисплей, показваща вътрешна/външна температура, влажност, налягане и 3-дневна прогноза.

## Функции

- 📊 **Двойни показания**: Вътрешни (AHT20 сензор) и външни (от отдалечена станция)
- 🌡️ Температура, влажност и налягане с индикатори за тенденция
- 📅 3-дневна метеорологична прогноза от Open-Meteo API
- 🔄 Автоматично превключване между основен екран и прогноза
- 🕐 NTP синхронизация на времето с поддръжка на часови зони
- 📶 Автоматично повторно свързване към WiFi

## Хардуерни изисквания

| Компонент | Описание |
|-----------|----------|
| NodeMCU v3 | ESP8266 платка за разработка |
| ILI9341 | 2.4" TFT LCD дисплей (240x320, SPI) |
| AHT20 | Сензор за температура и влажност (I2C) |

### Схема на свързване

![Breadboard оформление](docs/home%20weather%20station_bb.jpg)

<details>
<summary>📐 Натисни за електрическа схема</summary>

![Електрическа схема](docs/home%20weather%20station_schem.png)

</details>

> 💡 Fritzing проект: [home weather station.fzz](docs/home%20weather%20station.fzz)

#### Връзки на пиновете

**TFT дисплей (ILI9341) → NodeMCU:**

| Дисплей | NodeMCU | GPIO |
|---------|---------|------|
| VCC | 3V3 | - |
| GND | GND | - |
| CS | D8 | GPIO15 |
| RESET | D3 | GPIO0 |
| DC | D4 | GPIO2 |
| SDI (MOSI) | D7 | GPIO13 |
| SCK | D5 | GPIO14 |
| LED | 3V3 | - |

**AHT20 сензор → NodeMCU:**

| Сензор | NodeMCU | GPIO |
|--------|---------|------|
| VCC | 3V3 | - |
| GND | GND | - |
| SDA | D2 | GPIO4 |
| SCL | D1 | GPIO5 |

## Инсталация

### Предварителни изисквания

- [VS Code](https://code.visualstudio.com/) с [PlatformIO разширение](https://platformio.org/install/ide?install=vscode)
- Или самостоятелен [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation.html)

### Стъпки

1. **Клонирай хранилището:**
   ```bash
   git clone https://github.com/NTsvetkov/home-weather-station.git
   cd home-weather-station
   ```

2. **Създай конфигурационен файл:**
   ```bash
   cp src/config.example.h src/config.h
   ```

3. **Редактирай `src/config.h`** с твоите настройки:
   ```cpp
   #define WIFI_SSID "твоята-wifi-мрежа"
   #define WIFI_PASS "твоята-парола"
   ```

4. **Компилирай и качи:**
   ```bash
   pio run -t upload
   ```

5. **Следи серийния изход (по желание):**
   ```bash
   pio device monitor
   ```

## Конфигурация

Всички настройки са в `src/config.h`. Основни опции:

| Настройка | По подразбиране | Описание |
|-----------|-----------------|----------|
| `WIFI_SSID` | - | Име на WiFi мрежата |
| `WIFI_PASS` | - | Парола за WiFi |
| `TZ_INFO` | `EET-2EEST...` | Часова зона (POSIX формат) |
| `TREND_WINDOW_MINUTES` | 10 | Минути за изчисляване на тенденция |
| `CFG_MAIN_SCREEN_DURATION_MS` | 10000 | Време на основен екран (ms) |
| `CFG_FORECAST_SCREEN_DURATION_MS` | 10000 | Време на екран с прогноза (ms) |
| `CFG_GAUGE_FETCH_INTERVAL_MS` | 180000 | Опресняване на външни данни (3 мин) |
| `CFG_FORECAST_FETCH_INTERVAL_MS` | 3600000 | Опресняване на прогноза (1 час) |

### Източници на данни

- **Външни данни**: От `meter.ac` (настройва се чрез `CFG_GAUGE_URL`)
- **Прогноза**: [Open-Meteo API](https://open-meteo.com/) (безплатен, без API ключ)

## Структура на проекта

```
├── src/
│   ├── main.cpp          # Основна логика
│   ├── config.h          # Твоята конфигурация (в .gitignore)
│   ├── config.example.h  # Шаблон за конфигурация
│   ├── display.cpp/h     # Функции за рисуване на TFT
│   ├── data.cpp/h        # Структури и извличане на данни
│   ├── sensors.cpp/h     # Работа с AHT20 сензора
│   └── utils.cpp/h       # Помощни функции
├── lib/
│   └── TFT_eSPI/         # Конфигурация на дисплей библиотеката
├── docs/                 # Документация и снимки
└── platformio.ini        # PlatformIO конфигурация
```

## Зависимости

Управляват се автоматично от PlatformIO:

- [Adafruit ILI9341](https://github.com/adafruit/Adafruit_ILI9341)
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
- [Adafruit AHTX0](https://github.com/adafruit/Adafruit_AHTX0)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)

## Отстраняване на проблеми

| Проблем | Решение |
|---------|---------|
| Дисплеят е бял/празен | Провери свързването, особено CS, DC, RST пиновете |
| Няма WiFi връзка | Провери SSID/парола в config.h |
| "No data" на екрана | Провери интернет връзката; външният източник може да е недостъпен |
| Грешни показания от сензора | Провери I2C свързването (SDA→D2, SCL→D1) |

## Лиценз

MIT лиценз - виж [LICENSE](LICENSE) за детайли.

## Принос

Pull request-и са добре дошли! Моля, запознай се със стила на съществуващия код и тествай промените си преди изпращане.

## Благодарности

> 🤖 **Разкритие:** Това е предимно vibe-coded проект. Аз измислих първоначалната идея и направих базов работещ прототип. После различни AI асистенти (GitHub Copilot, Claude) генерираха по-голямата част от кода, докато аз давах напътствия, правех код ревюта и произнасях нецензурни думички по време на дебъгване. Човешки принос: ~15-20%.

---

Направено с ❤️ в България 🇧🇬
