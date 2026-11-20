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
#include "x11/connection.hpp"
#include "x11/focus.hpp"
#include "x11/listener.hpp"
#include "x11/retype.hpp"

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

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

unsigned keycode_of(XConnection &x11, KeySym sym) {
  return XKeysymToKeycode(x11.ctrl(), sym);
}

bool is_command_modifier(unsigned state) {
  return (state & (ControlMask | Mod1Mask | Mod4Mask | Mod5Mask)) != 0;
}

} // namespace

int App::run(const Config &config, bool check_only) {
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
  Dictionary english_dict;
  Dictionary ukrainian_dict;
  if (en_path.empty() || !english_dict.load_file(en_path)) {
    log::error("немає англійського словника. Встановіть hunspell-en-us");
    return 1;
  }
  if (uk_path.empty() || !ukrainian_dict.load_file(uk_path)) {
    log::error("немає українського словника. Встановіть hunspell-uk або "
               "залиште data/dicts/uk_UA.dic");
    return 1;
  }
  log::info("словники: en " + std::to_string(english_dict.size()) + " (" +
            en_path + (english_dict.has_morphology() ? ", Hunspell), uk "
                                                       : ", точні основи), uk ") +
            std::to_string(ukrainian_dict.size()) + " (" +
            uk_path + (ukrainian_dict.has_morphology() ? ", Hunspell)"
                                                        : ", точні основи)"));
  if (!focus.wm_class().empty() || !focus.wm_name().empty()) {
    log::info("активне вікно: " + focus.wm_class() + " / " + focus.wm_name());
  }
  if (check_only) {
    log::info("перевірку завершено");
    return 0;
  }

  Retype retype(x11);
  Listener listener(x11);
  if (!retype.init() || !listener.start()) {
    return 1;
  }

  const auto pause_key = parse_hotkey(config.pause_key);
  const auto undo_key = parse_hotkey(config.undo_key);
  const unsigned pause_kc =
      keycode_of(x11, XStringToKeysym(pause_key.keysym.c_str()));
  const unsigned undo_kc =
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

  log::info("слухаю клавіатуру. Пауза: " + config.pause_key +
            ", скасувати: " + config.undo_key + ". Таймаут перемикання: " +
            std::to_string(kLayoutSwitchTimeoutMs) + " мс");

  while (g_stop == 0) {
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
    if (focus.window_id() != seen_window) {
      seen_window = focus.window_id();
      buffer.clear();
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
          buffer.clear();
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
          buffer.clear();
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
          buffer.clear();
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
          buffer.clear();
          undo.valid = false;
          log::info(paused ? "пауза" : "знову слухаю");
        }
        continue;
      }

      if (paused || focus.excluded(config.exclude) ||
          is_command_modifier(event.state) ||
          key_in(event.keycode, command_keys)) {
        buffer.clear();
        undo.valid = false;
        continue;
      }
      if (event.keycode == retype.backspace_keycode()) {
        buffer.pop();
        undo.valid = false;
        continue;
      }
      if (key_in(event.keycode, nav_keys)) {
        buffer.clear();
        undo.valid = false;
        continue;
      }

      const int group = groups.current_group();
      const Lang lang = groups.language_of(group);
      if (lang != Lang::En && lang != Lang::Uk) {
        buffer.clear();
        continue;
      }
      if (buffer.group() >= 0 && buffer.group() != group) {
        buffer.clear();
      }
      const int other_group =
          groups.group_for(lang == Lang::En ? Lang::Uk : Lang::En);
      const auto current = groups.translate(event.keycode, group, shift, caps);
      const auto other =
          groups.translate(event.keycode, other_group, shift, caps);
      if (config.debug_keys && !current.text.empty()) {
        log::keys(current.text + " | " + other.text);
      }
      if (current.kind == Translated::Kind::Ignore) {
        continue;
      }
      if (current.kind == Translated::Kind::Word) {
        undo.valid = false;
        if (buffer.group() < 0) {
          buffer.set_group(group);
        }
        buffer.push_word(event.keycode, shift, current.text, other.text);
        continue;
      }

      buffer.push_boundary(event.keycode, shift, current.text);
      const auto &word = buffer.current_text();
      const auto &alternate = buffer.other_text();
      const bool english = lang == Lang::En;
      const auto decision =
          decide(word, alternate, english ? english_dict : ukrainian_dict,
                 english ? ukrainian_dict : english_dict, config.min_length);
      if (decision == Decision::Switch && buffer.has_word()) {
        const Lang target = english ? Lang::Uk : Lang::En;
        const int target_index =
            cinnamon.index_of(target == Lang::Uk ? "ua" : "us");
        const int target_group = groups.group_for(target);
        std::vector<Stroke> original = buffer.strokes();
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
        const int from_index = cinnamon.index_of(english ? "us" : "ua");
        std::vector<EchoKey> echoes;
        retype.replace(static_cast<int>(original.size()), {}, echoes);
        if (cinnamon.activate(target_index) &&
            groups.wait_until(target, kLayoutSwitchTimeoutMs)) {
          retype.replace(0, output, echoes);
          undo = Undo{
              true,  std::move(original), std::move(output), from_index, lang,
              target};
          log::info("виправлено слово, розкладку перемкнено");
        } else {
          retype.replace(0, original, echoes);
          log::error("не вдалося перемкнути розкладку за " +
                     std::to_string(kLayoutSwitchTimeoutMs) +
                     " мс, слово повернуто");
        }
        x11.flush_ctrl();
        begin_inject(std::move(echoes));
      } else {
        undo.valid = false;
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
