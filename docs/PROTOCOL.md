# Протоколы Attack Shark

Всё, что нужно для воспроизведения драйвера. Пометки: **[п]** — проверено на
железе в этом проекте, **[в]** — проверено авторами сторонних проектов,
**[?]** — выведено из вендорского JS, на железе не подтверждено.

## 1. R11 Ultra — Compx / Control HUB (VID 3554)

### Транспорт **[п]**

Вендорская коллекция: usage page `0xFF02`, report ID `8`, 16 байт,
interrupt IN + control. Открывается как `/dev/hidrawN` интерфейса с этой
коллекцией.

```
запись: write(fd, [0x08] ++ packet16)
чтение:  input report, hidraw отдаёт report ID первым байтом
```

### Bluetooth (HOGP): вендорского канала нет **[п]**

R11 Ultra по Bluetooth виден как `0005:3554:f533`, `/dev/hidrawN` создаётся
ядром через `uhid`, но настроек на этом транспорте не бывает:

- Report Map (характеристика `0x2A4B` HID-сервиса `0x1812`) — 202 байта и
  содержит только коллекции: клавиатура (report ID 1), потребительские
  клавиши (2), системные клавиши (3), мышь (4). Usage page `0xFF02` и report
  ID 8 отсутствуют, поэтому писать вендорские пакеты некуда: `hidraw`
  пропускает только объявленные отчёты.
- В GATT только GAP, GATT, Device Information (`0x180A`), Battery (`0x180F`)
  и HID (`0x1812`) — вендорского сервиса для конфигурации нет.
- Заряд доступен через Battery Service (`0x2A19`), BlueZ показывает его в
  `bluetoothctl info` и `upower -d`.

Отсюда правило драйвера: узел с `HID_ID` на шине `0005` (или без коллекции
`0xFF02`/`0xFFFF`) показывается в `list`, но команды настроек возвращают
`ENOTSUP` с подсказкой про приёмник 2.4 ГГц или кабель. Приёмник и кабель
вендорскую коллекцию отдают, протокол на них описан выше.

Пакет:

```
[0]     команда
[1]     0
[2..3]  адрес EEPROM, big-endian
[4]     длина payload (+128 для keyboard-класса)
[5..14] payload
[15]    контрольная сумма
```

Контрольная сумма:

```c
sum = Σ packet[0..14];
packet[15] = (85 - (sum & 0xFF) - 8) & 0xFF;   /* 8 == report ID */
```

Ответ: те же поля, payload начинается с байта 5 (после `[0]=cmd, [1]=status,
[2..3]=addr, [4]=len`). **`[1]=0` — данные, `[1]=1` — команда понята, но не
поддерживается.** Устройство отвечает на первые 3 байта запроса (5 байт для
команд flash).

Чтение нестабильно на 2.4 ГГц: вендор повторяет пакет до 5 раз с окном
~200–250 мс и принимает ответ только при совпадении эха. Так же сделано в
`src/mouse.c`.

### Handshake **[п]**

Команда `1`, payload 8 байт: 4 случайных + 4 нуля, `[4]=8`.
Ответ:

```
[1]  status (0)
[9]  cid
[10] mid
[11] type   (0/1/4/5 — wireless, 2/3 — wired, 6 — charging base)
```

Для `3554:fb44` получено `cid=124 (0x7c), mid=3, type=5`, для `3554:fb43` в
режиме зарядки по кабелю — `type=2` (wired 1000). По `cfg.json`
вендора `cid 124 / mid 3` — сенсор 3950, максимум 42000 DPI, 5 клавиш,
6 профилей DPI — совпадает с R11 Ultra.

### Команды **[?]** (из минифицированного бандла controlhub.top)

| # | Имя | # | Имя |
|---:|---|---:|---|
| 1 | EncryptionData (handshake) | 14 | GetCurrentConfig |
| 2 | PCDriverStatus | 15 | SetCurrentConfig |
| 3 | DeviceOnLine | 18 | ReadVersionID |
| 4 | BatteryLevel | 20/21 | Set/Get4KDongleRGB |
| 7 | WriteFlashData | 22/23 | Set/GetLongRangeMode |
| 8 | ReadFlashData | 24/25 | Set/GetDongleRGBBarMode |
| 9 | ClearSetting | 29 | GetDongleVersion |
| 10 | StatusChanged | 44/45 | Dongle3 RGB |

`GetCurrentConfig` — payload `[5] = profile`.

### Карта EEPROM (256 байт, на профиль) **[в]**

| Смещение | Поле | Смещение | Поле |
|---:|---|---:|---|
| 0 | ReportRate | 96 | KeyFunction |
| 2 | maxDpiStage | 160 | Light (7 байт + состояние) |
| 4 | CurrentDPI | 169 | DebounceTime |
| 8 | KeyOperation | 171 | MotionSync |
| 10 | LOD | 173 | SleepTime |
| 12 | DPIValue (4 байта × 6) | 175 | AngleSnap |
| 44 | DPIColor | 177 | Ripple |
| 76 | DPIEffectMode | 181/183 | PerformanceState/Performance |
| 78 | DPIEffectBrightness | 185 | SensorMode |
| 80 | DPIEffectSpeed | 225 | SensorFPS20K |
| 82 | DPIEffectState | 6912 | Sensor3955DPI |

Одиночные настройки хранятся парами `[value, 85-value]`.
DPI-запись — один 4-байтный блок со своей суммой.

### Частота опроса **[п]**

Код: `hz > 1000 ? hz/125 : 1000/hz`.

| Hz | код | Hz | код |
|---:|---:|---:|---:|
| 125 | 0x08 | 1000 | 0x01 |
| 250 | 0x04 | 2000 | 0x10 |
| 500 | 0x02 | 4000 | 0x20 |
| | | 8000 | 0x40 |

### DPI **[п]**

```
low  (50..30000, шаг 50):    val = dpi/50 - 1
high (30100..60000, шаг 100): val = (dpi/2)/50 - 1, dpiex = 17

r[0] = val & 0xFF
r[1] = val & 0xFF              /* Y; X/Y совпадают, если не разделены */
r[2] = (hi<<2) | (hi<<6) | dpiex | (dpiex<<4)
r[3] = 85 - (r[0]+r[1]+r[2])
```

`hi = val >> 8` дублируется в битах 2–3 и 6–7; DPIex — в битах 0 и 4.
Биты DPIex — не флаг шага, а множитель: бит 0 умножает X, бит 4 — Y на 2
(поэтому 17 = 0x11 даёт ×2 для обеих осей).

Эталонные записи (совпадают с прошивкой): 800 → `0f 0f 00 37`, 1600 →
`1f 1f 00 17`, 3200 → `3f 3f 00 d7`, 5600 → `6f 6f 00 77`, 8000 →
`9f 9f 00 17`, 42000 → `a3 a3 55 ba`.

### Подсветка **[?]**

Смещение 160, 7 байт:

```
[0] mode     [1] R   [2] G   [3] B   [4] speed   [5] brightness
[6] checksum = 85 - (0..5)
```

Включение/выключение — отдельная пара `[v, 85-v]` по смещению 167.
Индикатор DPI — блок 76: четыре пары `[mode, brightness, speed, state]`.

### Батарея **[?]**

`Le[6]==1` — зарядка; `Le[9]==1` — уровень в `Le[10]`; иначе уровень
`Le[5]`, напряжение `(Le[7]<<8)|Le[8]` мВ.

---

## 2. R85 HE — Royuan RY5088 / GearHub (VID 3151)

### Транспорт **[п]**

Коллекция usage page `0xFFFF`, usage `0x02`, feature-отчёт **64 байта без
report ID**, интерфейс 2 (`/dev/hidraw6` у тестового экземпляра).

```
запись: ioctl(HIDIOCSFEATURE(65), [0x00] ++ payload64)
чтение:  ioctl(HIDIOCGFEATURE(65), buf65)   /* buf[0] — номер отчёта 0 */
```

Аналоговый поток: отдельная коллекция `0xFFFF/0x01`, report ID 5, 31 байт
на интерфейсе 1.

Payload:

```
[0]    команда
[1..]  аргументы
[7]    checksum = 0xFF - sum([0..6])       (Bit7)
[8]    checksum = 0xFF - sum([0..7])       (Bit8, подсветка)
```

### Команды RY5088 (GearHub «Et») **[?]**

| SET | # | GET | # |
|---|---:|---|---:|
| RESET | 0x01 | RF_VERSION | 0x80 |
| REPORT_RATE | 0x03 | REPORT_RATE | 0x83 |
| PROFILE | 0x04 | PROFILE | 0x84 |
| DEBOUNCE | 0x06 | DEBOUNCE | 0x86 |
| LEDPARAM | 0x07 | LEDPARAM | 0x87 |
| KBOPTION | 0x09 | KEYMATRIX | 0x8A |
| KEYMATRIX | 0x0A | USB_VERSION | 0x8F |
| FNKEYMATRIX | 0x10 | MAGNETISM_MODE | 0x9D |
| SLEEPTIME | 0x11 | MULTI_MAGNETISM | 0xE5 |
| MAGNETISM_REPORT | 0x1B | REV | 0xF0 |
| KEY_MAGNETISM_MODE | 0x1D | | |
| MULTI_MAGNETISM | 0x65 | | |

Перед первой командой вендор ждёт ~1 c после открытия устройства; после
записи — 10–100 мс. Ответ приходит тем же feature-отчётом.

### Частота опроса **[?]**

`[1]=0` (у Et не используется), `[2]= index`:

| 8000 | 4000 | 2000 | 1000 | 500 | 250 | 125 |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | 1 | 2 | 3 | 4 | 5 | 6 |

Чтение: ответ `[2]` — индекс.

### Подсветка **[?]**

`SET_LED` (Bit8):

```
[1] mode   [2] 4-speed   [3] brightness 0..4
[4] (option << 4) | (dazzle ? 8 : 7)   [5..7] R G B
```

Режимы 0..25: off, always-on, breath, neon, wave, ripple, raindrop, snake,
press-action, coverage, sine-wave, kaleidoscope, line-wave, user-picture,
laser, circle-wave, dazzling, rain-down, meteor, press-action-off,
music-follow-3, screen-color, music-follow-2, train, fireworks, user-color.

### Ход клавиш и Rapid Trigger **[?]**

`SET_MULTI_MAGNETISM` (0x65), страницы по 56 значений:

```
[1] param (0 actuation, 1 release, 2 RT press, 3 RT release,
          6 deadzone, 7 option)
[2] 1 = все клавиши
[3] номер страницы
[4] 1 = сохранить в flash (на последней странице)
[8..63] значения
```

Чтение `0xE5`: `[1]=param, [2]=1, [3]=page`; ответ — **сырые 64 байта**
значений без заголовка.

Ширина значений зависит от версии прошивки (`GET_USB_VERSION`, u32 LE в
байтах 1..4):

| версия | шаг | множитель | формат |
|---:|---:|---:|---|
| < 768 | 0.1 мм | 10 | u8 |
| 768..1279 | 0.01 мм | 100 | u16 LE |
| >= 1280 | 0.005 мм | 200 | u16 LE |

R85 HE сообщает версию 3123, то есть 0.005 мм и u16: actuation 2.0 мм — это
`400` (`90 01`), deadzone 0.3 мм — `60` (`3c 00`). В u8-режиме:
`raw = round(mm*10) - 1`, deadzone `raw = 40 - round(mm*10) - 1`; в u16-режиме
`raw = round(mm*mult)` и для deadzone тоже, без смещения 40. Запись u8-значений
в u16-прошивку делает срабатывание почти нулевым — именно так и выглядит
«клавиши срабатывают от касания».

Бит 7 байта option включает Rapid Trigger,
бит 6 — «полный ход снизу», младший ниббл — режим (0 normal, 2 DKS, 3 MT,
4 TGL hold, 5 TGL dots, 7 snap).

### Аналоговый поток **[?]**

Включение: `SET_MAGNETISM_REPORT` (0x1B), `[1]=1`. Затем читать report ID 5
(31 байт) с интерфейса `0xFFFF/0x01`; пакет содержит `0x1B, lo, hi, keyId`,
`raw = lo | hi<<8`, полный ход ≈ 350 единиц ≈ 3,4 мм. Пока поток включён,
обычная печать может не работать — выключайте `[1]=0` после мониторинга.

## Источники

- `https://controlhub.top/AttackShark/` — вендорский WebHID-драйвер мыши
  (`cfg.json` с VID/PID и таблицей устройств).
- `https://qmk.top/` — GearHub, вендорский драйвер клавиатур.
- MontyMcK/attack-shark-x11-ultra-linux — `PROTOCOL.md` для X11 Ultra.
- srineshr1/keyboard-driver — протокол X68HE, того же семейства Royuan.
- jluiscg/attack-shark-r6-webhid — та же схема пакетов для R6.
