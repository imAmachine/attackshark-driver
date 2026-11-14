# attackshark-driver

Нативный Linux-драйвер для периферии Attack Shark, которая официально
поддерживается только веб-драйверами (WebHID). Работает напрямую через
`hidraw`, без Wine, браузера и проприетарного софта.

## Поддерживаемые устройства

| Устройство | VID:PID | Платформа |
|---|---|---|
| Attack Shark R85 HE | `3151:5029` | Royuan RY5088, GearHub |
| Attack Shark R11 Ultra (2.4 ГГц) | `3554:fb43`, `3554:fb44` | Compx, Control HUB |
| Attack Shark R11 Ultra (кабель) | `3554:f5f6` | Compx, Control HUB |
| Attack Shark X11 Ultra | `3554:f517`, `3554:f515` | Compx, Control HUB |
| прочие клавиатуры Royuan | `3151:*` | GearHub |

Устройства определяются по дескриптору HID (коллекция `0xFFFF/0x02` у
клавиатур и `0xFF02` у мышей), поэтому новые PID в этих семействах обычно
работают без правок кода.

## Возможности

**Мышь (R11 Ultra и родственные Compx):**

- `info` — прошивка, версия донгла, состояние связи, батарея, DPI, LOD,
  debounce, motion sync, angle snap, ripple, подсветка, профиль;
- `battery`, `dpi`, `rate`, `lod`, `debounce`, `motion-sync`, `angle-snap`,
  `ripple`, `sleep`, `effect`, `light`;
- `dump`, `backup`, `restore` — чтение и запись 256-байтного блока настроек;
- `raw` — произвольная 16-байтная команда (отладка).

**Клавиатура (R85 HE и родственные Royuan HE):**

- `info` — прошивка, профиль, частота опроса, debounce, режим магнитов,
  подсветка;
- `light` — режим, яркость, скорость, цвет, dazzle;
- `rate`, `profile`, `debounce`, `magmode`;
- `travel` — ход срабатывания, отпускания, Rapid Trigger, deadzone для всех
  клавиш (без аргументов показывает текущие значения). Шаг значений драйвер
  определяет сам по версии прошивки: 0.1, 0.01 или 0.005 мм;
- `multi` — сырая таблица магнитов по параметру;
- `stream` — живой мониторинг хода клавиш (аналоговый поток);
- `reset` — сброс к заводским настройкам.

Любая команда поддерживает `--json`, все команды можно выполнять из файла
через `attackshark apply FILE`.

## Сборка и установка

```sh
make            # соберёт ./attackshark, нужен только C-компилятор
make test       # проверка кодеков DPI, частоты опроса и хода клавиш
sudo make install
sudo udevadm control --reload && sudo udevadm trigger
```

`make install` ставит бинарник в `/usr/local/bin`, правила udev
(`/etc/udev/rules.d/99-attackshark.rules`) и юнит
`/etc/systemd/system/attackshark.service`.

Зависимостей нет: используется только `hidraw`, `ioctl` и `select`.

## Быстрый старт

```sh
attackshark list
attackshark mouse info
attackshark keyboard info

attackshark mouse rate set 8000
attackshark mouse dpi set 1 1600
attackshark mouse motion-sync on
attackshark mouse light --mode 1 --color ff0000 --brightness 6 --speed 3

attackshark keyboard rate 8000
attackshark keyboard light --mode wave --brightness 3 --speed 2 --color 00a0ff
attackshark keyboard travel --actuation 1.5 --rt --rt-press 0.2 --deadzone 0.2
```

Полный список: `attackshark --help`.

## Профиль при загрузке

`etc/attackshark.conf` — пример конфигурации: по одной команде CLI на строку.
Установленный юнит применяет его при старте системы (`--optional`: если
устройство не подключено, остальные строки всё равно выполняются).

```ini
mouse rate set 8000
mouse dpi set 1 800
keyboard light --mode wave --brightness 3 --speed 2 --color 00a0ff
keyboard travel --actuation 1.5 --rt --rt-press 0.2 --deadzone 0.2
```

```sh
sudo cp etc/attackshark.conf /etc/attackshark.conf
sudo systemctl enable --now attackshark.service
```

## Как это работает

Драйвер не является модулем ядра: он использует штатный `hidraw` и
вендорские HID-коллекции, которые ядро и так пробрасывает в userspace.
Кратко:

- **R11 Ultra** — «Control HUB» протокол: 16-байтные пакеты с report ID 8,
  команды чтения/записи EEPROM, DPI, частоты опроса, подсветки. Формат,
  контрольные суммы и коды частот — в `docs/PROTOCOL.md`.
- **R85 HE** — «GearHub» протокол: 64-байтные feature-отчёты, команда в
  байте 0, контрольная сумма `0xFF - sum(...)` в байте 7 (подсветка — в
  байте 8), плюс аналоговый поток хода клавиш через report ID 5.

## Ограничения и состояние проверки

- Мышь R11 Ultra проверена на реальном образце (`3554:fb44`/`fb43`):
  handshake, состояние связи, прошивка v3.01, батарея и напряжение,
  профиль, DPI, частота опроса, LOD, debounce, motion sync, angle snap,
  ripple, подсветка (чтение и запись), а также резервное копирование и
  восстановление всех 256 байт настроек — побайтно идентично. Кодек DPI
  сверен с эталонными записями прошивки, включая `42000 -> a3 a3 55 ba`.
- Клавиатура R85 HE проверена полностью: чтение состояния, подсветка,
  частота опроса, профиль, debounce, режим магнитов, ход клавиш и Rapid
  Trigger (запись + чтение подтверждены), сброс к заводским настройкам.
- Параметры мыши (DPI, частота, LOD и т.д.) читаются и пишутся только когда
  мышь включена: донгл отвечает за связь, но настройки хранятся в мыши.
  При выключенной мыши `mouse info` показывает `link offline`.
- Конфигурационный эндпоинт клавиатуры может «зависнуть», если слать
  запросы слишком часто (это умеет и OpenRGB при старте). Переподключение
  клавиатуры возвращает его в строй; драйвер сам читает не больше одного
  отчёта на команду.
- Заводские значения R85 HE: actuation 2.0 мм, release 2.0 мм, deadzone
  0.3 мм, Rapid Trigger выключен. `keyboard reset` возвращает их.
- Не реализованы: макросы, переназначение клавиш/кнопок, OLED/экранные
  эффекты, калибровка магнитов, обновление прошивки. Это отдельный объём
  работы.

## Диагностика

**«permission denied»** — не установлены udev-правила или пользователь не
перелогинился после их установки:

```sh
sudo cp udev/99-attackshark.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

**`link offline`, нет батареи/DPI** — мышь выключена или спит. Включите её
и пошевелите, затем повторите команду.

**Клавиатура: «configuration endpoint did not answer»** — переподключите
клавиатуру по USB. Если до этого вы меняли ход клавиш, после переподключения
проверьте `attackshark keyboard travel` и при необходимости верните заводские
значения (`--actuation 2.0 --release 2.0 --deadzone 0.3`). Если помогает только закрытие другой программы, значит
устройство уже открыто (например, OpenRGB держит `/dev/hidraw6`).

**OpenRGB** определяет R85 HE как Skyloong GK68HE Pro и занимает ту же
коллекцию. Совместная работа возможна, но при зависании конфигурационного
эндпоинта закройте OpenRGB и переподключите клавиатуру.

## Структура

```
include/attackshark.h   API драйвера
src/hidraw.c            поиск hidraw-узлов, feature/interrupt обмен
src/mouse.c             протокол мыши (Control HUB)
src/keyboard.c          протокол клавиатуры (GearHub)
src/main.c              CLI
tests/test_codec.c      тесты кодеков
udev/, systemd/, etc/   установка и автозапуск
docs/PROTOCOL.md        описание протоколов
tools/                  скрипты, которыми протоколы разбирались
```
