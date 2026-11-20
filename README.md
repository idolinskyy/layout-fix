# layout-fix

Фоновий демон для X11 і Cinnamon. Якщо слово набране не в тій розкладці, він стирає його на пробілі, перемикає джерело вводу і набирає ті самі клавіші іншою мовою. Підтримуються English (US), Ukrainian і Russian; перемикання відбувається лише коли підходить рівно один словник.

Демон читає події клавіатури в межах X11-сесії, тримає поточне слово в пам'яті й не записує його на диск. Звичайний журнал не містить набраного тексту. `--debug-keys` виводить символи в stderr, тому не вмикай його під час введення паролів чи інших секретів.

Керування з клавіатури й меню трея. Значок показує код поточної мови; у паузі він сірий. У меню є Pause/Resume, вибір джерела Cinnamon, налаштування, About і вихід:

- `Pause` ставить демон на паузу і знімає її. Перед паролем увімкніть паузу.
- `Shift+Pause` скасовує останнє виправлення, якщо після нього ще не почато наступне слово.

## Збірка

Потрібні пакети: `build-essential`, `cmake`, `pkg-config`, `libx11-dev`, `libxtst-dev`, `libxkbcommon-dev`, `libglib2.0-dev`, `hunspell-en-us`, `hunspell-uk`, `hunspell-ru`, `libhunspell-dev`. Без `libhunspell-dev` демон перевіряє точні основи з `.dic`. Файли `.aff` автоматично підхоплюються поруч із `.dic`. Без Qt 6.12+ Widgets tray menu недоступне, але гарячі клавіші та автоматичне виправлення працюють.

Для меню трея потрібен Qt 6.12 або новіший із модулем Widgets. Передай каталог `gcc_64` через `CMAKE_PREFIX_PATH`; для запуску додай його до `QT_PREFIX`:

```bash
export QT_PREFIX="$HOME/Qt/6.12.3/gcc_64"
cmake -S . -B build -DCMAKE_PREFIX_PATH="$QT_PREFIX"
cmake --build build
ctest --test-dir build --output-on-failure
QT_PREFIX="$QT_PREFIX" ./build/layout-fix --check
QT_PREFIX="$QT_PREFIX" ./build/layout-fix
```

Для Linux-артефактів релізу виконай `scripts/build-release.sh`. Скрипт перевіряє словники, збирає Release, проганяє тести та створює versioned `.tar.gz` і `.deb` у `dist/`; версію бере з CMake. До кожного файла додається `.sha256`. Пакети не містять системних бібліотек. Для встановлення Debian-пакета:

```bash
sudo apt install ./dist/layout-fix_0.1.0_amd64.deb
systemctl --user daemon-reload
systemctl --user enable --now layout-fix.service
```

Якщо Qt встановлена не в стандартний каталог, задай шлях для прямого запуску:

```bash
QT_PREFIX="$HOME/Qt/6.12.3/gcc_64" /usr/bin/layout-fix
```

Для user-service задай той самий шлях у drop-in через `systemctl --user edit layout-fix`:

```ini
[Service]
Environment=QT_PREFIX=%h/Qt/6.12.3/gcc_64
```

`VERSION` можна передати в `scripts/build-release.sh`, якщо треба перевизначити CMake-версію.

`--check` з'єднується з X11, читає групи клавіатури і словники, потім виходить. Слухати клавіші він не починає.

## Запуск

У параметрах Cinnamon мають бути розкладки English (US) і Ukrainian. Якщо увімкнена Russian, демон також враховує її; коли словник приймає слово у двох мовах, перемикання не відбувається.

```bash
./build/layout-fix
```

Другий запуск нічого не робить: уже працює один примірник.

У меню трея можна призупинити або відновити роботу, вибрати доступне джерело Cinnamon, відкрити вікно налаштувань, переглянути About чи вийти. About показує версію та ліцензії коду й fallback-словника.

У налаштуваннях доступні мінімальна довжина слова, Pause/Undo hotkeys і список виключених вікон. Збережені зміни записуються в user config і застосовуються одразу. Якщо запускати з явним `--config PATH`, форма зберігає саме в цей файл; інакше використовує `~/.config/layout-fix/layout-fix.conf`, не перезаписуючи `/etc/xdg` config.

Термінали `gnome-terminal`, `terminator`, `xterm`, а також `cinnamon-screensaver` і `gcr-prompter`, у списку винятків. Свій список лежить у `~/.config/layout-fix/layout-fix.conf`. Зразок є в `config/layout-fix.conf`. Ім'я вікна береться з `WM_CLASS` (`xprop`). Діалог пароля polkit малюється всередині Cinnamon і в цей список не потрапляє: перед ним увімкніть паузу.

Окреме коротке слово не чіпається. Контекст із двох або трьох сусідніх слів може виправитися разом, якщо цільовий словник приймає всі слова, а поточний не приймає хоча б одне. З `libhunspell-dev` враховуються словоформи за `.aff`; без нього працює точний збіг основи.

Відоме обмеження: багатослівне виправлення може втратити пробіли й склеїти слова. Наприклад, `Ye s oj` може стати `Нуі що`, а `S oj` — `Іщо`. Автоматичне виправлення виділеного тексту ще не реалізоване.

Після зміни списку джерел у Cinnamon демон перечитує XKB і `GetInputSources`. Поки індекси `us`/`ua` та ввімкненої `ru` не узгоджені, автоматичне виправлення призупиняється.

## Ліцензія

Код проєкту поширюється за GNU GPL версії 3 або, на вибір користувача, будь-якої пізнішої версії. Текст ліцензії міститься у файлі `LICENSE`. Запасний словник `data/dicts/uk_UA.dic` має окрему ліцензію MPL-2.0; його copyright notice збережено в `data/dicts/uk_UA.COPYRIGHT`.

## Автозапуск

Для автозапуску використовуй user-сервіс systemd; CMake та Debian-пакет встановлюють launcher у відповідний `bin` каталог.

```bash
sudo cmake --install build
mkdir -p ~/.config/systemd/user
cp build/layout-fix.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now layout-fix.service
```

Після `cmake --install build` скопіюй згенерований `build/layout-fix.service` у `~/.config/systemd/user/`. Не додавай desktop autostart разом із сервісом: це створить другий запуск.
