#include "app.hpp"

#include "detect/detector.hpp"
#include "detect/selection.hpp"
#include "events.hpp"
#include "input/echoes.hpp"
#include "input/undo_restore.hpp"
#include "input/word_buffer.hpp"
#include "layout/cinnamon.hpp"
#include "layout/groups.hpp"
#include "log.hpp"
#include "timing.hpp"
#include "tray_state.hpp"
#include "ui/tray.hpp"
#include "utf8.hpp"
#include "x11/clipboard.hpp"
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
#include <memory>
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

struct LastWord {
  bool valid = false;
  std::array<std::string, 3> forms;
  std::vector<Stroke> strokes;
  Lang lang = Lang::Other;
  std::array<bool, 3> visited{false, false, false};
};

struct ClipboardRestore {
  bool pending = false;
  bool had_saved = false;
  bool serve_seen = false;
  std::string saved;
  std::chrono::steady_clock::time_point deadline;
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

unsigned keycode_of(XConnection &x11, KeySym sym) {
  return sym == NoSymbol ? 0 : XKeysymToKeycode(x11.ctrl(), sym);
}

unsigned hotkey_keycode(XConnection &x11, const Hotkey &hotkey) {
  if (hotkey.keysym.empty()) {
    return 0;
  }
  return keycode_of(x11, XStringToKeysym(hotkey.keysym.c_str()));
}

bool same_hotkey(const Hotkey &a, unsigned a_kc, const Hotkey &b,
                 unsigned b_kc) {
  return a_kc == b_kc && a.shift == b.shift && a.ctrl == b.ctrl &&
         a.alt == b.alt && a.super == b.super;
}

bool hotkey_matches(const Hotkey &hotkey, unsigned hk_kc, unsigned break_kc,
                    unsigned event_kc, unsigned state) {
  if (hk_kc == 0 || event_kc == 0) {
    return false;
  }
  const bool key_match =
      event_kc == hk_kc ||
      (hotkey.keysym == "Pause" && break_kc != 0 && event_kc == break_kc);
  if (!key_match) {
    return false;
  }
  const bool shift = (state & ShiftMask) != 0;
  const bool ctrl = (state & ControlMask) != 0;
  const bool alt = (state & Mod1Mask) != 0;
  const bool super = (state & Mod4Mask) != 0;
  return hotkey.shift == shift && hotkey.ctrl == ctrl && hotkey.alt == alt &&
         hotkey.super == super;
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
  const auto ru_path =
      resolve(config.dict_ru, {"/usr/share/hunspell/ru_RU.dic"});
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
  Clipboard clipboard(x11);
  if (!retype.init() || !listener.start() || !clipboard.init()) {
    return 1;
  }

  auto drain_ctrl = [&] {
    auto *dpy = x11.ctrl();
    while (XPending(dpy) > 0) {
      XEvent ev;
      XNextEvent(dpy, &ev);
      focus.handle_event(ev);
      clipboard.handle_event(ev);
    }
  };

  auto pause_key = parse_hotkey(config.pause_key);
  auto undo_key = parse_hotkey(config.undo_key);
  auto convert_key = parse_hotkey(config.convert_key);
  auto ignore_set = build_ignore_set(config.ignore_words);
  unsigned pause_kc = hotkey_keycode(x11, pause_key);
  unsigned undo_kc = hotkey_keycode(x11, undo_key);
  unsigned convert_kc = hotkey_keycode(x11, convert_key);
  const unsigned break_kc = keycode_of(x11, XK_Break);
  std::vector<unsigned> command_keys;
  for (KeySym sym : {XK_Control_L, XK_Control_R, XK_Alt_L, XK_Alt_R, XK_Super_L,
                     XK_Super_R, XK_Meta_L, XK_Meta_R, XK_ISO_Level3_Shift}) {
    if (const unsigned kc = keycode_of(x11, sym); kc != 0) {
      command_keys.push_back(kc);
    }
  }
  std::vector<unsigned> hotkey_modifier_keys;
  auto rebuild_hotkey_modifier_keys = [&] {
    hotkey_modifier_keys.clear();
    auto add_sym = [&](KeySym sym) {
      if (const unsigned kc = keycode_of(x11, sym); kc != 0) {
        hotkey_modifier_keys.push_back(kc);
      }
    };
    const bool use_ctrl = pause_key.ctrl || undo_key.ctrl || convert_key.ctrl;
    const bool use_alt = pause_key.alt || undo_key.alt || convert_key.alt;
    const bool use_super =
        pause_key.super || undo_key.super || convert_key.super;
    if (use_ctrl) {
      add_sym(XK_Control_L);
      add_sym(XK_Control_R);
    }
    if (use_alt) {
      add_sym(XK_Alt_L);
      add_sym(XK_Alt_R);
      add_sym(XK_Meta_L);
      add_sym(XK_Meta_R);
    }
    if (use_super) {
      add_sym(XK_Super_L);
      add_sym(XK_Super_R);
    }
  };
  rebuild_hotkey_modifier_keys();
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
  LastWord last_word;
  ClipboardRestore clip_restore;
  std::vector<PendingWord> pending_short;
  auto clear_input = [&] {
    buffer.clear();
    pending_short.clear();
    last_word.valid = false;
  };
  bool paused = false;
  bool injecting = false;
  std::vector<EchoKey> pending_echoes;
  auto inject_deadline = std::chrono::steady_clock::now();
  unsigned long seen_window = 0;
  std::string last_app_wm_class;
  auto track_last_window = [&] {
    const std::string &candidate =
        !focus.wm_class().empty() ? focus.wm_class() : focus.wm_name();
    if (candidate.empty()) {
      return;
    }
    const std::string lowered = utf8::casefold(candidate);
    if (lowered == "layout-fix" || lowered == "cinnamon" ||
        lowered == "nemo-desktop") {
      return;
    }
    last_app_wm_class = candidate;
  };
  track_last_window();

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

  bool saved_modal_paused = false;
  std::unique_ptr<Tray> tray;
  auto update_tray = [&](bool force = false) {
    if (tray) {
      tray->update(groups.language_of(groups.current_group()),
                   choose_tray_mode(paused, maps_ready), paused, force);
    }
  };
  auto rebuild_language_menu = [&] {
    if (!tray) {
      return;
    }
    std::vector<TrayLanguageOption> options;
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
      options.push_back(TrayLanguageOption{language, source.index});
    }
    tray->set_languages(options);
  };

  TrayCallbacks tray_callbacks;
  tray_callbacks.on_toggle_pause = [&] {
    paused = !paused;
    clear_input();
    undo.valid = false;
    log::info(paused ? "пауза" : "знову слухаю");
    update_tray();
  };
  tray_callbacks.on_activate_source = [&](int index) {
    clear_input();
    undo.valid = false;
    cinnamon.activate(index);
  };
  tray_callbacks.on_quit = [] { g_stop = 1; };
  tray_callbacks.on_modal_pause = [&](bool entering) {
    if (entering) {
      drain_ctrl();
      track_last_window();
      saved_modal_paused = paused;
      paused = true;
      clear_input();
      undo.valid = false;
      update_tray();
    } else {
      listener.drain();
      listener.take();
      drain_ctrl();
      clear_input();
      paused = saved_modal_paused;
      update_tray();
    }
  };
  tray_callbacks.on_after_modal = [&] {
    listener.drain();
    listener.take();
    drain_ctrl();
    update_tray();
  };
  tray_callbacks.get_config = [&]() -> const Config & { return config; };
  tray_callbacks.get_last_window = [&] { return last_app_wm_class; };
  tray_callbacks.validate_hotkeys = [&](const Config &candidate) {
    const Hotkey p = parse_hotkey(candidate.pause_key);
    const Hotkey u = parse_hotkey(candidate.undo_key);
    const Hotkey c = parse_hotkey(candidate.convert_key);
    const unsigned p_kc = hotkey_keycode(x11, p);
    const unsigned u_kc = hotkey_keycode(x11, u);
    const unsigned c_kc = hotkey_keycode(x11, c);
    return p_kc != 0 && u_kc != 0 && c_kc != 0 &&
           !same_hotkey(p, p_kc, u, u_kc) && !same_hotkey(p, p_kc, c, c_kc) &&
           !same_hotkey(u, u_kc, c, c_kc);
  };
  tray_callbacks.apply_config = [&](Config updated) {
    config = std::move(updated);
    pause_key = parse_hotkey(config.pause_key);
    undo_key = parse_hotkey(config.undo_key);
    convert_key = parse_hotkey(config.convert_key);
    pause_kc = hotkey_keycode(x11, pause_key);
    undo_kc = hotkey_keycode(x11, undo_key);
    convert_kc = hotkey_keycode(x11, convert_key);
    ignore_set = build_ignore_set(config.ignore_words);
    rebuild_hotkey_modifier_keys();
  };
  tray = std::make_unique<Tray>(std::move(tray_callbacks));
  rebuild_language_menu();
  update_tray(true);

  log::info("слухаю клавіатуру. Пауза: " + config.pause_key +
            ", скасувати: " + config.undo_key +
            ", виділений текст: " + config.convert_key +
            ". Таймаут перемикання: " +
            std::to_string(kLayoutSwitchTimeoutMs) + " мс");

  while (g_stop == 0) {
    tray->process_events();
    if (g_stop != 0) {
      break;
    }
    update_tray();
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
    poll(pfds, 2, clip_restore.pending ? 20 : 100);
    if ((pfds[0].revents & POLLIN) != 0) {
      listener.drain();
    }
    if ((pfds[1].revents & POLLIN) != 0 || XPending(x11.ctrl()) > 0) {
      drain_ctrl();
    }
    if (clip_restore.pending) {
      if (!clipboard.is_owning()) {
        clip_restore.pending = false;
      } else {
        const auto now = std::chrono::steady_clock::now();
        if (!clip_restore.serve_seen && clipboard.content_served()) {
          clip_restore.serve_seen = true;
          const auto soon =
              now + std::chrono::milliseconds(kClipboardRestoreAfterServeMs);
          if (soon < clip_restore.deadline) {
            clip_restore.deadline = soon;
          }
        }
        if (now >= clip_restore.deadline) {
          if (clip_restore.had_saved) {
            clipboard.set(std::move(clip_restore.saved));
          } else {
            clipboard.clear();
          }
          clip_restore.pending = false;
        }
      }
    }
    if (cinnamon.consume_sources_changed()) {
      clear_input();
      undo.valid = false;
      maps_ready = false;
      refresh_groups_pending = cinnamon.refresh();
      rebuild_language_menu();
      update_tray(true);
      map_retry_at = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(250);
      log::error("Cinnamon змінив джерела; виправлення призупинено до оновлення XKB");
    }
    if (refresh_groups_pending &&
        std::chrono::steady_clock::now() >= map_retry_at) {
      if (groups.init(false) && source_map_matches()) {
        maps_ready = true;
        refresh_groups_pending = false;
        rebuild_language_menu();
        update_tray(true);
        log::info("мапи XKB і Cinnamon оновлено");
      } else {
        map_retry_at = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(250);
      }
    }
    if (focus.window_id() != seen_window) {
      seen_window = focus.window_id();
      track_last_window();
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
      const bool want_pause =
          hotkey_matches(pause_key, pause_kc, break_kc, event.keycode,
                         event.state);
      const bool want_undo =
          hotkey_matches(undo_key, undo_kc, break_kc, event.keycode,
                         event.state);
      const bool want_convert =
          hotkey_matches(convert_key, convert_kc, break_kc, event.keycode,
                         event.state);
      if (want_pause || want_undo || want_convert) {
        if (want_pause) {
          paused = !paused;
          clear_input();
          undo.valid = false;
          update_tray();
          log::info(paused ? "пауза" : "знову слухаю");
        } else if (want_undo && !paused && undo.valid) {
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
        } else if (want_convert && !paused && maps_ready &&
                   !focus.excluded(config.exclude)) {
          auto active_dictionaries = dictionaries;
          std::array<bool, 3> active_languages{
              groups.group_for(Lang::En) >= 0 && cinnamon.index_of("us") >= 0,
              groups.group_for(Lang::Uk) >= 0 && cinnamon.index_of("ua") >= 0,
              groups.group_for(Lang::Ru) >= 0 && russian_dict_ready &&
                  cinnamon.index_of("ru") >= 0};
          if (!active_languages[lang_index(Lang::Ru)]) {
            active_dictionaries[lang_index(Lang::Ru)] = nullptr;
          }

          if (buffer.has_word() || last_word.valid) {
            LastWord word_to_fix;
            if (buffer.has_word()) {
              const Lang live_lang = groups.language_of(buffer.group());
              word_to_fix.valid = true;
              for (const Lang candidate : kSupportedLanguages) {
                word_to_fix.forms[lang_index(candidate)] =
                    buffer.text(candidate);
              }
              word_to_fix.strokes = buffer.strokes();
              word_to_fix.lang = live_lang;
              if (live_lang != Lang::Other) {
                word_to_fix.visited[lang_index(live_lang)] = true;
              }
            } else {
              word_to_fix = last_word;
            }

            const auto target = choose_forced_language(
                word_to_fix.forms, word_to_fix.lang, active_dictionaries,
                active_languages, word_to_fix.visited);
            if (target.has_value()) {
              const int target_index =
                  cinnamon.index_of(cinnamon_source_id(*target));
              const int target_group = groups.group_for(*target);
              const int from_index =
                  cinnamon.index_of(cinnamon_source_id(word_to_fix.lang));
              std::vector<Stroke> original = word_to_fix.strokes;
              std::vector<Stroke> output;
              output.reserve(original.size());
              for (const auto &stroke : original) {
                if (stroke.word) {
                  output.push_back(stroke);
                  continue;
                }
                Stroke boundary = stroke;
                if (const auto hit =
                        groups.find_key(target_group, stroke.current)) {
                  boundary.keycode = hit->keycode;
                  boundary.shift = hit->shift;
                }
                output.push_back(std::move(boundary));
              }
              std::vector<EchoKey> echoes;
              retype.replace(static_cast<int>(original.size()), {}, echoes);
              if (cinnamon.activate(target_index) &&
                  groups.wait_until(*target, kLayoutSwitchTimeoutMs)) {
                retype.replace(0, output, echoes);
                undo = Undo{true,
                            original,
                            output,
                            from_index,
                            word_to_fix.lang,
                            *target};
                auto next_visited = word_to_fix.visited;
                if (next_visited[lang_index(*target)]) {
                  // Завершили повне коло — починаємо новий цикл із *target.
                  next_visited = {false, false, false};
                }
                next_visited[lang_index(*target)] = true;
                last_word = LastWord{true, word_to_fix.forms, std::move(output),
                                     *target, next_visited};
                buffer.clear();
                pending_short.clear();
                update_tray();
                log::info("примусово виправлено останнє слово; мова " +
                          std::string(lang_name(*target)));
              } else {
                retype.replace(0, original, echoes);
                clear_input();
                undo.valid = false;
                log::error("не вдалося перемкнути розкладку за " +
                           std::to_string(kLayoutSwitchTimeoutMs) +
                           " мс, слово повернуто");
              }
              x11.flush_ctrl();
              begin_inject(std::move(echoes));
            }
            continue;
          }

          clear_input();
          undo.valid = false;
          std::string saved_clip;
          bool had_saved = false;
          if (clip_restore.pending) {
            had_saved = clip_restore.had_saved;
            saved_clip = std::move(clip_restore.saved);
            clip_restore.pending = false;
          } else {
            had_saved = clipboard.read(kClipboardTimeoutMs, saved_clip);
          }

          auto restore_saved_now = [&] {
            if (had_saved) {
              clipboard.set(std::move(saved_clip));
            } else {
              clipboard.clear();
            }
          };

          std::string primary_text;
          const bool copy_line_editor =
              is_copy_line_editor(focus.wm_class(), focus.wm_name());
          if (copy_line_editor) {
            clipboard.read_primary(80, primary_text);
          }

          std::vector<EchoKey> echoes;
          const bool sentinel_ok = clipboard.claim_sentinel();
          retype.send_ctrl_c(echoes);
          const bool copied =
              sentinel_ok &&
              clipboard.wait_for_owner_change(kClipboardTimeoutMs);
          std::string selected;
          if (copied && clipboard.read(kClipboardTimeoutMs, selected) &&
              !selected.empty()) {
            std::string web_custom_data;
            if (copy_line_editor || selected.back() == '\n') {
              clipboard.read_custom_target("chromium/x-web-custom-data", 80,
                                           web_custom_data);
            }
            if (looks_like_empty_line_copy(selected, primary_text,
                                           copy_line_editor,
                                           web_custom_data)) {
              restore_saved_now();
              log::info("пропущено порожнє виділення в редакторі");
              x11.flush_ctrl();
              begin_inject(std::move(echoes));
              continue;
            }
            const Lang current_lang =
                groups.language_of(groups.current_group());
            const auto converted = convert_selection(
                selected, groups.layout_maps(), active_dictionaries,
                active_languages, current_lang);
            if (converted.changed) {
              const int target_index =
                  cinnamon.index_of(cinnamon_source_id(converted.target));
              if (converted.target != current_lang && target_index >= 0) {
                if (cinnamon.activate(target_index)) {
                  groups.wait_until(converted.target, kLayoutSwitchTimeoutMs);
                }
                update_tray();
              }
              clipboard.set(converted.text);
              retype.send_ctrl_v(echoes);
              clip_restore = ClipboardRestore{
                  true, had_saved, false, std::move(saved_clip),
                  std::chrono::steady_clock::now() +
                      std::chrono::milliseconds(kClipboardRestoreDelayMs)};
              log::info("виправлено виділений текст; мова " +
                        std::string(lang_name(converted.target)));
            } else {
              restore_saved_now();
            }
          } else {
            restore_saved_now();
          }
          x11.flush_ctrl();
          begin_inject(std::move(echoes));
        }
        continue;
      }

      if (!maps_ready) {
        clear_input();
        undo.valid = false;
        continue;
      }

      if (paused || focus.excluded(config.exclude)) {
        clear_input();
        undo.valid = false;
        continue;
      }
      if (key_in(event.keycode, hotkey_modifier_keys)) {
        // Не стираємо buffer/last_word/undo при голому натисканні модифікатора,
        // задіяного в активному хоткеї (напр. Alt для Alt+Pause). Реальні
        // комбінації (Alt+Tab, Ctrl+A) очистять стан через is_command_modifier.
        continue;
      }
      if (is_command_modifier(event.state) ||
          key_in(event.keycode, command_keys)) {
        clear_input();
        undo.valid = false;
        continue;
      }
      if (event.keycode == retype.backspace_keycode()) {
        buffer.pop();
        pending_short.clear();
        last_word.valid = false;
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
      bool word_in_any_active = current.kind == Translated::Kind::Word;
      for (const Lang candidate : kSupportedLanguages) {
        const int candidate_group = groups.group_for(candidate);
        if (candidate_group >= 0) {
          const auto translated =
              groups.translate(event.keycode, candidate_group, shift, caps);
          forms[lang_index(candidate)] = translated.text;
          const bool candidate_active =
              candidate != Lang::Ru ||
              (russian_dict_ready && cinnamon.index_of("ru") >= 0);
          if (candidate_active && translated.kind == Translated::Kind::Word) {
            word_in_any_active = true;
          }
        }
      }
      if (config.debug_keys && !current.text.empty()) {
        log::keys(current.text + " | " + forms[0] + " | " + forms[1] +
                  " | " + forms[2]);
      }
      if (current.kind == Translated::Kind::Ignore) {
        continue;
      }
      if (word_in_any_active) {
        undo.valid = false;
        last_word.valid = false;
        if (buffer.group() < 0) {
          buffer.set_group(group);
        }
        buffer.push_word(event.keycode, shift, lang, std::move(forms));
        continue;
      }

      buffer.push_boundary(event.keycode, shift, current.text);
      const bool whitespace_boundary = current.text == " " || current.text == "\t" ||
                                       current.text == "\r" || current.text == "\n";
      auto active_dictionaries = dictionaries;
      if (cinnamon.index_of("ru") < 0) {
        active_dictionaries[lang_index(Lang::Ru)] = nullptr;
      }
      std::optional<Lang> target;
      std::size_t matched_start = pending_short.size();
      if (buffer.has_word()) {
        for (std::size_t start = 0; start <= pending_short.size(); ++start) {
          std::array<std::vector<std::string>, 3> words_by_language;
          for (std::size_t p = start; p < pending_short.size(); ++p) {
            for (std::size_t i = 0; i < pending_short[p].forms.size(); ++i) {
              words_by_language[i].push_back(pending_short[p].forms[i]);
            }
          }
          for (const Lang candidate : kSupportedLanguages) {
            words_by_language[lang_index(candidate)].push_back(
                buffer.text(candidate));
          }
          const auto candidate_target = unique_language_candidate(
              words_by_language, lang, active_dictionaries, config.min_length,
              &ignore_set);
          if (candidate_target.has_value()) {
            target = candidate_target;
            matched_start = start;
            break;
          }
        }
      }
      if (target.has_value() && buffer.has_word()) {
        const bool multi_word = matched_start < pending_short.size();
        std::vector<Stroke> original;
        for (std::size_t p = matched_start; p < pending_short.size(); ++p) {
          original.insert(original.end(), pending_short[p].strokes.begin(),
                          pending_short[p].strokes.end());
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
        std::string paste_text;
        std::string saved_clip;
        bool had_saved = false;
        if (multi_word) {
          paste_text = build_replacement_text(original, *target);
          if (clip_restore.pending) {
            had_saved = clip_restore.had_saved;
            saved_clip = std::move(clip_restore.saved);
            clip_restore.pending = false;
          } else {
            had_saved = clipboard.read(kClipboardTimeoutMs, saved_clip);
          }
        }
        std::vector<EchoKey> echoes;
        retype.replace(static_cast<int>(original.size()), {}, echoes);
        if (cinnamon.activate(target_index) &&
            groups.wait_until(*target, kLayoutSwitchTimeoutMs)) {
          if (multi_word && !paste_text.empty() &&
              clipboard.set(std::move(paste_text))) {
            retype.send_ctrl_v(echoes);
            clip_restore = ClipboardRestore{
                true, had_saved, false, std::move(saved_clip),
                std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(kClipboardRestoreDelayMs)};
          } else {
            if (multi_word) {
              if (had_saved) {
                clipboard.set(std::move(saved_clip));
              } else {
                clipboard.clear();
              }
            }
            retype.replace(0, output, echoes);
          }
          undo = Undo{
              true,  std::move(original), std::move(output), from_index, lang,
              *target};
          update_tray();
          log::info("виправлено слово або контекст; мова " +
                    std::string(lang_name(*target)));
        } else {
          if (multi_word) {
            if (had_saved) {
              clipboard.set(std::move(saved_clip));
            } else {
              clipboard.clear();
            }
          }
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
      std::array<std::string, 3> current_word_forms;
      if (buffer.has_word()) {
        for (const Lang candidate : kSupportedLanguages) {
          current_word_forms[lang_index(candidate)] = buffer.text(candidate);
        }
        std::array<bool, 3> visited{false, false, false};
        if (lang != Lang::Other) {
          visited[lang_index(lang)] = true;
        }
        last_word = LastWord{true, current_word_forms, buffer.strokes(), lang,
                             visited};
      } else {
        last_word.valid = false;
      }
      const auto& current_word = buffer.text(lang);
      const bool ignored =
          buffer.has_word() && is_ignored_word(current_word, ignore_set);
      const bool is_short = utf8::char_count(current_word) <
                            static_cast<std::size_t>(config.min_length);
      const bool is_ambiguous =
          buffer.has_word() &&
          is_ambiguous_word(current_word_forms, lang, active_dictionaries,
                            &ignore_set);
      if (whitespace_boundary && buffer.has_word() && !ignored &&
          (is_short || is_ambiguous)) {
        pending_short.push_back(
            PendingWord{std::move(current_word_forms), buffer.strokes()});
        if (pending_short.size() > 3) {
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
