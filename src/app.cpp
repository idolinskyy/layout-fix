#include "app.hpp"

#include "detect/detector.hpp"
#include "events.hpp"
#include "input/word_buffer.hpp"
#include "layout/cinnamon.hpp"
#include "layout/groups.hpp"
#include "log.hpp"
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
    std::vector<Stroke> strokes;
    int source_index = -1;
    Lang lang = Lang::Other;
};

unsigned keycode_of(XConnection& x11, KeySym sym) {
    return XKeysymToKeycode(x11.ctrl(), sym);
}

bool is_command_modifier(unsigned state) {
    return (state & (ControlMask | Mod1Mask | Mod4Mask | Mod5Mask)) != 0;
}

}  // namespace

int App::run(const Config& config, bool check_only) {
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

    auto resolve = [](const std::string& configured, const std::vector<std::string>& fallbacks) {
        return resolve_dictionary(configured, fallbacks);
    };
    const auto en_path = resolve(config.dict_en, {"/usr/share/hunspell/en_US.dic"});
    const auto uk_path = resolve(config.dict_uk,
                                 {"/usr/share/hunspell/uk_UA.dic",
                                  LAYOUT_FIX_DATA_DIR "/uk_UA.dic",
                                  "data/dicts/uk_UA.dic",
                                  "../data/dicts/uk_UA.dic",
                                  "../../data/dicts/uk_UA.dic"});
    Dictionary english_dict;
    Dictionary ukrainian_dict;
    if (en_path.empty() || !english_dict.load_file(en_path)) {
        log::error("немає англійського словника. Встановіть hunspell-en-us");
        return 1;
    }
    if (uk_path.empty() || !ukrainian_dict.load_file(uk_path)) {
        log::error("немає українського словника. Встановіть hunspell-uk або залиште data/dicts/uk_UA.dic");
        return 1;
    }
    log::info("словники: en " + std::to_string(english_dict.size()) + " (" + en_path + "), uk " +
              std::to_string(ukrainian_dict.size()) + " (" + uk_path + ")");
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
    const unsigned pause_kc = keycode_of(x11, XStringToKeysym(pause_key.keysym.c_str()));
    const unsigned undo_kc = keycode_of(x11, XStringToKeysym(undo_key.keysym.c_str()));
    const unsigned break_kc = keycode_of(x11, XK_Break);
    std::vector<unsigned> command_keys;
    for (KeySym sym : {XK_Control_L, XK_Control_R, XK_Alt_L, XK_Alt_R, XK_Super_L, XK_Super_R,
                       XK_Meta_L, XK_Meta_R, XK_ISO_Level3_Shift}) {
        if (const unsigned kc = keycode_of(x11, sym); kc != 0) {
            command_keys.push_back(kc);
        }
    }
    std::vector<unsigned> nav_keys;
    for (KeySym sym : {XK_Left, XK_Right, XK_Up, XK_Down, XK_Home, XK_End, XK_Delete, XK_Escape,
                       XK_Prior, XK_Next, XK_Insert, XK_KP_Left, XK_KP_Right, XK_KP_Up, XK_KP_Down,
                       XK_KP_Home, XK_KP_End, XK_KP_Delete, XK_KP_Prior, XK_KP_Next, XK_BackSpace}) {
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
    bool arm_echoes = false;
    int echoes = 0;
    auto inject_deadline = std::chrono::steady_clock::now();
    std::string seen_class;

    auto key_in = [](unsigned keycode, const std::vector<unsigned>& keys) {
        for (unsigned key : keys) {
            if (key == keycode) {
                return true;
            }
        }
        return false;
    };

    auto finish_inject_soon = [&](int count) {
        injecting = true;
        arm_echoes = false;
        echoes = count;
        inject_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
        if (echoes <= 0) {
            injecting = false;
        }
    };

    log::info("слухаю клавіатуру. Пауза: " + config.pause_key + ", скасувати: " + config.undo_key);

    while (g_stop == 0) {
        if (injecting && std::chrono::steady_clock::now() > inject_deadline) {
            log::error("не дочекався відлуння власних клавіш, продовжую слухати");
            injecting = false;
            arm_echoes = false;
            echoes = 0;
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
        if (focus.wm_class() != seen_class) {
            seen_class = focus.wm_class();
            buffer.clear();
            undo.valid = false;
        }

        const auto batch = listener.take();
        for (const RawEvent& event : batch) {
            if (injecting && arm_echoes) {
                if (event.type != RawEvent::Type::Button && echoes > 0) {
                    --echoes;
                }
                if (echoes <= 0) {
                    injecting = false;
                    arm_echoes = false;
                }
                continue;
            }
            if (event.type == RawEvent::Type::Release || event.type == RawEvent::Type::Button) {
                if (event.type == RawEvent::Type::Button) {
                    buffer.clear();
                    undo.valid = false;
                }
                continue;
            }

            const bool shift = (event.state & ShiftMask) != 0;
            const bool caps = (event.state & LockMask) != 0;
            const bool pause_hit =
                event.keycode != 0 && (event.keycode == pause_kc || event.keycode == break_kc ||
                                       event.keycode == undo_kc);
            if (pause_hit && !is_command_modifier(event.state)) {
                const bool want_undo = undo_key.shift == shift && event.keycode == undo_kc;
                const bool want_pause = pause_key.shift == shift &&
                                        (event.keycode == pause_kc || event.keycode == break_kc);
                if (want_undo && undo.valid) {
                    auto strokes = undo.strokes;
                    const int index = undo.source_index;
                    const Lang lang = undo.lang;
                    undo.valid = false;
                    buffer.clear();
                    // Спершу стираємо виправлене слово в поточній розкладці, потім повертаємо стару.
                    const int deleted = static_cast<int>(strokes.size());
                    std::vector<Stroke> empty;
                    int count = retype.replace(deleted, empty);
                    if (cinnamon.activate(index) && groups.wait_until(lang, 150)) {
                        count += retype.replace(0, strokes);
                        log::info("скасовано останнє виправлення");
                    } else {
                        count += retype.replace(0, strokes);
                        log::error("не вдалося повернути розкладку для скасування");
                    }
                    x11.flush_ctrl();
                    finish_inject_soon(count);
                } else if (want_pause) {
                    paused = !paused;
                    buffer.clear();
                    undo.valid = false;
                    log::info(paused ? "пауза" : "знову слухаю");
                }
                continue;
            }

            if (paused || focus.excluded(config.exclude) || is_command_modifier(event.state) ||
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
            const int other_group = groups.group_for(lang == Lang::En ? Lang::Uk : Lang::En);
            const auto current = groups.translate(event.keycode, group, shift, caps);
            const auto other = groups.translate(event.keycode, other_group, shift, caps);
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
            const auto& word = buffer.current_text();
            const auto& alternate = buffer.other_text();
            const bool english = lang == Lang::En;
            const auto decision =
                decide(word, alternate, english ? english_dict : ukrainian_dict,
                       english ? ukrainian_dict : english_dict, config.min_length);
            if (decision == Decision::Switch && buffer.has_word()) {
                const Lang target = english ? Lang::Uk : Lang::En;
                const int target_index = cinnamon.index_of(target == Lang::Uk ? "ua" : "us");
                const int target_group = groups.group_for(target);
                std::vector<Stroke> original = buffer.strokes();
                std::vector<Stroke> output;
                output.reserve(original.size());
                for (const auto& stroke : original) {
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
                int count = retype.replace(static_cast<int>(original.size()), {});
                if (cinnamon.activate(target_index) && groups.wait_until(target, 150)) {
                    count += retype.replace(0, output);
                    undo = Undo{true, std::move(original), from_index, lang};
                    log::info("виправлено слово, розкладку перемкнено");
                } else {
                    count += retype.replace(0, original);
                    log::error("не вдалося перемкнути розкладку, слово повернуто");
                }
                x11.flush_ctrl();
                finish_inject_soon(count);
            } else {
                undo.valid = false;
            }
            buffer.clear();
        }
        if (injecting) {
            arm_echoes = true;
        }
    }

    log::info("зупинено");
    return 0;
}

}  // namespace layoutfix
