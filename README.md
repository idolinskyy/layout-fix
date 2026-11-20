# layout-fix

Фоновий демон для X11 і Cinnamon. Якщо слово набране не в тій розкладці, він стирає його на пробілі, перемикає джерело вводу і набирає ті самі клавіші іншою мовою. Підтримуються English (US), Ukrainian і Russian; перемикання відбувається лише коли підходить рівно один словник.

Демон читає події клавіатури в межах X11-сесії, тримає поточне слово в пам'яті й не записує його на диск. Звичайний журнал не містить набраного тексту. `--debug-keys` виводить символи в stderr, тому не вмикай його під час введення паролів чи інших секретів.

Керування з клавіатури. Меню значка в треї показує поточну мову та стан, дає перемкнути джерело вводу, відкрити конфіг або завершити роботу:

- `Pause` ставить демон на паузу і знімає її. Перед паролем увімкніть паузу.
- `Shift+Pause` скасовує останнє виправлення, якщо після нього ще не почато наступне слово.

## Збірка

Потрібні пакети: `build-essential`, `cmake`, `pkg-config`, `libx11-dev`, `libxtst-dev`, `libxkbcommon-dev`, `libglib2.0-dev`, `hunspell-en-us`, `hunspell-uk`, `hunspell-ru`, `libhunspell-dev`. Без `libhunspell-dev` демон перевіряє точні основи з `.dic`. Файли `.aff` автоматично підхоплюються поруч із `.dic`. Для меню трея потрібен Qt 6.12 або новіший Qt 6 із модулем Widgets; без Qt демон лишається керованим клавішами.

Щоб вибрати встановлену Qt-версію, передай її `gcc_64` каталог як `CMAKE_PREFIX_PATH`. Потрібен Qt 6.12 або новіший:

```bash
QT_PREFIX="$HOME/Qt/6.12.3/gcc_64"
cmake -S . -B build -DCMAKE_PREFIX_PATH="$QT_PREFIX"
cmake --build build
```

Щоб вибрати конкретну Qt-інсталяцію, задай `QT_PREFIX` під час конфігурації. Версія має бути не нижча за 6.12:

```bash
QT_PREFIX="$HOME/Qt/6.12.3/gcc_64"
cmake -S . -B build -DCMAKE_PREFIX_PATH="$QT_PREFIX"
cmake --build build
QT_PREFIX="$QT_PREFIX" ./build/layout-fix
```

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/layout-fix --check
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

У меню трея можна призупинити або відновити роботу, вибрати доступне джерело Cinnamon, відкрити конфіг у системному редакторі чи вийти. Після редагування конфігу перезапусти демон.

Термінали `gnome-terminal`, `terminator`, `xterm`, а також `cinnamon-screensaver` і `gcr-prompter`, у списку винятків. Свій список лежить у `~/.config/layout-fix/layout-fix.conf`. Зразок є в `config/layout-fix.conf`. Ім'я вікна береться з `WM_CLASS` (`xprop`). Діалог пароля polkit малюється всередині Cinnamon і в цей список не потрапляє: перед ним увімкніть паузу.

Окреме коротке слово не чіпається. Контекст із двох або трьох сусідніх слів може виправитися разом, якщо цільовий словник приймає всі слова, а поточний не приймає хоча б одне. З `libhunspell-dev` враховуються словоформи за `.aff`; без нього працює точний збіг основи.

Після зміни списку джерел у Cinnamon демон перечитує XKB і `GetInputSources`. Поки індекси `us`/`ua` не узгоджені, автоматичне виправлення призупиняється.

## Ліцензія

Код проєкту поширюється за GNU GPL версії 3 або, на вибір користувача, будь-якої пізнішої версії. Текст ліцензії міститься у файлі `LICENSE`. Запасний словник `data/dicts/uk_UA.dic` має окрему ліцензію MPL-2.0; його copyright notice збережено в `data/dicts/uk_UA.COPYRIGHT`.

## Автозапуск

Один спосіб: user-сервіс systemd. Бінарник має лежати в `/usr/local/bin` (встановлення просить пароль).

```bash
sudo cmake --install build
mkdir -p ~/.config/systemd/user
cp build/layout-fix.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now layout-fix.service
```

Не копіюй `build/layout-fix.desktop` у `~/.config/autostart/`: разом із сервісом це запустить другий примірник.
