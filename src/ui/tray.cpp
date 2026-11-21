#include "ui/tray.hpp"

#include "log.hpp"

#ifdef LAYOUT_FIX_HAVE_QT_TRAY
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QVBoxLayout>
#endif

#include <utility>

namespace layoutfix {

#ifdef LAYOUT_FIX_HAVE_QT_TRAY
namespace {

QIcon layout_icon(Lang language, TrayMode mode) {
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  const QColor active_color("#378b57");
  const QColor desynced_color("#c97a16");
  const int gray = qGray(active_color.rgb());
  const QColor bg =
      (mode == TrayMode::Paused || mode == TrayMode::PausedDesynced)
          ? QColor(gray, gray, gray)
          : (mode == TrayMode::Desynced ? desynced_color : active_color);
  painter.setBrush(bg);
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

QString language_label(Lang language) {
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
}

QKeySequenceEdit *make_hotkey_edit(const std::string &value) {
  auto *edit = new QKeySequenceEdit;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  edit->setMaximumSequenceLength(1);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
  edit->setClearButtonEnabled(true);
#endif
  edit->setKeySequence(QKeySequence::fromString(
      QString::fromStdString(hotkey_to_qt_sequence(value)),
      QKeySequence::PortableText));
  QObject::connect(edit, &QKeySequenceEdit::keySequenceChanged, edit,
                   [edit](const QKeySequence &seq) {
                     if (seq.count() > 1) {
                       edit->setKeySequence(QKeySequence(seq[0]));
                     }
                   });
  return edit;
}

std::string read_hotkey_edit(const QKeySequenceEdit *edit) {
  return hotkey_from_qt_sequence(
      edit->keySequence().toString(QKeySequence::PortableText).toStdString());
}

}  // namespace

struct Tray::Impl {
  int qt_argc = 1;
  char qt_name[11] = "layout-fix";
  char *qt_argv[2] = {qt_name, nullptr};
  QApplication qt_app;
  QSystemTrayIcon tray;
  QMenu tray_menu;
  QAction *status_action = nullptr;
  QAction *pause_action = nullptr;
  QMenu *language_menu = nullptr;
  std::vector<std::pair<Lang, QAction *>> language_actions;
  QAction *settings_action = nullptr;
  QAction *about_action = nullptr;
  QAction *quit_action = nullptr;
  TrayCallbacks callbacks;
  bool has_state = false;
  Lang last_lang = Lang::Other;
  TrayMode last_mode = TrayMode::Active;

  explicit Impl(TrayCallbacks cb)
      : qt_app(qt_argc, qt_argv), callbacks(std::move(cb)) {
    qt_app.setQuitOnLastWindowClosed(false);
    QApplication::setApplicationName(QStringLiteral("layout-fix"));
    QApplication::setApplicationVersion(QStringLiteral(LAYOUT_FIX_VERSION));

    status_action = tray_menu.addAction("Стан: запускаю…");
    status_action->setEnabled(false);
    tray_menu.addSeparator();
    pause_action = tray_menu.addAction("Пауза");
    language_menu = tray_menu.addMenu("Перемкнути розкладку");
    settings_action = tray_menu.addAction("Налаштування…");
    about_action = tray_menu.addAction("Про layout-fix");
    tray_menu.addSeparator();
    quit_action = tray_menu.addAction("Вийти");
    tray.setContextMenu(&tray_menu);

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
      log::error(
          "системний трей недоступний; керування лишається на клавіатурі");
    }

    QObject::connect(pause_action, &QAction::triggered, [this] {
      if (callbacks.on_toggle_pause) {
        callbacks.on_toggle_pause();
      }
    });
    QObject::connect(settings_action, &QAction::triggered,
                     [this] { open_settings(); });
    QObject::connect(about_action, &QAction::triggered,
                     [this] { open_about(); });
    QObject::connect(quit_action, &QAction::triggered, [this] {
      if (callbacks.on_quit) {
        callbacks.on_quit();
      }
    });
  }

  void open_settings() {
    if (!callbacks.get_config || !callbacks.validate_hotkeys ||
        !callbacks.apply_config) {
      return;
    }
    if (callbacks.on_modal_pause) {
      callbacks.on_modal_pause(true);
    }

    const Config current_cfg = callbacks.get_config();
    const std::string last_window =
        callbacks.get_last_window ? callbacks.get_last_window() : std::string{};

    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("Налаштування layout-fix"));
    dialog.setMinimumWidth(480);
    auto *form = new QFormLayout;
    auto *minimum_length = new QSpinBox;
    minimum_length->setRange(1, 24);
    minimum_length->setValue(current_cfg.min_length);
    form->addRow(QStringLiteral("Мінімальна довжина слова"), minimum_length);

    auto *pause_hotkey = make_hotkey_edit(current_cfg.pause_key);
    form->addRow(QStringLiteral("Пауза / продовжити"), pause_hotkey);

    auto *undo_hotkey = make_hotkey_edit(current_cfg.undo_key);
    form->addRow(QStringLiteral("Скасувати останнє виправлення"), undo_hotkey);

    auto *convert_hotkey = make_hotkey_edit(current_cfg.convert_key);
    form->addRow(QStringLiteral("Виправити виділений текст"), convert_hotkey);

    auto *excluded_windows = new QPlainTextEdit;
    excluded_windows->setPlaceholderText(
        QStringLiteral("Одна частина WM_CLASS на рядок"));
    QStringList excluded_lines;
    for (const auto &name : current_cfg.exclude) {
      excluded_lines.push_back(QString::fromStdString(name));
    }
    excluded_windows->setPlainText(excluded_lines.join('\n'));
    excluded_windows->setMinimumHeight(110);
    auto *exclude_box = new QVBoxLayout;
    exclude_box->setContentsMargins(0, 0, 0, 0);
    exclude_box->addWidget(excluded_windows);
    auto *last_window_row = new QHBoxLayout;
    last_window_row->setContentsMargins(0, 0, 0, 0);
    const QString last_wm = QString::fromStdString(last_window);
    auto *last_window_label = new QLabel(
        last_wm.isEmpty()
            ? QStringLiteral("Останнє вікно: невідоме")
            : QStringLiteral("Останнє вікно: %1").arg(last_wm));
    auto *add_last_window = new QPushButton(QStringLiteral("Додати"));
    add_last_window->setEnabled(!last_wm.isEmpty());
    QObject::connect(add_last_window, &QPushButton::clicked, [&] {
      if (last_wm.isEmpty()) {
        return;
      }
      QStringList lines = excluded_windows->toPlainText().split('\n');
      for (const auto &line : lines) {
        if (line.trimmed().compare(last_wm, Qt::CaseInsensitive) == 0) {
          return;
        }
      }
      QString text = excluded_windows->toPlainText();
      if (!text.isEmpty() && !text.endsWith('\n')) {
        text += '\n';
      }
      text += last_wm;
      excluded_windows->setPlainText(text);
    });
    last_window_row->addWidget(last_window_label, 1);
    last_window_row->addWidget(add_last_window);
    exclude_box->addLayout(last_window_row);
    form->addRow(QStringLiteral("Не виправляти у вікнах"), exclude_box);

    auto *ignored_words_edit = new QPlainTextEdit;
    ignored_words_edit->setPlaceholderText(
        QStringLiteral("Одне слово на рядок (без урахування регістру)"));
    QStringList ignored_lines;
    for (const auto &word : current_cfg.ignore_words) {
      ignored_lines.push_back(QString::fromStdString(word));
    }
    ignored_words_edit->setPlainText(ignored_lines.join('\n'));
    ignored_words_edit->setMinimumHeight(80);
    form->addRow(QStringLiteral("Не виправляти слова"), ignored_words_edit);

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

    const int result = dialog.exec();
    if (callbacks.on_modal_pause) {
      callbacks.on_modal_pause(false);
    }
    if (result != QDialog::Accepted) {
      return;
    }

    Config updated = current_cfg;
    updated.min_length = minimum_length->value();
    updated.pause_key = read_hotkey_edit(pause_hotkey);
    updated.undo_key = read_hotkey_edit(undo_hotkey);
    updated.convert_key = read_hotkey_edit(convert_hotkey);
    if (!callbacks.validate_hotkeys(updated)) {
      QMessageBox::warning(
          nullptr, QStringLiteral("Неправильні клавіші"),
          QStringLiteral(
              "Вкажи три різні доступні клавіші (для літерних комбінацій "
              "перемкнись на латиницю)."));
      if (callbacks.on_after_modal) {
        callbacks.on_after_modal();
      }
      return;
    }

    updated.exclude.clear();
    for (const auto &line : excluded_windows->toPlainText().split('\n')) {
      const auto name = line.trimmed();
      if (!name.isEmpty()) {
        updated.exclude.push_back(name.toStdString());
      }
    }
    updated.ignore_words.clear();
    for (const auto &line : ignored_words_edit->toPlainText().split('\n')) {
      for (const auto &part : line.split(',')) {
        const auto word = part.trimmed();
        if (!word.isEmpty()) {
          updated.ignore_words.push_back(word.toStdString());
        }
      }
    }
    const std::string save_path = current_cfg.config_path_explicit
                                      ? current_cfg.config_path
                                      : current_cfg.user_config_path;
    updated.config_path = save_path;
    if (!save_config_file(updated, save_path)) {
      QMessageBox::critical(nullptr, QStringLiteral("Не вдалося зберегти"),
                            QStringLiteral("Перевір права доступу до конфігу."));
      if (callbacks.on_after_modal) {
        callbacks.on_after_modal();
      }
      return;
    }

    callbacks.apply_config(std::move(updated));
    log::info("налаштування збережено й застосовано");
  }

  void open_about() {
    const QString text =
        QStringLiteral("<h3>layout-fix %1</h3>"
                       "<p>Автоматичне виправлення розкладки для X11 і Cinnamon.</p>"
                       "<p>Мови: English (US), українська та російська.</p>"
                       "<p>Ліцензія коду: GPL-3.0-or-later.</p>"
                       "<br>"
                       "<p>© 2026 Ihor Dolinskyi</p>"
                       "<p>Fallback-словник uk_UA: MPL-2.0.</p>")
            .arg(QApplication::applicationVersion());
    QMessageBox::about(nullptr, QStringLiteral("Про програму"), text);
    if (callbacks.on_after_modal) {
      callbacks.on_after_modal();
    }
  }
};

Tray::Tray(TrayCallbacks callbacks)
    : impl_(std::make_unique<Impl>(std::move(callbacks))) {}

Tray::~Tray() = default;

void Tray::set_languages(const std::vector<TrayLanguageOption> &options) {
  impl_->language_menu->clear();
  impl_->language_actions.clear();
  for (const auto &option : options) {
    QAction *action =
        impl_->language_menu->addAction(language_label(option.lang));
    action->setCheckable(true);
    impl_->language_actions.emplace_back(option.lang, action);
    QObject::connect(action, &QAction::triggered,
                     [this, index = option.source_index] {
                       if (impl_->callbacks.on_activate_source) {
                         impl_->callbacks.on_activate_source(index);
                       }
                     });
  }
}

void Tray::update(Lang active_language, TrayMode mode, bool paused,
                  bool force) {
  if (!force && impl_->has_state && active_language == impl_->last_lang &&
      mode == impl_->last_mode) {
    if (!impl_->tray.isVisible() && QSystemTrayIcon::isSystemTrayAvailable()) {
      impl_->tray.show();
    }
    return;
  }
  impl_->has_state = true;
  impl_->last_lang = active_language;
  impl_->last_mode = mode;

  QString state_text;
  QString tooltip_state;
  switch (mode) {
  case TrayMode::Active:
    state_text = QStringLiteral("працює");
    tooltip_state = QStringLiteral("працює");
    break;
  case TrayMode::Paused:
    state_text = QStringLiteral("пауза");
    tooltip_state = QStringLiteral("пауза");
    break;
  case TrayMode::Desynced:
    state_text = QStringLiteral("XKB/Cinnamon не узгоджені");
    tooltip_state = QStringLiteral(
        "XKB і Cinnamon не узгоджені (виправлення призупинено)");
    break;
  case TrayMode::PausedDesynced:
    state_text = QStringLiteral("пауза (XKB/Cinnamon не узгоджені)");
    tooltip_state = QStringLiteral("пауза · XKB і Cinnamon не узгоджені");
    break;
  }
  impl_->status_action->setText(
      QStringLiteral("Стан: %1 · мова: %2")
          .arg(state_text, language_label(active_language)));
  impl_->pause_action->setText(paused ? "Відновити" : "Пауза");
  for (const auto &[language, action] : impl_->language_actions) {
    action->setChecked(language == active_language);
  }
  impl_->tray.setIcon(layout_icon(active_language, mode));
  impl_->tray.setToolTip(QStringLiteral("layout-fix · %1 · %2")
                             .arg(tooltip_state,
                                  language_label(active_language)));
  if (!impl_->tray.isVisible() && QSystemTrayIcon::isSystemTrayAvailable()) {
    impl_->tray.show();
  }
}

void Tray::process_events() {
  impl_->qt_app.processEvents(QEventLoop::AllEvents, 0);
}

#else

struct Tray::Impl {};

Tray::Tray(TrayCallbacks) : impl_(std::make_unique<Impl>()) {}
Tray::~Tray() = default;
void Tray::set_languages(const std::vector<TrayLanguageOption> &) {}
void Tray::update(Lang, TrayMode, bool, bool) {}
void Tray::process_events() {}

#endif

}  // namespace layoutfix

