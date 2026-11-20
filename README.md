# layout-fix

Фоновий демон для X11 і Cinnamon. Якщо слово набране не в тій розкладці, він стирає його на пробілі, перемикає джерело вводу і набирає те саме слово в іншій розкладці. Пара мов: англійська (`us`) і українська (`ua`).

Натискання нікуди не записуються. У журнал потрапляє лише факт виправлення. Символи видно в stderr тільки з прапорцем `--debug-keys`.

Керування з клавіатури, без вікна:

- `Pause` ставить демон на паузу і знімає її. Перед паролем увімкніть паузу.
- `Shift+Pause` скасовує останнє виправлення, якщо після нього ще не почато наступне слово.

## Збірка

Потрібні пакети: `build-essential`, `cmake`, `pkg-config`, `libx11-dev`, `libxtst-dev`, `libxkbcommon-dev`, `libglib2.0-dev`, `hunspell-en-us`. Український словник демон бере з `hunspell-uk`, якщо пакет встановлений, інакше з `data/dicts/uk_UA.dic` (MPL-2.0, LibreOffice dictionaries).

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/layout-fix --check
```

`--check` з'єднується з X11, читає групи клавіатури і словники, потім виходить. Слухати клавіші він не починає.

## Запуск

У параметрах Cinnamon мають бути розкладки English (US) і Ukrainian. Російська, якщо вона теж увімкнена, не виправляється.

```bash
./build/layout-fix
```

Другий запуск нічого не робить: уже працює один примірник.

Термінали `gnome-terminal`, `terminator` і `xterm` у списку винятків. Свій список лежить у `~/.config/layout-fix/layout-fix.conf`. Зразок є в `config/layout-fix.conf`. Ім'я вікна береться з `WM_CLASS` (`xprop`).

Слово коротше за 3 літери не чіпається. Якщо набране вже є в словнику поточної мови, розкладка теж не змінюється.

## Автозапуск

Після встановлення в `/usr/local`:

```bash
cmake --install build
mkdir -p ~/.config/autostart ~/.config/systemd/user
cp data/layout-fix.desktop ~/.config/autostart/
cp data/layout-fix.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now layout-fix.service
```

Достатньо одного зі способів: або пункт автозапуску Cinnamon, або user-сервіс systemd.
