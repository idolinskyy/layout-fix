#include "detect/detector.hpp"
#include "detect/dictionary.hpp"
#include "input/echoes.hpp"
#include "input/undo_restore.hpp"
#include "input/word_buffer.hpp"
#include "utf8.hpp"

#include <filesystem>
#include <iostream>
#include <string>

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
  CHECK(layoutfix::utf8::casefold("Привіт") == "привіт");
  CHECK(layoutfix::utf8::casefold("HELLO") == "hello");
  CHECK(layoutfix::utf8::casefold("ҐІЇЄ") == "ґіїє");
  CHECK(layoutfix::utf8::char_count("привіт") == 6);
  CHECK(layoutfix::utf8::char_count("hello") == 5);

  layoutfix::Dictionary english;
  layoutfix::Dictionary ukrainian;
  CHECK(english.load_text("3\nhello/SM\nyes\nab\n"));
      CHECK(ukrainian.load_text("4\nпривіт/1$\nщо\nну\nі\n"));
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

  layoutfix::WordBuffer buffer;
  buffer.set_group(0);
  buffer.push_word(42, false, "g", "п");
  buffer.push_word(43, true, "H", "Р");
  CHECK(buffer.current_text() == "gH");
  CHECK(buffer.other_text() == "пР");
  buffer.pop();
  CHECK(buffer.current_text() == "g");
  CHECK(buffer.other_text() == "п");
  buffer.push_boundary(65, false, " ");
  CHECK(buffer.strokes().size() == 2);
  CHECK(!buffer.strokes().back().word);

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

  using layoutfix::Lang;
  using layoutfix::UndoRestore;
  CHECK(layoutfix::choose_undo_restore(true, Lang::En, Lang::En, Lang::Uk) ==
        UndoRestore::Original);
  CHECK(layoutfix::choose_undo_restore(false, Lang::En, Lang::En, Lang::Uk) ==
        UndoRestore::Original);
  CHECK(layoutfix::choose_undo_restore(false, Lang::Uk, Lang::En, Lang::Uk) ==
        UndoRestore::Shown);
  CHECK(layoutfix::choose_undo_restore(false, Lang::Other, Lang::En,
                                       Lang::Uk) == UndoRestore::Drop);

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
    }
  } else {
    std::cerr << "FAIL немає uk_UA.dic для перевірки справжнього словника\n";
    ++failures;
  }

  if (failures != 0) {
    std::cerr << failures << " перевірок не пройшло\n";
    return 1;
  }
  std::cout << "ok\n";
  return 0;
}
