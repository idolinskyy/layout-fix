#ifdef LAYOUT_FIX_HAVE_QT_TRAY
#include <QApplication>
#include <QAction>
#include <QColor>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFormLayout>
#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QVBoxLayout>
#endif

#include "app.hpp"

#include "detect/detector.hpp"
#include "events.hpp"
#include "input/echoes.hpp"
#include "input/undo_restore.hpp"
#include "input/word_buffer.hpp"
#include "layout/cinnamon.hpp"
#include "layout/groups.hpp"
#include "log.hpp"
#include "timing.hpp"
#include "utf8.hpp"
#include "x11/connection.hpp"
#include "x11/focus.hpp"
#include "x11/listener.hpp"
#include "x11/retype.hpp"

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <array>
#include <chrono>
#include <csignal>
#include <poll.h>
#include <string>
#include <vector>

namespace layoutfix {
namespace {

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) { g_stop = 1; }

struct Undo {
  bool valid = false;
  std::vector<Stroke> original;
  std::vector<Stroke> shown;
  int source_index = -1;
  Lang source_lang = Lang::Other;
  Lang shown_lang = Lang::Other;
};

struct PendingWord {
  std::array<std::string, 3> forms;
  std::vector<Stroke> strokes;
};

const char *cinnamon_source_id(Lang language) {
  switch (language) {
  case Lang::En:
    return "us";
  case Lang::Uk:
    return "ua";
  case Lang::Ru:
    return "ru";
  case Lang::Other:
    return "";
  }
  return "";
}

#ifdef LAYOUT_FIX_HAVE_QT_TRAY
QIcon layout_icon(Lang language, bool paused) {
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  const QColor active_color("#378b57");
  const int gray = qGray(active_color.rgb());
  painter.setBrush(paused ? QColor(gray, gray, gray) : active_color);
  painter.drawEllipse(2, 2, 28, 28);
  painter.setPen(Qt::white);
  painter.setFont(QFont("Sans", 10, QFont::DemiBold));
  const char *code = language == Lang::En ? "en"
                     : language == Lang::Uk ? "uk"
                     : language == Lang::Ru ? "ru"
                                            : "--";
  painter.drawText(pixmap.rect(), Qt::AlignCenter, QString::fromLatin1(code));
  return QIcon(pixmap);
}
#endif

unsigned keycode_of(XConnection &x11, KeySym sym) {
  return XKeysymToKeycode(x11.ctrl(), sym);
}

bool is_command_modifier(unsigned state) {
  return (state & (ControlMask | Mod1Mask | Mod4Mask | Mod5Mask)) != 0;
}

} // namespace

int App::run(Config config, bool check_only) {
  log::set_debug_keys(config.debug_keys);

  XConnection x11;
  if (!x11.open()) {
    return 1;
  }
  Groups groups(x11);
  CinnamonLayout cinnamon;
  if (!groups.init() || !cinnamon.open()) {
    return 1;
  }
  Focus focus(x11);
  focus.install();
  auto source_map_matches = [&] {
    return groups.group_for(Lang::En) == cinnamon.index_of("us") &&
           groups.group_for(Lang::Uk) == cinnamon.index_of("ua") &&
           (cinnamon.index_of("ru") < 0 ||
            groups.group_for(Lang::Ru) == cinnamon.index_of("ru"));
  };
  bool maps_ready = source_map_matches();
  if (!maps_ready) {
    log::error("мапи XKB і джерел Cinnamon не збігаються; виправлення призупинено");
  }

  auto resolve = [](const std::string &configured,
                    const std::vector<std::string> &fallbacks) {
    return resolve_dictionary(configured, fallbacks);
  };
  const auto en_path =
      resolve(config.dict_en, {"/usr/share/hunspell/en_US.dic"});
  const auto uk_path =
      resolve(config.dict_uk,
              {"/usr/share/hunspell/uk_UA.dic",
               LAYOUT_FIX_DATA_DIR "/uk_UA.dic", "data/dicts/uk_UA.dic",
               "../data/dicts/uk_UA.dic", "../../data/dicts/uk_UA.dic"});
  const auto ru_path = resolve("", {"/usr/share/hunspell/ru_RU.dic"});
  Dictionary english_dict;
  Dictionary ukrainian_dict;
  Dictionary russian_dict;
  if (en_path.empty() || !english_dict.load_file(en_path)) {
    log::error("немає англійського словника. Встановіть hunspell-en-us");
    return 1;
  }
  if (uk_path.empty() || !ukrainian_dict.load_file(uk_path)) {
    log::error("немає українського словника. Встановіть hunspell-uk або "
               "залиште data/dicts/uk_UA.dic");
    return 1;
  }
  const bool russian_dict_ready =
      !ru_path.empty() && russian_dict.load_file(ru_path);
  if (cinnamon.index_of("ru") >= 0 && !russian_dict_ready) {
    log::error("увімкнено ru у Cinnamon, але немає ru_RU.dic/.aff");
    return 1;
  }
  const std::array<const Dictionary *, 3> dictionaries{
      &english_dict, &ukrainian_dict,
      russian_dict_ready ? &russian_dict : nullptr};
  log::info("словники: en " + std::to_string(english_dict.size()) + " (" +
            en_path + (english_dict.has_morphology() ? ", Hunspell), uk "
                                                       : ", точні основи), uk ") +
            std::to_string(ukrainian_dict.size()) + " (" +
            uk_path + (ukrainian_dict.has_morphology() ? ", Hunspell)"
                                                        : ", точні основи)"));
  if (russian_dict_ready) {
    log::info("словник: ru " + std::to_string(russian_dict.size()) + " (" +
              ru_path + ", " +
              (russian_dict.has_morphology() ? "Hunspell)" : "точні основи)"));
  }
  if (check_only) {
    if (!maps_ready) {
      return 1;
    }
    log::info("перевірку завершено");
    return 0;
  }

  Retype retype(x11);
  Listener listener(x11);
  if (!retype.init() || !listener.start()) {
    return 1;
  }

  auto pause_key = parse_hotkey(config.pause_key);
  auto undo_key = parse_hotkey(config.undo_key);
  unsigned pause_kc =
      keycode_of(x11, XStringToKeysym(pause_key.keysym.c_str()));
  unsigned undo_kc =
      keycode_of(x11, XStringToKeysym(undo_key.keysym.c_str()));
  const unsigned break_kc = keycode_of(x11, XK_Break);
  std::vector<unsigned> command_keys;
  for (KeySym sym : {XK_Control_L, XK_Control_R, XK_Alt_L, XK_Alt_R, XK_Super_L,
                     XK_Super_R, XK_Meta_L, XK_Meta_R, XK_ISO_Level3_Shift}) {
    if (const unsigned kc = keycode_of(x11, sym); kc != 0) {
      command_keys.push_back(kc);
    }
  }
  std::vector<unsigned> nav_keys;
  for (KeySym sym :
       {XK_Left,     XK_Right,   XK_Up,        XK_Down,     XK_Home,
        XK_End,      XK_Delete,  XK_Escape,    XK_Prior,    XK_Next,
        XK_Insert,   XK_KP_Left, XK_KP_Right,  XK_KP_Up,    XK_KP_Down,
        XK_KP_Home,  XK_KP_End,  XK_KP_Delete, XK_KP_Prior, XK_KP_Next,
        XK_BackSpace}) {
    // BackSpace обробляється окремо, але в цей список не входить.
    if (sym == XK_BackSpace) {
      continue;
    }
    if (const unsigned kc = keycode_of(x11, sym); kc != 0) {
      nav_keys.push_back(kc);
    }
  }

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  WordBuffer buffer;
  Undo undo;
  std::vector<PendingWord> pending_short;
  auto clear_input = [&] {
    buffer.clear();
    pending_short.clear();
  };
  bool paused = false;
  bool injecting = false;
  std::vector<EchoKey> pending_echoes;
  auto inject_deadline = std::chrono::steady_clock::now();
  unsigned long seen_window = 0;

  auto key_in = [](unsigned keycode, const std::vector<unsigned> &keys) {
    for (unsigned key : keys) {
      if (key == keycode) {
        return true;
      }
    }
    return false;
  };

  auto begin_inject = [&](std::vector<EchoKey> echoes) {
    pending_echoes = std::move(echoes);
    injecting = !pending_echoes.empty();
    if (injecting) {
      inject_deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(kEchoTimeoutMs);
    }
  };

  bool refresh_groups_pending = !maps_ready;
  auto map_retry_at = std::chrono::steady_clock::now() +
                      std::chrono::milliseconds(250);

#ifdef LAYOUT_FIX_HAVE_QT_TRAY
  int qt_argc = 1;
  char qt_name[] = "layout-fix";
  char *qt_argv[] = {qt_name, nullptr};
  QApplication qt_app(qt_argc, qt_argv);
  qt_app.setQuitOnLastWindowClosed(false);
  QApplication::setApplicationName(QStringLiteral("layout-fix"));
  QApplication::setApplicationVersion(QStringLiteral(LAYOUT_FIX_VERSION));
  QSystemTrayIcon tray;
  QMenu tray_menu;
  QAction *status_action = tray_menu.addAction("Стан: запускаю…");
  status_action->setEnabled(false);
  tray_menu.addSeparator();
  QAction *pause_action = tray_menu.addAction("Пауза");
  QMenu *language_menu = tray_menu.addMenu("Перемкнути розкладку");
  std::vector<std::pair<Lang, QAction *>> language_actions;
  QAction *settings_action = tray_menu.addAction("Налаштування…");
  QAction *about_action = tray_menu.addAction("Про layout-fix");
  tray_menu.addSeparator();
  QAction *quit_action = tray_menu.addAction("Вийти");
  tray.setContextMenu(&tray_menu);

  auto language_label = [](Lang language) {
    switch (language) {
    case Lang::En:
      return QStringLiteral("English (US)");
    case Lang::Uk:
      return QStringLiteral("Українська");
    case Lang::Ru:
      return QStringLiteral("Русский");
    case Lang::Other:
      return QStringLiteral("невідома");
    }
    return QStringLiteral("невідома");
  };
  auto rebuild_language_menu = [&] {
    language_menu->clear();
    language_actions.clear();
    for (const auto &source : cinnamon.sources()) {
      Lang language = Lang::Other;
      if (source.id == "us") {
        language = Lang::En;
      } else if (source.id == "ua") {
        language = Lang::Uk;
      } else if (source.id == "ru") {
        language = Lang::Ru;
      }
      if (language == Lang::Other || groups.group_for(language) < 0) {
        continue;
      }
      QAction *action = language_menu->addAction(language_label(language));
      action->setCheckable(true);
      language_actions.emplace_back(language, action);
      QObject::connect(action, &QAction::triggered,
                       [&, index = source.index] {
                         clear_input();
                         undo.valid = false;
                         cinnamon.activate(index);
                       });
    }
  };
  auto update_tray = [&] {
    const Lang active_language = groups.language_of(groups.current_group());
    status_action->setText(
        QStringLiteral("Стан: %1 · мова: %2")
            .arg(paused ? QStringLiteral("пауза")
                        : QStringLiteral("працює"),
                 language_label(active_language)));
    pause_action->setText(paused ? "Відновити" : "Пауза");
    for (const auto &[language, action] : language_actions) {
      action->setChecked(language == active_language);
    }
    tray.setIcon(layout_icon(active_language, paused));
    tray.setToolTip(QStringLiteral("layout-fix · %1 · %2")
                        .arg(paused ? QStringLiteral("пауза")
                                    : QStringLiteral("працює"),
                             language_label(active_language)));
    if (!tray.isVisible() && QSystemTrayIcon::isSystemTrayAvailable()) {
      tray.show();
    }
  };
  rebuild_language_menu();
  update_tray();
  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    log::error("системний трей недоступний; керування лишається на клавіатурі");
  }
  QObject::connect(pause_action, &QAction::triggered, [&] {
    paused = !paused;
    clear_input();
    undo.valid = false;
    log::info(paused ? "пауза" : "знову слухаю");
    update_tray();
  });
  QObject::connect(settings_action, &QAction::triggered, [&] {
    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("Налаштування layout-fix"));
    dialog.setMinimumWidth(440);
    auto *form = new QFormLayout;
    auto *minimum_length = new QSpinBox;
    minimum_length->setRange(1, 24);
    minimum_length->setValue(config.min_length);
    form->addRow(QStringLiteral("Мінімальна довжина слова"), minimum_length);

    auto *pause_hotkey = new QLineEdit(QString::fromStdString(config.pause_key));
    pause_hotkey->setPlaceholderText(QStringLiteral("Pause або F8"));
    form->addRow(QStringLiteral("Пауза / продовжити"), pause_hotkey);

    auto *undo_hotkey = new QLineEdit(QString::fromStdString(config.undo_key));
    undo_hotkey->setPlaceholderText(QStringLiteral("Shift+Pause"));
    form->addRow(QStringLiteral("Скасувати останнє виправлення"), undo_hotkey);

    auto *excluded_windows = new QPlainTextEdit;
    excluded_windows->setPlaceholderText(
        QStringLiteral("Одна частина WM_CLASS на рядок"));
    QStringList excluded_lines;
    for (const auto &name : config.exclude) {
      excluded_lines.push_back(QString::fromStdString(name));
    }
    excluded_windows->setPlainText(excluded_lines.join('\n'));
    excluded_windows->setMinimumHeight(120);
    form->addRow(QStringLiteral("Не виправляти у вікнах"), excluded_windows);

    auto *help = new QLabel(QStringLiteral(
        "Зміни збережуться у користувацький конфіг і застосуються одразу."));
    help->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save |
                                        QDialogButtonBox::Cancel);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(help);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                     &QDialog::reject);

    const bool was_paused = paused;
    paused = true;
    clear_input();
    undo.valid = false;
    update_tray();
    const int result = dialog.exec();
    listener.drain();
    listener.take();
    focus.drain();
    clear_input();
    paused = was_paused;
    update_tray();
    if (result != QDialog::Accepted) {
      return;
    }

    const std::string new_pause_text = pause_hotkey->text().trimmed().toStdString();
    const std::string new_undo_text = undo_hotkey->text().trimmed().toStdString();
    const Hotkey new_pause = parse_hotkey(new_pause_text);
    const Hotkey new_undo = parse_hotkey(new_undo_text);
    const unsigned new_pause_kc =
        keycode_of(x11, XStringToKeysym(new_pause.keysym.c_str()));
    const unsigned new_undo_kc =
        keycode_of(x11, XStringToKeysym(new_undo.keysym.c_str()));
    if (new_pause_kc == 0 || new_undo_kc == 0 ||
        (new_pause_kc == new_undo_kc && new_pause.shift == new_undo.shift)) {
      QMessageBox::warning(nullptr, QStringLiteral("Неправильні клавіші"),
                           QStringLiteral("Вкажи дві різні доступні клавіші."));
      listener.drain();
      listener.take();
      focus.drain();
      return;
    }

    Config updated = config;
    updated.min_length = minimum_length->value();
    updated.pause_key = new_pause_text;
    updated.undo_key = new_undo_text;
    updated.exclude.clear();
    for (const auto &line : excluded_windows->toPlainText().split('\n')) {
      const auto name = line.trimmed();
      if (!name.isEmpty()) {
        updated.exclude.push_back(name.toStdString());
      }
    }
    const std::string save_path = config.config_path_explicit
                                      ? config.config_path
                                      : config.user_config_path;
    updated.config_path = save_path;
    if (!save_config_file(updated, save_path)) {
      QMessageBox::critical(nullptr, QStringLiteral("Не вдалося зберегти"),
                            QStringLiteral("Перевір права доступу до конфігу."));
      listener.drain();
      listener.take();
      focus.drain();
      return;
    }

    config = std::move(updated);
    pause_key = new_pause;
    undo_key = new_undo;
    pause_kc = new_pause_kc;
    undo_kc = new_undo_kc;
    log::info("налаштування збережено й застосовано");
  });
  QObject::connect(about_action, &QAction::triggered, [&] {
    const QString text =
        QStringLiteral("<h3>layout-fix %1</h3>"
                       "<p>Автоматичне виправлення розкладки для X11 і Cinnamon.</p>"
                       "<p>Мови: English (US), українська та російська.</p>"
                       "<p>Ліцензія коду: GPL-3.0-or-later.<br>"
                       "Fallback-словник uk_UA: MPL-2.0.</p>")
            .arg(QApplication::applicationVersion());
    QMessageBox::about(nullptr, QStringLiteral("Про програму"), text);
    update_tray();
  });
  QObject::connect(quit_action, &QAction::triggered,
                   [] { g_stop = 1; });
#endif

  log::info("слухаю клавіатуру. Пауза: " + config.pause_key +
            ", скасувати: " + config.undo_key + ". Таймаут перемикання: " +
            std::to_string(kLayoutSwitchTimeoutMs) + " мс");

  while (g_stop == 0) {
#ifdef LAYOUT_FIX_HAVE_QT_TRAY
    qt_app.processEvents(QEventLoop::AllEvents, 0);
    if (g_stop != 0) {
      break;
    }
    update_tray();
#endif
    if (injecting && std::chrono::steady_clock::now() > inject_deadline) {
      log::error("не дочекався відлуння власних клавіш за " +
                 std::to_string(kEchoTimeoutMs) + " мс, продовжую слухати");
      injecting = false;
      pending_echoes.clear();
    }

    pollfd pfds[2] = {
        {x11.data_fd(), POLLIN, 0},
        {x11.ctrl_fd(), POLLIN, 0},
    };
    poll(pfds, 2, 100);
    if ((pfds[0].revents & POLLIN) != 0) {
      listener.drain();
    }
    if ((pfds[1].revents & POLLIN) != 0) {
      focus.drain();
    }
    if (cinnamon.consume_sources_changed()) {
      clear_input();
      undo.valid = false;
      maps_ready = false;
      refresh_groups_pending = cinnamon.refresh();
    #ifdef LAYOUT_FIX_HAVE_QT_TRAY
      rebuild_language_menu();
    #endif
      map_retry_at = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(250);
      log::error("Cinnamon змінив джерела; виправлення призупинено до оновлення XKB");
    }
    if (refresh_groups_pending &&
        std::chrono::steady_clock::now() >= map_retry_at) {
      if (groups.init(false) && source_map_matches()) {
        maps_ready = true;
        refresh_groups_pending = false;
        log::info("мапи XKB і Cinnamon оновлено");
      } else {
        map_retry_at = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(250);
      }
    }
    if (focus.window_id() != seen_window) {
      seen_window = focus.window_id();
      clear_input();
      undo.valid = false;
    }

    const auto batch = listener.take();
    for (const RawEvent &event : batch) {
      if (injecting) {
        const bool button = event.type == RawEvent::Type::Button;
        const auto consumed =
            consume_echo(pending_echoes, button,
                         event.type == RawEvent::Type::Press, event.keycode);
        if (button) {
          clear_input();
          undo.valid = false;
        }
        if (consumed == EchoConsume::Done) {
          injecting = false;
          pending_echoes.clear();
        }
        continue;
      }
      if (event.type == RawEvent::Type::Release ||
          event.type == RawEvent::Type::Button) {
        if (event.type == RawEvent::Type::Button) {
          clear_input();
          undo.valid = false;
        }
        continue;
      }

      const bool shift = (event.state & ShiftMask) != 0;
      const bool caps = (event.state & LockMask) != 0;
      const bool pause_hit = event.keycode != 0 && (event.keycode == pause_kc ||
                                                    event.keycode == break_kc ||
                                                    event.keycode == undo_kc);
      if (pause_hit && !is_command_modifier(event.state)) {
        const bool want_undo =
            undo_key.shift == shift && event.keycode == undo_kc;
        const bool want_pause =
            pause_key.shift == shift &&
            (event.keycode == pause_kc || event.keycode == break_kc);
        if (want_undo && undo.valid) {
          auto original = std::move(undo.original);
          auto shown = std::move(undo.shown);
          const int index = undo.source_index;
          const Lang source_lang = undo.source_lang;
          const Lang shown_lang = undo.shown_lang;
          undo.valid = false;
          clear_input();
          std::vector<EchoKey> echoes;
          const int deleted = static_cast<int>(shown.size());
          retype.replace(deleted, {}, echoes);
          const bool confirmed =
              cinnamon.activate(index) &&
              groups.wait_until(source_lang, kLayoutSwitchTimeoutMs);
          const Lang now = groups.language_of(groups.current_group());
          const auto restore =
              choose_undo_restore(confirmed, now, source_lang, shown_lang);
          const auto timeout = std::to_string(kLayoutSwitchTimeoutMs);
          if (restore == UndoRestore::Original) {
            retype.replace(0, original, echoes);
            log::info("скасовано останнє виправлення");
          } else if (restore == UndoRestore::Shown) {
            retype.replace(0, shown, echoes);
            log::error("не вдалося повернути розкладку за " + timeout +
                       " мс, виправлене слово лишилось");
          } else {
            log::error("не вдалося повернути розкладку за " + timeout +
                       " мс, слово не набрано повторно");
          }
          x11.flush_ctrl();
          begin_inject(std::move(echoes));
        } else if (want_pause) {
          paused = !paused;
          clear_input();
          undo.valid = false;
#ifdef LAYOUT_FIX_HAVE_QT_TRAY
          update_tray();
#endif
          log::info(paused ? "пауза" : "знову слухаю");
        }
        continue;
      }

      if (!maps_ready) {
        clear_input();
        undo.valid = false;
        continue;
      }

      if (paused || focus.excluded(config.exclude) ||
          is_command_modifier(event.state) ||
          key_in(event.keycode, command_keys)) {
        clear_input();
        undo.valid = false;
        continue;
      }
      if (event.keycode == retype.backspace_keycode()) {
        buffer.pop();
        pending_short.clear();
        undo.valid = false;
        continue;
      }
      if (key_in(event.keycode, nav_keys)) {
        clear_input();
        undo.valid = false;
        continue;
      }

      const int group = groups.current_group();
      const Lang lang = groups.language_of(group);
      if (lang != Lang::En && lang != Lang::Uk &&
          (lang != Lang::Ru || !russian_dict_ready ||
           cinnamon.index_of("ru") < 0)) {
        clear_input();
        continue;
      }
      if (buffer.group() >= 0 && buffer.group() != group) {
        clear_input();
      }
      const auto current = groups.translate(event.keycode, group, shift, caps);
      std::array<std::string, 3> forms;
      for (const Lang candidate : kSupportedLanguages) {
        const int candidate_group = groups.group_for(candidate);
        if (candidate_group >= 0) {
          forms[lang_index(candidate)] =
              groups.translate(event.keycode, candidate_group, shift, caps).text;
        }
      }
      if (config.debug_keys && !current.text.empty()) {
        log::keys(current.text + " | " + forms[0] + " | " + forms[1] +
                  " | " + forms[2]);
      }
      if (current.kind == Translated::Kind::Ignore) {
        continue;
      }
      if (current.kind == Translated::Kind::Word) {
        undo.valid = false;
        if (buffer.group() < 0) {
          buffer.set_group(group);
        }
        buffer.push_word(event.keycode, shift, lang, std::move(forms));
        continue;
      }

      buffer.push_boundary(event.keycode, shift, current.text);
      const bool whitespace_boundary = current.text == " " || current.text == "\t" ||
                                       current.text == "\r" || current.text == "\n";
      std::array<std::vector<std::string>, 3> words_by_language;
      for (const auto& pending : pending_short) {
        for (std::size_t i = 0; i < pending.forms.size(); ++i) {
          words_by_language[i].push_back(pending.forms[i]);
        }
      }
      for (const Lang candidate : kSupportedLanguages) {
        words_by_language[lang_index(candidate)].push_back(
            buffer.text(candidate));
      }
      auto active_dictionaries = dictionaries;
      if (cinnamon.index_of("ru") < 0) {
        active_dictionaries[lang_index(Lang::Ru)] = nullptr;
      }
      const auto target = unique_language_candidate(
          words_by_language, lang, active_dictionaries, config.min_length);
      if (target.has_value() && buffer.has_word()) {
        std::vector<Stroke> original;
        for (const auto& pending : pending_short) {
          original.insert(original.end(), pending.strokes.begin(),
                          pending.strokes.end());
        }
        const auto& current_strokes = buffer.strokes();
        original.insert(original.end(), current_strokes.begin(),
                        current_strokes.end());
        const int target_index =
            cinnamon.index_of(cinnamon_source_id(*target));
        const int target_group = groups.group_for(*target);
        std::vector<Stroke> output;
        output.reserve(original.size());
        for (const auto &stroke : original) {
          if (stroke.word) {
            output.push_back(stroke);
            continue;
          }
          Stroke boundary = stroke;
          if (const auto hit = groups.find_key(target_group, stroke.current)) {
            boundary.keycode = hit->keycode;
            boundary.shift = hit->shift;
          }
          output.push_back(std::move(boundary));
        }
        const int from_index = cinnamon.index_of(cinnamon_source_id(lang));
        std::vector<EchoKey> echoes;
        retype.replace(static_cast<int>(original.size()), {}, echoes);
        if (cinnamon.activate(target_index) &&
            groups.wait_until(*target, kLayoutSwitchTimeoutMs)) {
          retype.replace(0, output, echoes);
          undo = Undo{
              true,  std::move(original), std::move(output), from_index, lang,
              *target};
          log::info("виправлено слово або контекст; мова " +
                    std::string(lang_name(*target)));
        } else {
          retype.replace(0, original, echoes);
          log::error("не вдалося перемкнути розкладку за " +
                     std::to_string(kLayoutSwitchTimeoutMs) +
                     " мс, слово повернуто");
        }
        x11.flush_ctrl();
        begin_inject(std::move(echoes));
        clear_input();
        continue;
      } else {
        undo.valid = false;
      }
      const auto& current_word = buffer.text(lang);
      if (whitespace_boundary && buffer.has_word() &&
          utf8::char_count(current_word) <
              static_cast<std::size_t>(config.min_length)) {
        std::array<std::string, 3> pending_forms;
        for (const Lang candidate : kSupportedLanguages) {
          pending_forms[lang_index(candidate)] = buffer.text(candidate);
        }
        pending_short.push_back(
            PendingWord{std::move(pending_forms), buffer.strokes()});
        if (pending_short.size() > 2) {
          pending_short.erase(pending_short.begin());
        }
      } else {
        pending_short.clear();
      }
      buffer.clear();
    }
  }

  log::info("зупинено");
  std::signal(SIGINT, SIG_DFL);
  std::signal(SIGTERM, SIG_DFL);
  return 0;
}

} // namespace layoutfix
