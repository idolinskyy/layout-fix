# layout-fix

Фоновий демон для X11 і Cinnamon. Якщо слово набране не в тій розкладці, він стирає його на пробілі, перемикає джерело вводу і набирає те саме слово в іншій розкладці. Пара мов: англійська (`us`) і українська (`ua`).

Натискання нікуди не записуються. У журнал потрапляє лише факт виправлення. Символи видно в stderr тільки з прапорцем `--debug-keys`.

Керування з клавіатури, без вікна:

- `Pause` ставить демон на паузу і знімає її. Перед паролем увімкніть паузу.
- `Shift+Pause` скасовує останнє виправлення, якщо після нього ще не почато наступне слово.

## Збірка

Потрібні пакети: `build-essential`, `cmake`, `pkg-config`, `libx11-dev`, `libxtst-dev`, `libxkbcommon-dev`, `libglib2.0-dev`, `hunspell-en-us`. Для морфології потрібен також `libhunspell-dev`; без нього демон перевіряє точні основи з `.dic`. Український словник демон бере з `hunspell-uk`, якщо пакет встановлений, інакше з `data/dicts/uk_UA.dic` (MPL-2.0, LibreOffice dictionaries). Файл `.aff` автоматично підхоплюється поруч із вибраним `.dic`.

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

Термінали `gnome-terminal`, `terminator`, `xterm`, а також `cinnamon-screensaver` і `gcr-prompter`, у списку винятків. Свій список лежить у `~/.config/layout-fix/layout-fix.conf`. Зразок є в `config/layout-fix.conf`. Ім'я вікна береться з `WM_CLASS` (`xprop`). Діалог пароля polkit малюється всередині Cinnamon і в цей список не потрапляє: перед ним увімкніть паузу.

Окреме коротке слово не чіпається. Контекст із двох або трьох сусідніх слів може виправитися разом, якщо цільовий словник приймає всі слова, а поточний не приймає хоча б одне. З `libhunspell-dev` враховуються словоформи за `.aff`; без нього працює точний збіг основи.

## Автозапуск

Один спосіб: user-сервіс systemd. Бінарник має лежати в `/usr/local/bin` (встановлення просить пароль).

```bash
sudo cmake --install build
mkdir -p ~/.config/systemd/user
cp data/layout-fix.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now layout-fix.service
```

`data/layout-fix.desktop` у `~/.config/autostart/` не класти. Файл лишається в репозиторії й потрапляє в меню після `cmake --install`, але разом із сервісом дає другий запуск.
