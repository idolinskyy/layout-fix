#include "config.hpp"
#include "log.hpp"
#include "detect/detector.hpp"
#include "a11y/password.hpp"
#include "detect/dictionary.hpp"
#include "input/pending.hpp"
#include "input/undo_state.hpp"
#include "input/word_buffer.hpp"
#include "layout/classify.hpp"
#include "layout/switch_outcome.hpp"
#include "detect/selection.hpp"
#include "input/echoes.hpp"
#include "input/undo_restore.hpp"
#include "tray_state.hpp"
#include "utf8.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, int line) {
  if (!condition) {
    std::cerr << "FAIL рядок " << line << '\n';
    ++failures;
  }
}

} // namespace

#define CHECK(cond) expect((cond), __LINE__)

int main() {
  const auto config_test_dir = std::filesystem::temp_directory_path() /
        ("layout-fix-config-test-" + std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()));
  layoutfix::Config saved_config;
  saved_config.min_length = 5;
  saved_config.dict_en = "/tmp/en_US.dic";
  saved_config.dict_uk = "/tmp/uk_UA.dic";
  saved_config.dict_ru = "/tmp/ru_RU.dic";
  saved_config.exclude = {"terminal-a", "terminal-b"};
  saved_config.ignore_words = {"kubectl", "Діма"};
  saved_config.pause_key = "F8";
  saved_config.undo_key = "Shift+F8";
  saved_config.convert_key = "Alt+F8";
  CHECK(layoutfix::save_config_file(
        saved_config, (config_test_dir / "layout-fix.conf").string()));
  const auto loaded_config =
        layoutfix::load_config((config_test_dir / "layout-fix.conf").string(),
                                           false);
  CHECK(loaded_config.min_length == saved_config.min_length);
  CHECK(loaded_config.dict_en == saved_config.dict_en);
  CHECK(loaded_config.dict_uk == saved_config.dict_uk);
  CHECK(loaded_config.dict_ru == saved_config.dict_ru);
  CHECK(loaded_config.exclude == saved_config.exclude);
  CHECK(loaded_config.ignore_words == saved_config.ignore_words);
  CHECK(loaded_config.pause_key == saved_config.pause_key);
  CHECK(loaded_config.undo_key == saved_config.undo_key);
  CHECK(loaded_config.convert_key == saved_config.convert_key);
  std::filesystem::remove_all(config_test_dir);

  // Default exclude list includes common terminals and password managers.
  const layoutfix::Config default_cfg;
  auto has_default_exclude = [&](std::string_view name) {
    for (const auto &item : default_cfg.exclude) {
      if (item == name) {
        return true;
      }
    }
    return false;
  };
  CHECK(has_default_exclude("gnome-terminal"));
  CHECK(has_default_exclude("alacritty"));
  CHECK(has_default_exclude("kitty"));
  CHECK(has_default_exclude("konsole"));
  CHECK(has_default_exclude("keepassxc"));
  CHECK(has_default_exclude("bitwarden"));
  CHECK(has_default_exclude("layout-fix"));
  CHECK(has_default_exclude("emacs"));
  CHECK(has_default_exclude("gvim"));

  const auto hk_pause = layoutfix::parse_hotkey("Pause");
  CHECK(hk_pause.keysym == "Pause" && !hk_pause.shift && !hk_pause.ctrl &&
        !hk_pause.alt && !hk_pause.super);
  const auto hk_undo = layoutfix::parse_hotkey("Shift+Pause");
  CHECK(hk_undo.keysym == "Pause" && hk_undo.shift && !hk_undo.ctrl &&
        !hk_undo.alt && !hk_undo.super);
  const auto hk_convert = layoutfix::parse_hotkey("Alt+Pause");
  CHECK(hk_convert.keysym == "Pause" && !hk_convert.shift && !hk_convert.ctrl &&
        hk_convert.alt && !hk_convert.super);
  const auto hk_combo = layoutfix::parse_hotkey(" ctrl + Shift + F9 ");
  CHECK(hk_combo.keysym == "F9" && hk_combo.shift && hk_combo.ctrl &&
        !hk_combo.alt && !hk_combo.super);
  CHECK(layoutfix::parse_hotkey("").keysym.empty());
  CHECK(layoutfix::parse_hotkey("Unknown+Pause").keysym.empty());
  CHECK(layoutfix::parse_hotkey("Alt+").keysym.empty());

  // Qt QKeySequenceEdit portable text <-> X11 config hotkey conversion.
  CHECK(layoutfix::hotkey_from_qt_sequence("Pause") == "Pause");
  CHECK(layoutfix::hotkey_from_qt_sequence("Shift+Pause") == "Shift+Pause");
  CHECK(layoutfix::hotkey_from_qt_sequence("Alt+Pause") == "Alt+Pause");
  CHECK(layoutfix::hotkey_from_qt_sequence("Meta+Esc") == "Super+Escape");
  CHECK(layoutfix::hotkey_from_qt_sequence("Ctrl+ScrollLock") ==
        "Ctrl+Scroll_Lock");
  CHECK(layoutfix::hotkey_from_qt_sequence("Alt+PgUp") == "Alt+Prior");
  CHECK(layoutfix::hotkey_from_qt_sequence("Ctrl+Space") == "Ctrl+space");
  CHECK(layoutfix::hotkey_from_qt_sequence("Alt+`") == "Alt+grave");
  CHECK(layoutfix::hotkey_from_qt_sequence("Ctrl++") == "Ctrl+plus");
  CHECK(layoutfix::hotkey_from_qt_sequence("Alt+Pause, Ctrl+X") == "Alt+Pause");
  CHECK(layoutfix::hotkey_from_qt_sequence("").empty());
  CHECK(layoutfix::hotkey_to_qt_sequence("Pause") == "Pause");
  CHECK(layoutfix::hotkey_to_qt_sequence("Shift+Pause") == "Shift+Pause");
  CHECK(layoutfix::hotkey_to_qt_sequence("Alt+Pause") == "Alt+Pause");
  CHECK(layoutfix::hotkey_to_qt_sequence("Super+Escape") == "Meta+Esc");
  CHECK(layoutfix::hotkey_to_qt_sequence("Ctrl+Scroll_Lock") ==
        "Ctrl+ScrollLock");
  CHECK(layoutfix::hotkey_to_qt_sequence("Alt+Prior") == "Alt+PgUp");
  CHECK(layoutfix::hotkey_to_qt_sequence("Alt+grave") == "Alt+`");
  CHECK(layoutfix::hotkey_to_qt_sequence("Ctrl+plus") == "Ctrl++");

  CHECK(layoutfix::utf8::casefold("Привіт") == "привіт");
  CHECK(layoutfix::utf8::casefold("HELLO") == "hello");
  CHECK(layoutfix::utf8::casefold("ҐІЇЄ") == "ґіїє");
  CHECK(layoutfix::utf8::char_count("привіт") == 6);
  CHECK(layoutfix::utf8::char_count("hello") == 5);

  layoutfix::Dictionary english;
  layoutfix::Dictionary ukrainian;
  layoutfix::Dictionary russian;
  CHECK(english.load_text("3\nhello/SM\nyes\nab\n"));
      CHECK(ukrainian.load_text("4\nпривіт/1$\nщо\nну\nі\n"));
  CHECK(russian.load_text("4\nпривет\nну\nи\nчто\n"));
  CHECK(english.contains("Hello"));
  CHECK(ukrainian.contains("Привіт"));
  CHECK(!english.contains("ghbdsn"));

  using layoutfix::Decision;
  CHECK(layoutfix::decide("hello", "руддщ", english, ukrainian, 3) ==
        Decision::Keep);
  CHECK(layoutfix::decide("ghbdsn", "привіт", english, ukrainian, 3) ==
        Decision::Switch);
  CHECK(layoutfix::decide("ab", "що", english, ukrainian, 3) == Decision::Keep);
  CHECK(layoutfix::decide("zzzz", "яяяя", english, ukrainian, 3) ==
        Decision::Keep);
  CHECK(layoutfix::decide("yes", "нуї", english, ukrainian, 3) ==
        Decision::Keep);
  CHECK(!english.contains("qzxjv"));
  CHECK(!ukrainian.contains("qzxjv"));
  CHECK(layoutfix::decide("qzxjv", "qzxjv", english, ukrainian, 3) ==
        Decision::Keep);
  CHECK(layoutfix::decide_context({"qx", "zz"}, {"ну", "і"}, english,
                                  ukrainian, 3) == Decision::Switch);
  CHECK(layoutfix::decide_context({"ab", "yes"}, {"ну", "і"}, english,
                                  ukrainian, 3) == Decision::Keep);
  CHECK(layoutfix::decide_context({"qx", "zz"}, {"ну", "qzxjv"}, english,
                                  ukrainian, 3) == Decision::Keep);
  CHECK(layoutfix::decide_context({"qx"}, {"ну"}, english, ukrainian, 3) ==
        Decision::Keep);

  const std::array<const layoutfix::Dictionary *, 3> dictionaries{
      &english, &ukrainian, &russian};
  const std::array<std::vector<std::string>, 3> unique_forms{{
      {"qx", "zz"}, {"ну", "і"}, {"qzxjv", "qzxjv"}}};
  CHECK(layoutfix::unique_language_candidate(unique_forms, layoutfix::Lang::En,
                                              dictionaries, 3) ==
        layoutfix::Lang::Uk);
  const std::array<std::vector<std::string>, 3> ambiguous_forms{{
      {"qx", "zz"}, {"ну", "і"}, {"ну", "и"}}};
  CHECK(!layoutfix::unique_language_candidate(ambiguous_forms,
                                               layoutfix::Lang::En,
                                               dictionaries, 3));
  const std::array<std::vector<std::string>, 3> missing_forms{{
      {"qzxjv", "qzxjv"}, {"qzxjv", "qzxjv"}, {"qzxjv", "qzxjv"}}};
  CHECK(!layoutfix::unique_language_candidate(missing_forms,
                                               layoutfix::Lang::En,
                                               dictionaries, 3));

  // Words with letters on punctuation keys (б, ю, ж, х, ї, ', э, ё) and
  // protection of real English punctuation.
  layoutfix::Dictionary punct_en;
  layoutfix::Dictionary punct_uk;
  layoutfix::Dictionary punct_ru;
  CHECK(punct_en.load_text("6\nhe\nks\nhello\nyes\nh\na\n"));
  CHECK(punct_uk.load_text(
      "12\nруб\nхліб\nжиття\nпроблемі\nлюдина\nможна\nхочу\nїжа\nтакож\nдух\nп'ять\nбо\nщо\n"));
  CHECK(punct_ru.load_text("3\nруб\nЭто\nёлка\n"));
  const std::array<const layoutfix::Dictionary *, 3> punct_dicts{
      &punct_en, &punct_uk, &punct_ru};

  // Mismatched punctuation "[ks," -> Ukrainian "хліб" (even though "ks" is in en).
  CHECK(layoutfix::unique_language_candidate(
            {{{"[ks,"}, {"хліб"}, {"хлыб"}}}, layoutfix::Lang::En, punct_dicts,
            3) == layoutfix::Lang::Uk);
  // Start / middle / end punctuation-key letters.
  CHECK(layoutfix::unique_language_candidate(
            {{{";bnnz"}, {"життя"}, {"життя"}}}, layoutfix::Lang::En,
            punct_dicts, 3) == layoutfix::Lang::Uk);
  CHECK(layoutfix::unique_language_candidate(
            {{{"ghj,ktvs"}, {"проблемі"}, {"проблемы"}}}, layoutfix::Lang::En,
            punct_dicts, 3) == layoutfix::Lang::Uk);
  CHECK(layoutfix::unique_language_candidate(
            {{{"k.lbyf"}, {"людина"}, {"людина"}}}, layoutfix::Lang::En,
            punct_dicts, 3) == layoutfix::Lang::Uk);
  CHECK(layoutfix::unique_language_candidate(
            {{{"vj;yf"}, {"можна"}, {"можна"}}}, layoutfix::Lang::En,
            punct_dicts, 3) == layoutfix::Lang::Uk);
  CHECK(layoutfix::unique_language_candidate(
            {{{"[jxe"}, {"хочу"}, {"хочу"}}}, layoutfix::Lang::En, punct_dicts,
            3) == layoutfix::Lang::Uk);
  CHECK(layoutfix::unique_language_candidate(
            {{{"];f"}, {"їжа"}, {"ъжа"}}}, layoutfix::Lang::En, punct_dicts,
            3) == layoutfix::Lang::Uk);
  CHECK(layoutfix::unique_language_candidate(
            {{{"nfrj;"}, {"також"}, {"також"}}}, layoutfix::Lang::En,
            punct_dicts, 3) == layoutfix::Lang::Uk);
  CHECK(layoutfix::unique_language_candidate(
            {{{"le["}, {"дух"}, {"дух"}}}, layoutfix::Lang::En, punct_dicts,
            3) == layoutfix::Lang::Uk);
  CHECK(layoutfix::unique_language_candidate(
            {{{"g`znm"}, {"п'ять"}, {"пёять"}}}, layoutfix::Lang::En,
            punct_dicts, 3) == layoutfix::Lang::Uk);
  CHECK(layoutfix::unique_language_candidate(
            {{{"\"nj"}, {"Єто"}, {"Это"}}}, layoutfix::Lang::En, punct_dicts,
            3) == layoutfix::Lang::Ru);
  // Short multi-word context with punctuation-key letter (",j oj" -> "бо що").
  CHECK(layoutfix::unique_language_candidate(
            {{{",j", "oj"}, {"бо", "що"}, {"бо", "що"}}}, layoutfix::Lang::En,
            punct_dicts, 3) == layoutfix::Lang::Uk);
  // English word with trailing punctuation typed in Uk ("руддщб" -> "hello,").
  CHECK(layoutfix::unique_language_candidate(
            {{{"hello,"}, {"руддщб"}, {"руддщб"}}}, layoutfix::Lang::Uk,
            punct_dicts, 3) == layoutfix::Lang::En);
  // Short English core ("h,.") typed in Uk must NOT switch when core < min_length.
  CHECK(!layoutfix::unique_language_candidate(
            {{{"h,."}, {"рбю"}, {"рбю"}}}, layoutfix::Lang::Uk, punct_dicts,
            3));
  // Real English with natural punctuation MUST be kept (no false switch!).
  CHECK(!layoutfix::unique_language_candidate(
            {{{"he,"}, {"руб"}, {"руб"}}}, layoutfix::Lang::En, punct_dicts,
            3));
  CHECK(!layoutfix::unique_language_candidate(
            {{{"[hello]"}, {"хруддщї"}, {"хруддщъ"}}}, layoutfix::Lang::En,
            punct_dicts, 3));
  CHECK(!layoutfix::unique_language_candidate(
            {{{"\"hello\","}, {"ЄруддщЄб"}, {"ЭруддщЭб"}}}, layoutfix::Lang::En,
            punct_dicts, 3));
  CHECK(!layoutfix::unique_language_candidate(
            {{{"..."}, {"ююю"}, {"ююю"}}}, layoutfix::Lang::En, punct_dicts,
            3));

  // Ambiguous Uk+Ru word followed by a uniquely Uk or Ru word (2B, up to 4 words).
  layoutfix::Dictionary amb_en;
  layoutfix::Dictionary amb_uk;
  layoutfix::Dictionary amb_ru;
  CHECK(amb_en.load_text("2\nhello\nmy\n"));
  CHECK(amb_uk.load_text("6\nДля\nпросто\nтекст\nвиділеного\nпривіт\n"));
  CHECK(amb_ru.load_text("5\nДля\nпросто\nтекст\nпримера\nпривет\n"));
  const std::array<const layoutfix::Dictionary *, 3> amb_dicts{
      &amb_en, &amb_uk, &amb_ru};
  CHECK(layoutfix::is_ambiguous_word({"Lkz", "Для", "Для"}, layoutfix::Lang::En,
                                     amb_dicts));
  CHECK(!layoutfix::is_ambiguous_word({"hello", "руддщ", "руддщ"},
                                      layoutfix::Lang::En, amb_dicts));
  // Single ambiguous word stays unswitched.
  CHECK(!layoutfix::unique_language_candidate(
            {{{"Lkz"}, {"Для"}, {"Для"}}}, layoutfix::Lang::En, amb_dicts, 3));
  // "Lkz dblsktyjuj" -> Uk ("Для виділеного").
  CHECK(layoutfix::unique_language_candidate(
            {{{"Lkz", "dblsktyjuj"},
              {"Для", "виділеного"},
              {"Для", "выдыленого"}}},
            layoutfix::Lang::En, amb_dicts, 3) == layoutfix::Lang::Uk);
  // "Lkz ghbvthf" -> Ru ("Для примера").
  CHECK(layoutfix::unique_language_candidate(
            {{{"Lkz", "ghbvthf"}, {"Для", "примера"}, {"Для", "примера"}}},
            layoutfix::Lang::En, amb_dicts, 3) == layoutfix::Lang::Ru);
  // 4-word context: 3 shared Uk+Ru words + 1 unique Uk word.
  CHECK(layoutfix::unique_language_candidate(
            {{{"Lkz", "ghjcnj", "ntrcn", "ghbdsn"},
              {"Для", "просто", "текст", "привіт"},
              {"Для", "просто", "текст", "привыт"}}},
            layoutfix::Lang::En, amb_dicts, 3) == layoutfix::Lang::Uk);
  // Ambiguous word followed by valid English word must NOT switch.
  CHECK(!layoutfix::unique_language_candidate(
            {{{"Lkz", "hello"}, {"Для", "руддщ"}, {"Для", "руддщ"}}},
            layoutfix::Lang::En, amb_dicts, 3));

  // Manual ignore-words protect words from auto-fixing (single, punctuation,
  // multi-word context, case-insensitive).
  const auto ignore_set =
      layoutfix::build_ignore_set({"ghbdsn", "Lkz", "Діма"});
  CHECK(layoutfix::is_ignored_word("GHBDSN", ignore_set));
  CHECK(layoutfix::is_ignored_word("ghbdsn,", ignore_set));
  CHECK(layoutfix::is_ignored_word("[ghbdsn]", ignore_set));
  CHECK(layoutfix::is_ignored_word("діма.", ignore_set));
  CHECK(!layoutfix::is_ignored_word("hello", ignore_set));
  CHECK(!layoutfix::unique_language_candidate(
            {{{"ghbdsn"}, {"привіт"}, {"привыт"}}}, layoutfix::Lang::En,
            amb_dicts, 3, &ignore_set));
  CHECK(!layoutfix::is_ambiguous_word({"Lkz", "Для", "Для"},
                                      layoutfix::Lang::En, amb_dicts,
                                      &ignore_set));
  CHECK(!layoutfix::unique_language_candidate(
            {{{"Lkz", "dblsktyjuj"},
              {"Для", "виділеного"},
              {"Для", "выдыленого"}}},
            layoutfix::Lang::En, amb_dicts, 3, &ignore_set));

  // Force-convert last word (short, proper noun, ambiguous, and language cycling).
  const std::array<bool, 3> force_all{true, true, true};
  const std::array<bool, 3> force_en_uk{true, true, false};
  // Dictionary picks unique Russian match ("ghbdtn" -> "привет").
  CHECK(layoutfix::choose_forced_language(
            {"ghbdtn", "привет", "привет"}, layoutfix::Lang::En,
            {&english, &ukrainian, &russian}, force_all,
            {true, false, false}) == layoutfix::Lang::Ru);
  // Short word ("oj" -> "що" in Uk, not in Ru).
  CHECK(layoutfix::choose_forced_language(
            {"oj", "що", "що"}, layoutfix::Lang::En,
            {&english, &ukrainian, &russian}, force_all,
            {true, false, false}) == layoutfix::Lang::Uk);
  // Unknown proper noun ("Bujhm" -> "Ігорь") toggles En <-> Uk with 2 languages.
  CHECK(layoutfix::choose_forced_language(
            {"Bujhm", "Ігорь", "Ыгорь"}, layoutfix::Lang::En,
            {&english, &ukrainian, nullptr}, force_en_uk,
            {true, false, false}) == layoutfix::Lang::Uk);
  CHECK(layoutfix::choose_forced_language(
            {"Bujhm", "Ігорь", "Ыгорь"}, layoutfix::Lang::Uk,
            {&english, &ukrainian, nullptr}, force_en_uk,
            {true, true, false}) == layoutfix::Lang::En);
  // Ambiguous word valid in both Uk and Ru cycles En -> Uk -> Ru -> En.
  CHECK(layoutfix::choose_forced_language(
            {"ye", "ну", "ну"}, layoutfix::Lang::En,
            {&english, &ukrainian, &russian}, force_all,
            {true, false, false}) == layoutfix::Lang::Uk);
  CHECK(layoutfix::choose_forced_language(
            {"ye", "ну", "ну"}, layoutfix::Lang::Uk,
            {&english, &ukrainian, &russian}, force_all,
            {true, true, false}) == layoutfix::Lang::Ru);
  CHECK(layoutfix::choose_forced_language(
            {"ye", "ну", "ну"}, layoutfix::Lang::Ru,
            {&english, &ukrainian, &russian}, force_all,
            {true, true, true}) == layoutfix::Lang::En);

  layoutfix::WordBuffer buffer;
  buffer.set_group(0);
  buffer.push_word(42, false, layoutfix::Lang::En, {"g", "п", "г"});
  buffer.push_word(43, true, layoutfix::Lang::En, {"H", "Р", "Г"});
  CHECK(buffer.text(layoutfix::Lang::En) == "gH");
  CHECK(buffer.text(layoutfix::Lang::Uk) == "пР");
  CHECK(buffer.text(layoutfix::Lang::Ru) == "гГ");
  buffer.pop();
  CHECK(buffer.text(layoutfix::Lang::En) == "g");
  CHECK(buffer.text(layoutfix::Lang::Uk) == "п");
  buffer.push_boundary(65, false, " ");
  CHECK(buffer.strokes().size() == 2);
  CHECK(!buffer.strokes().back().word);

  // build_replacement_text for atomic multi-word clipboard paste.
  layoutfix::WordBuffer multi_w1;
  multi_w1.set_group(0);
  multi_w1.push_word(46, true, layoutfix::Lang::En, {"L", "Д", "Д"});
  multi_w1.push_word(45, false, layoutfix::Lang::En, {"k", "л", "л"});
  multi_w1.push_word(52, false, layoutfix::Lang::En, {"z", "я", "я"});
  multi_w1.push_boundary(65, false, " ");
  layoutfix::WordBuffer multi_w2;
  multi_w2.set_group(0);
  multi_w2.push_word(42, false, layoutfix::Lang::En, {"g", "п", "п"});
  multi_w2.push_word(43, false, layoutfix::Lang::En, {"h", "р", "р"});
  multi_w2.push_word(56, false, layoutfix::Lang::En, {"b", "и", "и"});
  multi_w2.push_boundary(36, false, "\r");
  std::vector<layoutfix::Stroke> multi_strokes = multi_w1.strokes();
  multi_strokes.insert(multi_strokes.end(), multi_w2.strokes().begin(),
                       multi_w2.strokes().end());
  CHECK(layoutfix::build_replacement_text(multi_strokes, layoutfix::Lang::Ru) ==
        "Для при\n");
  CHECK(layoutfix::build_replacement_text(multi_strokes, layoutfix::Lang::En) ==
        "Lkz ghb\n");

  std::vector<layoutfix::EchoKey> echoes{
      {22, true}, {22, false}, {38, true}, {38, false}};
  CHECK(layoutfix::consume_echo(echoes, false, true, 22) ==
        layoutfix::EchoConsume::Matched);
  CHECK(layoutfix::consume_echo(echoes, true, true, 1) ==
        layoutfix::EchoConsume::Ignored);
  CHECK(echoes.size() == 3);
  CHECK(layoutfix::consume_echo(echoes, false, true, 99) ==
        layoutfix::EchoConsume::Ignored);
  CHECK(echoes.front().keycode == 22);
  CHECK(layoutfix::consume_echo(echoes, false, false, 22) ==
        layoutfix::EchoConsume::Matched);
  CHECK(layoutfix::consume_echo(echoes, false, true, 38) ==
        layoutfix::EchoConsume::Matched);
  CHECK(layoutfix::consume_echo(echoes, false, false, 38) ==
        layoutfix::EchoConsume::Done);
  CHECK(echoes.empty());

  // Optional pre-release skipped cleanly when press arrives directly.
  std::vector<layoutfix::EchoKey> opt_echoes{
      {65, false, true}, {65, true, false}, {65, false, false}};
  CHECK(layoutfix::consume_echo(opt_echoes, false, true, 99) ==
        layoutfix::EchoConsume::Ignored);
  CHECK(opt_echoes.size() == 3);
  CHECK(layoutfix::consume_echo(opt_echoes, false, true, 65) ==
        layoutfix::EchoConsume::Matched);
  CHECK(opt_echoes.size() == 1);
  CHECK(layoutfix::consume_echo(opt_echoes, false, false, 65) ==
        layoutfix::EchoConsume::Done);
  CHECK(opt_echoes.empty());

  // Optional pre-release matched normally when emitted by Xorg.
  std::vector<layoutfix::EchoKey> opt_echoes_hit{
      {65, false, true}, {65, true, false}, {65, false, false}};
  CHECK(layoutfix::consume_echo(opt_echoes_hit, false, false, 65) ==
        layoutfix::EchoConsume::Matched);
  CHECK(opt_echoes_hit.size() == 2);
  CHECK(layoutfix::consume_echo(opt_echoes_hit, false, true, 65) ==
        layoutfix::EchoConsume::Matched);
  CHECK(layoutfix::consume_echo(opt_echoes_hit, false, false, 65) ==
        layoutfix::EchoConsume::Done);

  // Selection conversion tests.
  layoutfix::LayoutMaps maps;
  auto add_key = [&](const char *en, const char *uk, const char *ru) {
    using layoutfix::Lang;
    using layoutfix::lang_index;
    const std::array<const char *, 3> vals{en, uk, ru};
    for (std::size_t a = 0; a < 3; ++a) {
      for (std::size_t b = 0; b < 3; ++b) {
        if (a != b) {
          maps.tables[a][b].emplace(vals[a], vals[b]);
        }
      }
    }
  };
  add_key("g", "п", "п");
  add_key("h", "р", "р");
  add_key("b", "и", "и");
  add_key("d", "в", "в");
  add_key("s", "і", "ы");
  add_key("S", "І", "Ы");
  add_key("n", "т", "т");
  add_key("t", "е", "е");
  add_key("Y", "Н", "Н");
  add_key("y", "н", "н");
  add_key("e", "у", "у");
  add_key("o", "щ", "щ");
  add_key("j", "о", "о");
  add_key("v", "м", "м");
  add_key("k", "л", "л");
  add_key("l", "д", "д");
  add_key("r", "к", "к");
  add_key("w", "ц", "ц");
  add_key(",", "б", "б");
  add_key(".", "ю", "ю");
  add_key("/", ".", ".");
  add_key("?", ",", ",");

  layoutfix::Dictionary sel_en;
  layoutfix::Dictionary sel_uk;
  layoutfix::Dictionary sel_ru;
  CHECK(sel_en.load_text("3\nhello\nworld\nyes\n"));
  CHECK(sel_uk.load_text("6\nпривіт\nну\nі\nщо\nмир\n"));
  CHECK(sel_ru.load_text("3\nпривет\nмир\nну\n"));
  const std::array<const layoutfix::Dictionary *, 3> sel_dicts{
      &sel_en, &sel_uk, &sel_ru};
  const std::array<bool, 3> all_active{true, true, true};
  const std::array<bool, 3> en_uk_active{true, true, false};

  const auto sel_uk_word = layoutfix::convert_selection(
      "ghbdsn", maps, sel_dicts, all_active, layoutfix::Lang::En);
  CHECK(sel_uk_word.changed && sel_uk_word.target == layoutfix::Lang::Uk &&
        sel_uk_word.text == "привіт");

  const auto sel_ru_word = layoutfix::convert_selection(
      "ghbdtn", maps, sel_dicts, all_active, layoutfix::Lang::En);
  CHECK(sel_ru_word.changed && sel_ru_word.target == layoutfix::Lang::Ru &&
        sel_ru_word.text == "привет");

  const auto sel_multiline = layoutfix::convert_selection(
      "Ye s oj?\nS oj/", maps, sel_dicts, all_active, layoutfix::Lang::En);
  CHECK(sel_multiline.changed && sel_multiline.target == layoutfix::Lang::Uk &&
        sel_multiline.text == "Ну і що,\nІ що.");

  const auto sel_to_en = layoutfix::convert_selection(
      "руддщб цщкдвю", maps, sel_dicts, all_active, layoutfix::Lang::Uk);
  CHECK(sel_to_en.changed && sel_to_en.target == layoutfix::Lang::En &&
        sel_to_en.text == "hello, world.");

  const auto sel_ru_to_uk = layoutfix::convert_selection(
      "привыт", maps, sel_dicts, all_active, layoutfix::Lang::Ru);
  CHECK(sel_ru_to_uk.changed && sel_ru_to_uk.target == layoutfix::Lang::Uk &&
        sel_ru_to_uk.text == "привіт");

  const auto sel_tie_ru = layoutfix::convert_selection(
      "vbh", maps, sel_dicts, all_active, layoutfix::Lang::Ru);
  CHECK(sel_tie_ru.changed && sel_tie_ru.target == layoutfix::Lang::Ru &&
        sel_tie_ru.text == "мир");

  const auto sel_tie_uk = layoutfix::convert_selection(
      "vbh", maps, sel_dicts, all_active, layoutfix::Lang::En);
  CHECK(sel_tie_uk.changed && sel_tie_uk.target == layoutfix::Lang::Uk &&
        sel_tie_uk.text == "мир");

  const auto sel_no_ru = layoutfix::convert_selection(
      "ghbdtn", maps, {&sel_en, &sel_uk, nullptr}, en_uk_active,
      layoutfix::Lang::En);
  CHECK(sel_no_ru.changed && sel_no_ru.target == layoutfix::Lang::Uk &&
        sel_no_ru.text == "привет");

  const auto sel_unchanged = layoutfix::convert_selection(
      "12345", maps, sel_dicts, all_active, layoutfix::Lang::En);
  CHECK(!sel_unchanged.changed);
  // Правильне українське слово не перекладається на російське лише через спільні клавіші.
  const auto sel_keep_uk = layoutfix::convert_selection(
      "привіт", maps, sel_dicts, all_active, layoutfix::Lang::Uk, true);
  CHECK(!sel_keep_uk.changed);
  // Нульовий рахунок без явного прохання лишає текст як є.
  const auto sel_no_force = layoutfix::convert_selection(
      "ghbdtn", maps, {&sel_en, &sel_uk, nullptr}, en_uk_active,
      layoutfix::Lang::En, false);
  CHECK(!sel_no_force.changed);

  // Copy-line-on-empty-selection guard (VS Code, JetBrains, Sublime, Kate).
  CHECK(layoutfix::is_copy_line_editor("Code", "code"));
  CHECK(layoutfix::is_copy_line_editor("jetbrains-clion", "clion"));
  CHECK(layoutfix::is_copy_line_editor("Sublime_text", "sublime_text"));
  CHECK(layoutfix::is_copy_line_editor("antigravity", "Antigravity"));
  CHECK(!layoutfix::is_copy_line_editor("firefox", "Navigator"));
  CHECK(layoutfix::has_vscode_empty_selection_marker(
      R"({"version":1,"isFromEmptySelection":true,"multicursorText":null})"));
  // UTF-16LE encoded VS Code Pickle payload.
  std::string utf16_marker;
  for (char ch : std::string_view("\"isFromEmptySelection\":true")) {
    utf16_marker.push_back(ch);
    utf16_marker.push_back('\0');
  }
  CHECK(layoutfix::has_vscode_empty_selection_marker(utf16_marker));
  CHECK(!layoutfix::has_vscode_empty_selection_marker(
      R"({"version":1,"isFromEmptySelection":false})"));
  // Empty-selection line copy in VS Code (even without trailing newline on last line).
  CHECK(layoutfix::looks_like_empty_line_copy(
      "int x = 1;", "", true, utf16_marker));
  // Empty-selection line copy in JetBrains/Sublime (single line + '\n', PRIMARY mismatch).
  CHECK(layoutfix::looks_like_empty_line_copy(
      "    ghbdsn cdsn\n", "older selection", true, ""));
  // Real partial selection in JetBrains/Sublime (no trailing '\n') -> allowed.
  CHECK(!layoutfix::looks_like_empty_line_copy(
      "ghbdsn cdsn", "", true, ""));
  // Real multi-line selection in JetBrains/Sublime -> allowed.
  CHECK(!layoutfix::looks_like_empty_line_copy(
      "ghbdsn\ncdsn\n", "", true, ""));
  // Explicit triple-click full-line selection (PRIMARY == selected) -> allowed.
  CHECK(!layoutfix::looks_like_empty_line_copy(
      "ghbdsn cdsn\n", "ghbdsn cdsn\n", true, ""));
  // Non-copy-line app (Firefox/LibreOffice) -> allowed.
  CHECK(!layoutfix::looks_like_empty_line_copy(
      "ghbdsn cdsn\n", "", false, ""));
  // VS Code explicitly says the selection is real, even if the line ends with \n.
  CHECK(!layoutfix::looks_like_empty_line_copy(
      "ghbdsn cdsn\n", "older selection", true,
      R"({"version":1,"isFromEmptySelection":false})"));

  using layoutfix::Lang;
  using layoutfix::UndoRestore;
  CHECK(layoutfix::choose_undo_restore(true, Lang::En, Lang::En, Lang::Uk) ==
        UndoRestore::Original);
  CHECK(layoutfix::choose_undo_restore(false, Lang::En, Lang::En, Lang::Uk) ==
        UndoRestore::Original);
  CHECK(layoutfix::choose_undo_restore(false, Lang::Uk, Lang::En, Lang::Uk) ==
        UndoRestore::Shown);
  CHECK(layoutfix::choose_undo_restore(false, Lang::Ru, Lang::Uk, Lang::Ru) ==
        UndoRestore::Shown);
  CHECK(layoutfix::choose_undo_restore(false, Lang::Other, Lang::En,
                                       Lang::Uk) == UndoRestore::Drop);

  using layoutfix::TrayMode;
  CHECK(layoutfix::choose_tray_mode(false, true) == TrayMode::Active);
  CHECK(layoutfix::choose_tray_mode(true, true) == TrayMode::Paused);
  CHECK(layoutfix::choose_tray_mode(false, false) == TrayMode::Desynced);
  CHECK(layoutfix::choose_tray_mode(true, false) == TrayMode::PausedDesynced);

  const std::filesystem::path candidates[] = {
      "/usr/share/hunspell/uk_UA.dic",
      "data/dicts/uk_UA.dic",
      "../data/dicts/uk_UA.dic",
      "../../data/dicts/uk_UA.dic",
  };
  std::string uk_path;
  for (const auto &path : candidates) {
    if (std::filesystem::is_regular_file(path)) {
      uk_path = path.string();
      break;
    }
  }
  if (!uk_path.empty()) {
    layoutfix::Dictionary real;
    CHECK(real.load_file(uk_path));
    CHECK(real.contains("привіт"));
    CHECK(real.contains("ПРИВІТ"));

    if (real.has_morphology()) {
      layoutfix::Dictionary real_english;
      CHECK(real_english.load_file("/usr/share/hunspell/en_US.dic"));
      CHECK(real.contains("привіту"));
      CHECK(!real_english.contains("ghbdne"));
      CHECK(layoutfix::decide("ghbdne", "привіту", real_english, real, 3) ==
            Decision::Switch);
      CHECK(real.contains("ну"));
      CHECK(real.contains("і"));
      CHECK(real.contains("що"));
      CHECK(real_english.contains("ye"));
      CHECK(real_english.contains("s"));
      CHECK(!real_english.contains("oj"));
      CHECK(layoutfix::decide_context({"ye", "s"}, {"ну", "і"},
                                      real_english, real, 3) == Decision::Keep);
      CHECK(layoutfix::decide_context({"ye", "s", "oj"}, {"ну", "і", "що"},
                                      real_english, real, 3) == Decision::Switch);
      const std::array<const layoutfix::Dictionary *, 3> real_en_uk{
          &real_english, &real, nullptr};
      CHECK(layoutfix::unique_language_candidate(
                {{{"[ks,"}, {"хліб"}, {""}}}, layoutfix::Lang::En, real_en_uk,
                3) == layoutfix::Lang::Uk);
      CHECK(!layoutfix::unique_language_candidate(
                {{{"he,"}, {"руб"}, {""}}}, layoutfix::Lang::En, real_en_uk,
                3));
      CHECK(layoutfix::unique_language_candidate(
                {{{"ghj,ktvs"}, {"проблемі"}, {""}}}, layoutfix::Lang::En,
                real_en_uk, 3) == layoutfix::Lang::Uk);
      CHECK(layoutfix::unique_language_candidate(
                {{{";bnnz"}, {"життя"}, {""}}}, layoutfix::Lang::En,
                real_en_uk, 3) == layoutfix::Lang::Uk);
      CHECK(layoutfix::unique_language_candidate(
                {{{"k.lbyf"}, {"людина"}, {""}}}, layoutfix::Lang::En,
                real_en_uk, 3) == layoutfix::Lang::Uk);
      CHECK(layoutfix::unique_language_candidate(
                {{{"vj;yf"}, {"можна"}, {""}}}, layoutfix::Lang::En,
                real_en_uk, 3) == layoutfix::Lang::Uk);
      CHECK(layoutfix::unique_language_candidate(
                {{{"[jxe"}, {"хочу"}, {""}}}, layoutfix::Lang::En,
                real_en_uk, 3) == layoutfix::Lang::Uk);
      CHECK(layoutfix::unique_language_candidate(
                {{{",j", "oj"}, {"бо", "що"}, {"", ""}}}, layoutfix::Lang::En,
                real_en_uk, 3) == layoutfix::Lang::Uk);
      CHECK(layoutfix::unique_language_candidate(
                {{{"hello,"}, {"руддщб"}, {""}}}, layoutfix::Lang::Uk,
                real_en_uk, 3) == layoutfix::Lang::En);
    }
  } else {
    std::cerr << "FAIL немає uk_UA.dic для перевірки справжнього словника\n";
    ++failures;
  }

      if (std::filesystem::is_regular_file("/usr/share/hunspell/ru_RU.dic")) {
            layoutfix::Dictionary real_russian;
            CHECK(real_russian.load_file("/usr/share/hunspell/ru_RU.dic"));
            CHECK(real_russian.has_morphology());
            CHECK(real_russian.contains("привет"));
            CHECK(!real_russian.contains("ghbdtn"));
            const std::array<const layoutfix::Dictionary*, 3> real_dictionaries{
                        &english, &ukrainian, &real_russian};
            const std::array<std::vector<std::string>, 3> russian_forms{{
                        {"ghbdtn"}, {"qzxjv"}, {"привет"}}};
            CHECK(layoutfix::unique_language_candidate(russian_forms,
                                                                                                                                                layoutfix::Lang::En,
                                                                                                                                                real_dictionaries, 3) ==
                              layoutfix::Lang::Ru);
      }

  layoutfix::GroupLetters latin;
  latin.latin_letters = 26;
  CHECK(layoutfix::classify_group_letters(latin) == layoutfix::Lang::En);
  layoutfix::GroupLetters ukrainian_letters;
  ukrainian_letters.uk_specific = 4;
  ukrainian_letters.other_cyrillic = 30;
  CHECK(layoutfix::classify_group_letters(ukrainian_letters) ==
        layoutfix::Lang::Uk);
  layoutfix::GroupLetters russian_letters;
  russian_letters.ru_specific = 4;
  russian_letters.other_cyrillic = 30;
  CHECK(layoutfix::classify_group_letters(russian_letters) ==
        layoutfix::Lang::Ru);
  layoutfix::GroupLetters phonetic;
  layoutfix::note_group_letter(phonetic, "с");
  layoutfix::note_group_letter(phonetic, "і");
  layoutfix::note_group_letter(phonetic, "ї");
  layoutfix::note_group_letter(phonetic, "є");
  CHECK(layoutfix::classify_group_letters(phonetic) == layoutfix::Lang::Uk);
  layoutfix::GroupLetters empty_group;
  CHECK(layoutfix::classify_group_letters(empty_group) == layoutfix::Lang::Other);
  // Українські літери перемагають, навіть якщо поряд є латинські (група не перша).
  layoutfix::GroupLetters ua_not_first = ukrainian_letters;
  ua_not_first.latin_letters = 26;
  CHECK(layoutfix::classify_group_letters(ua_not_first) == layoutfix::Lang::Uk);

  CHECK(layoutfix::resolve_switch({true, true, false, false}) ==
        layoutfix::SwitchOutcome::Applied);
  CHECK(layoutfix::resolve_switch({false, false, false, false}) ==
        layoutfix::SwitchOutcome::Restored);
  CHECK(layoutfix::resolve_switch({true, false, true, true}) ==
        layoutfix::SwitchOutcome::Restored);
  CHECK(layoutfix::resolve_switch({true, false, true, false}) ==
        layoutfix::SwitchOutcome::Desynced);
  CHECK(layoutfix::resolve_switch({true, false, false, false}) ==
        layoutfix::SwitchOutcome::Desynced);

  layoutfix::Undo undo;
  undo.arm_keys({}, {}, 1, layoutfix::Lang::En, layoutfix::Lang::Uk);
  undo.base_delete = 6;
  undo.on_typed_key();
  undo.on_typed_key();
  CHECK(undo.valid);
  CHECK(undo.delete_count() == 8);
  undo.on_backspace();
  CHECK(undo.valid);
  CHECK(undo.delete_count() == 7);
  undo.on_word_committed();
  CHECK(!undo.valid);
  undo.arm_paste("ghbdsn", 6, 0, layoutfix::Lang::Uk, layoutfix::Lang::En);
  CHECK(undo.paste);
  CHECK(undo.on_backspace());
  CHECK(!undo.valid);

  layoutfix::WordBuffer live;
  live.set_group(1);
  live.push_word(10, false, layoutfix::Lang::Uk, {"y", "н", "н"});
  live.push_word(11, false, layoutfix::Lang::Uk, {"e", "у", "у"});
  live.push_boundary(12, false, " ");
  std::vector<layoutfix::PendingWord> pending;
  layoutfix::remember_pending(
      pending, layoutfix::PendingWord{{live.text(layoutfix::Lang::En),
                                       live.text(layoutfix::Lang::Uk),
                                       live.text(layoutfix::Lang::Ru)},
                                      live.strokes(),
                                      layoutfix::Lang::Uk});
  live.clear();
  live.set_group(1);
  live.push_word(13, false, layoutfix::Lang::Uk, {"s", "і", "ы"});
  live.push_boundary(14, false, " ");
  layoutfix::remember_pending(
      pending, layoutfix::PendingWord{{live.text(layoutfix::Lang::En),
                                       live.text(layoutfix::Lang::Uk),
                                       live.text(layoutfix::Lang::Ru)},
                                      live.strokes(),
                                      layoutfix::Lang::Uk});
  live.clear();
  auto kept = layoutfix::restore_pending_on_backspace(pending, true, true);
  CHECK(!kept.restored);
  CHECK(pending.size() == 2);
  auto restored = layoutfix::restore_pending_on_backspace(pending, false, true);
  CHECK(restored.restored);
  CHECK(pending.size() == 1);
  CHECK(restored.word.lang == layoutfix::Lang::Uk);
  CHECK(restored.word.forms[1] == "і");
  CHECK(restored.word.strokes.size() == 1);
  CHECK(restored.word.strokes.back().word);
  live.assign(1, restored.word.lang, restored.word.forms,
              std::move(restored.word.strokes));
  CHECK(live.pop());
  CHECK(live.empty());
  auto older = layoutfix::restore_pending_on_backspace(pending, false, true);
  CHECK(older.restored);
  CHECK(older.word.forms[1] == "ну");
  CHECK(older.word.strokes.size() == 2);
  CHECK(pending.empty());

  std::vector<layoutfix::PendingWord> capped;
  for (int i = 0; i < 5; ++i) {
    layoutfix::PendingWord word;
    word.forms[1] = std::to_string(i);
    word.lang = layoutfix::Lang::Uk;
    word.strokes.push_back(
        layoutfix::Stroke{1, false, true, word.forms[1], {}, word.forms});
    layoutfix::remember_pending(capped, std::move(word));
  }
  CHECK(capped.size() == 3);
  CHECK(capped.front().forms[1] == "2");
  CHECK(capped.back().forms[1] == "4");

  CHECK(layoutfix::is_password_widget(layoutfix::WidgetRole::Password, ""));
  CHECK(layoutfix::is_password_widget(layoutfix::WidgetRole::Entry, "password"));
  CHECK(layoutfix::is_password_widget(layoutfix::WidgetRole::Text, "Password"));
  CHECK(!layoutfix::is_password_widget(layoutfix::WidgetRole::Entry, ""));
  CHECK(!layoutfix::is_password_widget(layoutfix::WidgetRole::Entry, "text"));
  CHECK(!layoutfix::is_password_widget(layoutfix::WidgetRole::Other, "pass"));
  layoutfix::FocusSnapshot password;
  password.role_known = true;
  password.role = layoutfix::WidgetRole::Password;
  CHECK(layoutfix::focus_snapshot_is_password(password));
  CHECK(layoutfix::focus_loss_ends_password(password));
  layoutfix::FocusSnapshot entry_password;
  entry_password.role_known = true;
  entry_password.role = layoutfix::WidgetRole::Entry;
  entry_password.attribute_known = true;
  entry_password.text_input_type = "password";
  CHECK(layoutfix::focus_snapshot_is_password(entry_password));
  layoutfix::FocusSnapshot unknown;
  CHECK(!layoutfix::focus_snapshot_is_password(unknown));
  CHECK(!layoutfix::focus_loss_ends_password(unknown));
  layoutfix::FocusSnapshot gone;
  gone.defunct = true;
  gone.role_known = true;
  gone.role = layoutfix::WidgetRole::Password;
  CHECK(!layoutfix::focus_snapshot_is_password(gone));
  CHECK(layoutfix::focus_loss_ends_password(gone));
  layoutfix::FocusSnapshot plain;
  plain.role_known = true;
  plain.role = layoutfix::WidgetRole::Entry;
  plain.attribute_known = true;
  plain.text_input_type = "text";
  CHECK(!layoutfix::focus_snapshot_is_password(plain));
  CHECK(!layoutfix::focus_loss_ends_password(plain));

  const auto log_dir = std::filesystem::temp_directory_path() /
                       ("layout-fix-log-test-" +
                        std::to_string(std::chrono::steady_clock::now()
                                           .time_since_epoch()
                                           .count()));
  const auto log_path = (log_dir / "layout-fix.log").string();
  CHECK(layoutfix::log::open_file(log_path, 120));
  layoutfix::log::info("ROTATE-AAA");
  layoutfix::log::error("ROTATE-ERR");
  layoutfix::log::set_debug_keys(true);
  layoutfix::log::keys("SECRET-KEYS");
  layoutfix::log::set_debug_keys(false);
  layoutfix::log::info("ROTATE-BBB");
  const std::string logged = layoutfix::log::read_text(log_path);
  layoutfix::log::close_file();
  CHECK(logged.find("layout-fix: ROTATE-AAA") != std::string::npos);
  CHECK(logged.find("layout-fix: помилка: ROTATE-ERR") != std::string::npos);
  CHECK(logged.find("layout-fix: ROTATE-BBB") != std::string::npos);
  CHECK(logged.find("ROTATE-AAA") < logged.find("ROTATE-BBB"));
  CHECK(logged.find("SECRET-KEYS") == std::string::npos);
  CHECK(std::filesystem::is_regular_file(log_path + ".1"));
  CHECK(layoutfix::log::default_path().find("layout-fix.log") != std::string::npos);
  std::error_code log_removed;
  std::filesystem::remove_all(log_dir, log_removed);

  if (failures != 0) {
    std::cerr << failures << " перевірок не пройшло\n";
    return 1;
  }
  std::cout << "ok\n";
  return 0;
}
