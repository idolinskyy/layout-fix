#include "layout/groups.hpp"

#include "layout/classify.hpp"
#include "log.hpp"
#include "utf8.hpp"

#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <xkbcommon/xkbcommon.h>

#include <array>
#include <chrono>
#include <thread>
#include <unordered_map>

namespace layoutfix {
namespace {

constexpr int kMaxGroups = 4;

std::string keysym_utf8(KeySym sym) {
  if (sym == NoSymbol || sym == 0) {
    return {};
  }
  char buf[16] = {};
  const int n =
      xkb_keysym_to_utf8(static_cast<xkb_keysym_t>(sym), buf, sizeof buf);
  if (n <= 1) {
    return {};
  }
  return std::string(buf, buf + n - 1);
}

bool is_keypad_keysym(KeySym sym) {
  return sym >= XK_KP_Space && sym <= XK_KP_Equal;
}

} // namespace

struct Groups::Impl {
  XkbDescPtr desc = nullptr;
  std::array<Lang, kMaxGroups> langs{};
  std::array<std::unordered_map<std::string, KeyHit>, kMaxGroups> keys{};
  LayoutMaps layout_maps{};

  ~Impl() {
    if (desc != nullptr) {
      XkbFreeKeyboard(desc, 0, True);
    }
  }

  KeySym keysym(unsigned keycode, int group, int level) const {
    if (desc == nullptr || group < 0) {
      return NoSymbol;
    }
    const int groups = XkbKeyNumGroups(desc, keycode);
    if (groups <= 0 || level < 0) {
      return NoSymbol;
    }
    int use = group;
    if (group >= groups) {
      const unsigned info = XkbKeyGroupInfo(desc, keycode);
      switch (XkbOutOfRangeGroupAction(info)) {
      case XkbRedirectIntoRange:
        use = static_cast<int>(XkbOutOfRangeGroupNumber(info));
        if (use >= groups) {
          use = 0;
        }
        break;
      case XkbClampIntoRange:
        use = groups - 1;
        break;
      default:
        use = group % groups;
        break;
      }
    }
    const int width = XkbKeyGroupsWidth(desc, keycode);
    if (width <= 0 || level >= width) {
      return NoSymbol;
    }
    return XkbKeySymsPtr(desc, keycode)[use * width + level];
  }
};

Groups::~Groups() { delete impl_; }

const char *lang_name(Lang lang) {
  switch (lang) {
  case Lang::En:
    return "en";
  case Lang::Uk:
    return "uk";
  case Lang::Ru:
    return "ru";
  case Lang::Other:
    return "інша";
  }
  return "?";
}

bool Groups::init(bool report) {
  delete impl_;
  impl_ = new Impl();
  auto *dpy = x11_.ctrl();
  impl_->desc = XkbGetMap(dpy, XkbAllClientInfoMask, XkbUseCoreKbd);
  if (impl_->desc == nullptr) {
    if (report) {
      log::error("не вдалося прочитати розкладку XKB");
    }
    return false;
  }

  int min_kc = 0;
  int max_kc = 0;
  XDisplayKeycodes(dpy, &min_kc, &max_kc);

  // Літери для розпізнавання мови беремо лише з реально заданої групи.
  // Інакше XKB підставляє групу 0 у клавіші без власного символу, і англійська
  // група після української отримує чужі літери.
  auto symbols_at = [&](unsigned keycode, int group, int level) -> KeySym {
    if (impl_->desc == nullptr || group < 0 || level < 0) {
      return NoSymbol;
    }
    const int defined = XkbKeyNumGroups(impl_->desc, keycode);
    if (defined <= 0 || group >= defined) {
      return NoSymbol;
    }
    const int width = XkbKeyGroupsWidth(impl_->desc, keycode);
    if (width <= 0 || level >= width) {
      return NoSymbol;
    }
    return XkbKeySymsPtr(impl_->desc, keycode)[group * width + level];
  };

  std::array<GroupLetters, kMaxGroups> seen{};
  for (int group = 0; group < kMaxGroups; ++group) {
    auto &map = impl_->keys[static_cast<std::size_t>(group)];
    for (int kc = min_kc; kc <= max_kc; ++kc) {
      for (int level = 0; level < 2; ++level) {
        const auto owned = keysym_utf8(
            symbols_at(static_cast<unsigned>(kc), group, level));
        if (!owned.empty()) {
          note_group_letter(seen[static_cast<std::size_t>(group)], owned);
        }
        auto utf =
            keysym_utf8(impl_->keysym(static_cast<unsigned>(kc), group, level));
        if (utf.empty()) {
          continue;
        }
        if (map.contains(utf)) {
          continue;
        }
        map.emplace(std::move(utf),
                    KeyHit{static_cast<unsigned>(kc), level == 1});
      }
    }
    impl_->langs[static_cast<std::size_t>(group)] =
        classify_group_letters(seen[static_cast<std::size_t>(group)]);
  }

  if (group_for(Lang::En) < 0 || group_for(Lang::Uk) < 0) {
    if (report) {
      log::error("потрібні обидві групи XKB: англійська і українська");
    }
    return false;
  }

  std::array<int, 3> lang_groups{-1, -1, -1};
  for (const Lang lang : kSupportedLanguages) {
    lang_groups[lang_index(lang)] = group_for(lang);
  }
  for (int kc = min_kc; kc <= max_kc; ++kc) {
    for (int level = 0; level < 2; ++level) {
      std::array<std::string, 3> chars;
      for (std::size_t i = 0; i < 3; ++i) {
        if (lang_groups[i] < 0) {
          continue;
        }
        const KeySym sym =
            impl_->keysym(static_cast<unsigned>(kc), lang_groups[i], level);
        if (is_keypad_keysym(sym)) {
          continue;
        }
        chars[i] = keysym_utf8(sym);
      }
      for (std::size_t from = 0; from < 3; ++from) {
        if (chars[from].empty()) {
          continue;
        }
        for (std::size_t to = 0; to < 3; ++to) {
          if (from == to || chars[to].empty()) {
            continue;
          }
          impl_->layout_maps.tables[from][to].emplace(chars[from], chars[to]);
        }
      }
    }
  }

  if (report) {
    log::info(std::string("XKB en=група ") +
              std::to_string(group_for(Lang::En)) + ", uk=група " +
              std::to_string(group_for(Lang::Uk)));
  }
  return true;
}

Lang Groups::language_of(int group) const {
  if (impl_ == nullptr || group < 0 || group >= kMaxGroups) {
    return Lang::Other;
  }
  return impl_->langs[static_cast<std::size_t>(group)];
}

int Groups::group_for(Lang lang) const {
  if (impl_ == nullptr) {
    return -1;
  }
  for (int group = 0; group < kMaxGroups; ++group) {
    if (impl_->langs[static_cast<std::size_t>(group)] == lang) {
      return group;
    }
  }
  return -1;
}

int Groups::current_group() const {
  XkbStateRec state{};
  // XkbGetState returns 0 (Success), не True. Стан заповнений і в цьому
  // випадку. state.group — ефективна група (base + latched + locked). Мову
  // визначає літера на S.
  XkbGetState(x11_.ctrl(), XkbUseCoreKbd, &state);
  return static_cast<int>(state.group);
}

bool Groups::wait_until(Lang lang, int timeout_ms) const {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  do {
    if (language_of(current_group()) == lang) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < deadline);
  return language_of(current_group()) == lang;
}

Translated Groups::translate(unsigned keycode, int group, bool shift,
                             bool caps) const {
  Translated out;
  if (impl_ == nullptr) {
    return out;
  }
  const KeySym plain = impl_->keysym(keycode, group, 0);
  int level = shift ? 1 : 0;
  const auto plain_text = keysym_utf8(plain);
  std::size_t offset = 0;
  const auto plain_cp =
      plain_text.empty() ? 0 : utf8::decode_at(plain_text, offset);
  if (caps && utf8::is_letter(plain_cp)) {
    level ^= 1;
  }
  const KeySym sym = impl_->keysym(keycode, group, level);
  if (sym == XK_space || sym == XK_KP_Space || sym == XK_Return ||
      sym == XK_KP_Enter || sym == XK_Tab || sym == XK_ISO_Enter) {
    out.kind = Translated::Kind::Boundary;
    out.text = keysym_utf8(sym);
    if (out.text.empty()) {
      out.text = sym == XK_Tab                           ? "\t"
                 : sym == XK_space || sym == XK_KP_Space ? " "
                                                         : "\r";
    }
    return out;
  }
  out.text = keysym_utf8(sym);
  if (out.text.empty()) {
    return out;
  }
  offset = 0;
  const auto cp = utf8::decode_at(out.text, offset);
  if (offset == out.text.size() &&
      (cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r')) {
    out.kind = Translated::Kind::Boundary;
    return out;
  }
  if (offset == out.text.size() && utf8::is_word_char(cp)) {
    out.kind = Translated::Kind::Word;
    return out;
  }
  if (offset == out.text.size()) {
    out.kind = Translated::Kind::Boundary;
    return out;
  }
  out.kind = Translated::Kind::Word;
  return out;
}

std::optional<KeyHit> Groups::find_key(int group, std::string_view text) const {
  if (impl_ == nullptr || group < 0 || group >= kMaxGroups) {
    return std::nullopt;
  }
  const auto &map = impl_->keys[static_cast<std::size_t>(group)];
  const auto it = map.find(std::string(text));
  if (it == map.end()) {
    return std::nullopt;
  }
  return it->second;
}

const LayoutMaps &Groups::layout_maps() const {
  static const LayoutMaps kEmpty;
  return impl_ != nullptr ? impl_->layout_maps : kEmpty;
}

} // namespace layoutfix
