#include "detect/dictionary.hpp"
#include "detect/selection.hpp"
#include "input/echoes.hpp"
#include "input/word_buffer.hpp"
#include "layout/groups.hpp"
#include "x11/clipboard.hpp"
#include "x11/connection.hpp"
#include "x11/focus.hpp"
#include "x11/listener.hpp"
#include "x11/retype.hpp"

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <poll.h>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, int line) {
  if (!condition) {
    std::cerr << "X11 FAIL рядок " << line << '\n';
    ++failures;
  }
}

#define CHECK(cond) expect((cond), __LINE__)

bool drain_echoes(layoutfix::Listener &listener,
                  std::vector<layoutfix::EchoKey> &echoes, int timeout_ms) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (!echoes.empty() && std::chrono::steady_clock::now() < deadline) {
    listener.drain();
    for (const auto &ev : listener.take()) {
      const bool button = ev.type == layoutfix::RawEvent::Type::Button;
      const auto res = layoutfix::consume_echo(
          echoes, button, ev.type == layoutfix::RawEvent::Type::Press,
          ev.keycode);
      if (res == layoutfix::EchoConsume::Done) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return echoes.empty();
}

} // namespace

int main(int argc, char **argv) {
  bool isolated = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--isolated-display") {
      isolated = true;
    }
  }
  if (!isolated) {
    std::cerr << "Відмова: запускайте лише на ізольованому Xvfb через "
                 "scripts/run-x11-test.sh (--isolated-display)\n";
    return 2;
  }

  layoutfix::XConnection x11;
  CHECK(x11.open());
  if (failures != 0) {
    return 1;
  }

  // 1. Groups + real XKB us,ua,ru + layout_maps + selection conversion.
  layoutfix::Groups groups(x11);
  CHECK(groups.init(false));
  CHECK(groups.group_for(layoutfix::Lang::En) == 0);
  CHECK(groups.group_for(layoutfix::Lang::Uk) == 1);
  CHECK(groups.group_for(layoutfix::Lang::Ru) == 2);

  const unsigned s_kc = XKeysymToKeycode(x11.ctrl(), XK_s);
  const unsigned comma_kc = XKeysymToKeycode(x11.ctrl(), XK_comma);
  const unsigned space_kc = XKeysymToKeycode(x11.ctrl(), XK_space);
  const unsigned o_kc = XKeysymToKeycode(x11.ctrl(), XK_o);
  const unsigned j_kc = XKeysymToKeycode(x11.ctrl(), XK_j);
  CHECK(s_kc != 0 && comma_kc != 0 && space_kc != 0 && o_kc != 0 && j_kc != 0);

  CHECK(groups.translate(s_kc, 0, false, false).text == "s");
  CHECK(groups.translate(s_kc, 1, false, false).text == "і");
  CHECK(groups.translate(s_kc, 2, false, false).text == "ы");
  CHECK(groups.translate(comma_kc, 0, false, false).text == ",");
  CHECK(groups.translate(comma_kc, 1, false, false).text == "б");
  CHECK(groups.translate(space_kc, 0, false, false).kind ==
        layoutfix::Translated::Kind::Boundary);
  const auto space_hit = groups.find_key(1, " ");
  CHECK(space_hit.has_value() && space_hit->keycode == space_kc &&
        !space_hit->shift);

  layoutfix::Dictionary en_dict;
  layoutfix::Dictionary uk_dict;
  layoutfix::Dictionary ru_dict;
  CHECK(en_dict.load_text("2\nhello\nworld\n"));
  CHECK(uk_dict.load_text("2\nпривіт\nсвіт\n"));
  CHECK(ru_dict.load_text("2\nпривет\nмир\n"));
  const std::array<const layoutfix::Dictionary *, 3> dicts{&en_dict, &uk_dict,
                                                           &ru_dict};
  const std::array<bool, 3> all_active{true, true, true};
  const auto conv_uk = layoutfix::convert_selection(
      "ghbdsn cdsn", groups.layout_maps(), dicts, all_active,
      layoutfix::Lang::En);
  CHECK(conv_uk.changed && conv_uk.target == layoutfix::Lang::Uk &&
        conv_uk.text == "привіт світ");
  const auto conv_ru = layoutfix::convert_selection(
      "ghbdtn vbh", groups.layout_maps(), dicts, all_active,
      layoutfix::Lang::En);
  CHECK(conv_ru.changed && conv_ru.target == layoutfix::Lang::Ru &&
        conv_ru.text == "привет мир");
  const auto conv_en = layoutfix::convert_selection(
      "руддщб цщкдвю", groups.layout_maps(), dicts, all_active,
      layoutfix::Lang::Uk);
  CHECK(conv_en.changed && conv_en.target == layoutfix::Lang::En &&
        conv_en.text == "hello, world.");

  // Group switching check via XkbLockGroup.
  XkbLockGroup(x11.ctrl(), XkbUseCoreKbd, 1);
  XFlush(x11.ctrl());
  CHECK(groups.wait_until(layoutfix::Lang::Uk, 150));
  XkbLockGroup(x11.ctrl(), XkbUseCoreKbd, 0);
  XFlush(x11.ctrl());
  CHECK(groups.wait_until(layoutfix::Lang::En, 150));

  // 2. Own isolated test window + Focus (_NET_ACTIVE_WINDOW / WM_CLASS).
  Display *target_dpy = XOpenDisplay(nullptr);
  CHECK(target_dpy != nullptr);
  Window target_win = XCreateSimpleWindow(
      target_dpy, DefaultRootWindow(target_dpy), 10, 10, 200, 100, 0, 0, 0);
  XSelectInput(target_dpy, target_win, KeyPressMask | KeyReleaseMask);
  XClassHint hint{};
  char res_name[] = "test-editor";
  char res_class[] = "TestEditor";
  hint.res_name = res_name;
  hint.res_class = res_class;
  XSetClassHint(target_dpy, target_win, &hint);
  XMapWindow(target_dpy, target_win);
  XSync(target_dpy, False);
  XSetInputFocus(target_dpy, target_win, RevertToParent, CurrentTime);
  const Atom active_atom =
      XInternAtom(target_dpy, "_NET_ACTIVE_WINDOW", False);
  XChangeProperty(target_dpy, DefaultRootWindow(target_dpy), active_atom,
                  XA_WINDOW, 32, PropModeReplace,
                  reinterpret_cast<const unsigned char *>(&target_win), 1);
  XSync(target_dpy, False);

  layoutfix::Focus focus(x11);
  CHECK(focus.install());
  CHECK(focus.window_id() == static_cast<unsigned long>(target_win));
  CHECK(focus.wm_class() == "TestEditor");
  CHECK(!focus.excluded({"gnome-terminal", "xterm"}));
  CHECK(focus.excluded({"gnome-terminal", "testeditor"}));

  // 3. Listener (XRecord) + Retype (XTest) + echo consumption on own window.
  layoutfix::Retype retype(x11);
  layoutfix::Listener listener(x11);
  layoutfix::Clipboard clipboard(x11);
  CHECK(retype.init());
  CHECK(listener.start());
  CHECK(clipboard.init());

  std::vector<layoutfix::Stroke> strokes{
      {s_kc, true, true, "S", {}, {"S", "І", "Ы"}},
      {space_kc, false, false, " ", {}, {}},
      {o_kc, false, true, "o", {}, {"o", "щ", "щ"}},
      {j_kc, false, true, "j", {}, {"j", "о", "о"}},
      {space_kc, false, false, " ", {}, {}},
  };
  std::vector<layoutfix::EchoKey> echoes;
  retype.replace(2, strokes, echoes);
  CHECK(drain_echoes(listener, echoes, 400));

  // Verify target window received 2 Backspaces + S + Space + o + j + Space.
  std::vector<std::pair<unsigned, bool>> received_keys;
  const auto key_deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
  while (received_keys.size() < 7 &&
         std::chrono::steady_clock::now() < key_deadline) {
    while (XPending(target_dpy) > 0) {
      XEvent ev;
      XNextEvent(target_dpy, &ev);
      if (ev.type == KeyPress) {
        const unsigned kc = ev.xkey.keycode;
        if (kc == retype.backspace_keycode() || kc == s_kc || kc == space_kc ||
            kc == o_kc || kc == j_kc) {
          received_keys.emplace_back(kc, (ev.xkey.state & ShiftMask) != 0);
        }
      }
    }
    if (received_keys.size() < 7) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  CHECK(received_keys.size() == 7);
  if (received_keys.size() == 7) {
    CHECK(received_keys[0].first == retype.backspace_keycode());
    CHECK(received_keys[1].first == retype.backspace_keycode());
    CHECK(received_keys[2].first == s_kc && received_keys[2].second);
    CHECK(received_keys[3].first == space_kc && !received_keys[3].second);
    CHECK(received_keys[4].first == o_kc && !received_keys[4].second);
    CHECK(received_keys[5].first == j_kc && !received_keys[5].second);
    CHECK(received_keys[6].first == space_kc && !received_keys[6].second);
  }

  // Verify Ctrl+C / Ctrl+V XTest shortcuts and echo matching.
  const unsigned c_kc = XKeysymToKeycode(x11.ctrl(), XK_c);
  const unsigned v_kc = XKeysymToKeycode(x11.ctrl(), XK_v);
  echoes.clear();
  retype.send_ctrl_c(echoes);
  retype.send_ctrl_v(echoes);
  CHECK(drain_echoes(listener, echoes, 400));
  bool saw_ctrl_c = false;
  bool saw_ctrl_v = false;
  const auto shortcut_deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
  while (!(saw_ctrl_c && saw_ctrl_v) &&
         std::chrono::steady_clock::now() < shortcut_deadline) {
    while (XPending(target_dpy) > 0) {
      XEvent ev;
      XNextEvent(target_dpy, &ev);
      if (ev.type == KeyPress && (ev.xkey.state & ControlMask) != 0) {
        if (ev.xkey.keycode == c_kc) {
          saw_ctrl_c = true;
        } else if (ev.xkey.keycode == v_kc) {
          saw_ctrl_v = true;
        }
      }
    }
    if (!(saw_ctrl_c && saw_ctrl_v)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  CHECK(saw_ctrl_c);
  CHECK(saw_ctrl_v);

  // 4. Clipboard round-trip + sentinel owner-change between two X11 clients.
  layoutfix::XConnection peer_x11;
  CHECK(peer_x11.open());
  layoutfix::Clipboard peer_clip(peer_x11);
  CHECK(peer_clip.init());

  // 4a. Serve main clipboard while peer reads UTF-8 text.
  CHECK(clipboard.set("Привіт, світ!\nДля примера "));
  std::atomic<bool> stop_main_serve{false};
  std::thread main_serve([&] {
    while (!stop_main_serve.load()) {
      pollfd pfd{x11.ctrl_fd(), POLLIN, 0};
      poll(&pfd, 1, 10);
      while (XPending(x11.ctrl()) > 0) {
        XEvent ev;
        XNextEvent(x11.ctrl(), &ev);
        focus.handle_event(ev);
        clipboard.handle_event(ev);
      }
    }
  });
  std::string peer_got;
  CHECK(peer_clip.read(250, peer_got));
  CHECK(peer_got == "Привіт, світ!\nДля примера ");
  stop_main_serve.store(true);
  main_serve.join();
  CHECK(clipboard.content_served());

  // 4b. Main claims sentinel -> peer claims clipboard -> main detects owner change and reads.
  CHECK(clipboard.claim_sentinel());
  CHECK(peer_clip.set("ghbdsn cdsn"));
  std::atomic<bool> stop_peer{false};
  std::thread peer_loop([&] {
    while (!stop_peer.load()) {
      pollfd pfd{peer_x11.ctrl_fd(), POLLIN, 0};
      poll(&pfd, 1, 10);
      while (XPending(peer_x11.ctrl()) > 0) {
        XEvent ev;
        XNextEvent(peer_x11.ctrl(), &ev);
        peer_clip.handle_event(ev);
      }
    }
  });
  CHECK(clipboard.wait_for_owner_change(250));
  std::string main_got;
  CHECK(clipboard.read(250, main_got));
  CHECK(main_got == "ghbdsn cdsn");
  stop_peer.store(true);
  peer_loop.join();

  // Англійська не мусить бути першою групою XKB.
  CHECK(std::system("setxkbmap -rules evdev -model pc105 -layout ua,us,ru") ==
        0);
  CHECK(groups.init(false));
  CHECK(groups.group_for(layoutfix::Lang::Uk) == 0);
  CHECK(groups.group_for(layoutfix::Lang::En) == 1);
  CHECK(groups.group_for(layoutfix::Lang::Ru) == 2);
  CHECK(groups.translate(s_kc, 0, false, false).text == "і");
  CHECK(groups.translate(s_kc, 1, false, false).text == "s");
  if (std::system("setxkbmap -rules evdev -model pc105 -layout "
                  "'ua(phonetic),us'") == 0) {
    CHECK(groups.init(false));
    CHECK(groups.group_for(layoutfix::Lang::Uk) == 0);
    CHECK(groups.group_for(layoutfix::Lang::En) == 1);
  }

  XDestroyWindow(target_dpy, target_win);
  XCloseDisplay(target_dpy);

  if (failures != 0) {
    std::cerr << failures << " перевірок X11 не пройшло\n";
    return 1;
  }
  std::cout << "x11 ok\n";
  return 0;
}
