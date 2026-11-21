# layout-fix: архітектура та нотатки розробника

Стан на 2026-11-21 (v0.3.0). Фоновий демон для X11 і Cinnamon; `build/` і `dist/` ігноруються в git.

## Що це

Фоновий C++20-демон для X11 і Cinnamon. На межі слова він перевіряє набрані клавіші за словниками Hunspell (English US, Ukrainian, опційно Russian) і при єдиному словниковому збігу перемикає джерело вводу Cinnamon та підставляє слово або контекст до 4 слів. Також підтримує виправлення виділеного тексту й примусове виправлення останнього слова (`Alt+Pause`), скасування (`Shift+Pause`) та сувору паузу (`Pause`).

Натискання не записуються у файл. У звичайному режимі журнал містить лише службові повідомлення; текст клавіш з'являється у stderr тільки з `--debug-keys`.

## Куди дивитися в коді

| Файл                         | Роль                                                                                                   |
| ---------------------------- | ------------------------------------------------------------------------------------------------------ |
| `src/main.cpp`               | аргументи CLI (`--config`, `--check`, `--debug-keys`), single-instance lock у `~/.cache/layout-fix.lock` |
| `src/app.cpp`                | головний `poll`-цикл, рішення, автовиправлення (1–4 слова), примусова конвертація, пауза, скасування   |
| `src/ui/tray.cpp`            | ізольований модуль Qt 6 (трей, меню мов, діалог налаштувань із `QKeySequenceEdit`, About); no-op без Qt |
| `src/tray_state.hpp`         | визначення стану трея (`Active`, `Paused`, `Desynced`, `PausedDesynced`)                               |
| `src/config.cpp`             | пошук, читання й збереження user/XDG конфігу (`dict-*`, `exclude`, `ignore-words`, хоткеї Qt ↔ X11)    |
| `src/input/word_buffer.cpp`  | буфер слова для 3 мов, історія `Stroke`, `build_replacement_text` для багатослівної вставки            |
| `src/input/echoes.hpp`       | звірка відлуння XTest за послідовністю keycode (включно з `optional` пре-відпусканням)                 |
| `src/input/undo_restore.hpp` | вибір безпечного відновлення, якщо скасування не повернуло вихідну групу                               |
| `src/timing.hpp`             | таймаути: 150 мс перемикання, 400 мс відлуння, 250/200/50 мс буфер обміну                              |
| `src/x11/connection.cpp`     | два X11-з'єднання (`ctrl`/`data`) і `abandon_close` без зависання в `XSync`                            |
| `src/x11/listener.cpp`       | пасивне слухання XRecord і коректне завершення по `XRecordEndOfData` (500 мс fallback)                 |
| `src/x11/retype.cpp`         | Backspace, повтор keycode і `Ctrl+C`/`Ctrl+V` через XTest (з пре-відпусканням затиснутих клавіш)       |
| `src/x11/clipboard.cpp`      | читання/володіння X11 `CLIPBOARD` (`UTF8_STRING`/`XA_STRING`), `PRIMARY` і custom targets              |
| `src/x11/focus.cpp`          | `_NET_ACTIVE_WINDOW`, `WM_CLASS` і перевірка винятків вікон                                            |
| `src/layout/groups.cpp`      | XKB-групи (`en`/`uk`/`ru` за клавішею `S`), `translate`, `find_key` і таблиці `LayoutMaps`             |
| `src/layout/cinnamon.cpp`    | D-Bus `org.Cinnamon`: `GetInputSources`, `ActivateInputSourceIndex`, сигнал `InputSourcesChanged`      |
| `src/detect/dictionary.cpp`  | словники Hunspell (`.dic`/`.aff`) із fallback на точні основи                                          |
| `src/detect/detector.cpp`    | `unique_language_candidate` (1–4 слова, пунктуаційні літери, `ignore-words`), `choose_forced_language` |
| `src/detect/selection.cpp`   | конвертація виділеного тексту за `LayoutMaps` і захист від copy-line-on-empty-selection в IDE          |
| `tests/test_core.cpp`        | offline-тести логіки без X-сервера                                                                     |
| `tests/test_x11.cpp`         | ізольований X11-тест під `Xvfb` (`scripts/run-x11-test.sh`) із власним вікном                          |
| `scripts/build-release.sh`   | збірка Release, перевірка словників і `ctest`, пакування `.tar.gz` і `.deb` (+ `.sha256`)              |

## Ключові інваріанти й надійність

- **Безпечне завершення XRecord:** `Ctrl+C` / `SIGTERM` дочитує `XRecordEndOfData` (до 500 мс) і лише тоді закриває X11; при таймауті сокети закриваються без `XSync` (`abandon_close`).
- **Синхронізація XKB ↔ Cinnamon:** підписка на `org.Cinnamon.InputSourcesChanged` перечитує `GetInputSources` і повторно опитує XKB до узгодження індексів `us`/`ua` і ввімкненої `ru`. Поки карти не узгоджені (`!maps_ready`), виправлення вимкнене, а трей показує помаранчевий статус.
- **Стабільність під IBus та XTest:** однослівні виправлення й примусова конвертація останнього слова йдуть через `Retype::replace` (із пре-відпусканням затиснутих модифікаторів/клавіш і `optional`-відлунням); багатослівні контексти (2–4 слова) вставляються атомарно через `Clipboard` + `Ctrl+V` із відкладеним відновленням попереднього буфера, що унеможливлює перестановку пробілів у `ibus`.
- **Безпека вводу:** буфер, контекст і `last_word` скидаються при зміні `window_id`, кліку миші, навігаційних клавішах і сторонніх модифікаторах (крім голих модифікаторів активних хоткеїв). У виключених вікнах (термінали, менеджери паролів, lockscreen) і під час паузи не спрацьовує жодне виправлення.
- **Автотести X11:** `layout-fix-x11-test` відмовляється запускатися без `--isolated-display` і проганяється лише на окремому дисплеї `Xvfb` (`-noreset`, `us,ua,ru`) у власному вікні, не втручаючись у сесію користувача.
- **Збірка з Qt 6:** CMake за замовчуванням шукає системний Qt 6 (`qt6-base-dev`), а при його відсутності підхоплює `QT_PREFIX` або `$HOME/Qt/6.*/gcc_64`. Генерований `data/layout-fix-launcher.in` для системного Qt залишає `QT_PREFIX` порожнім.
- **Автозапуск:** використовувати лише один user-сервіс systemd; desktop autostart разом із сервісом не вмикати.

## Чого навмисно немає

Скорочень на кшталт `thx`, автоматичного навчання на скасуваннях, підтримки Wayland і запису натискань клавіш у файл. Діалог налаштувань охоплює базові параметри; шляхи до словників (`dict-en`, `dict-uk`, `dict-ru`) задаються в конфіг-файлі.
