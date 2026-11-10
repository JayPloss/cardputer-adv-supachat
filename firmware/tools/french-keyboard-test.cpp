// Host test of the French keyboard: every required character, and every printable ASCII
// character, can be typed; dead keys compose and undo; the KEYS screen lists what it should.
//   g++ -std=c++11 -I firmware/include firmware/tools/french-keyboard-test.cpp -o /tmp/fr-kb && /tmp/fr-kb [--list]
#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>
#include "french_keyboard.h"

using namespace frkb;

struct Press { uint8_t column, row; bool shift, alt; };

std::string typeAll(Typist &typist, const std::vector<Press> &presses) {
  std::string text;
  for (const Press &press : presses) typist.press(press.column, press.row, press.shift, press.alt, [&](const char *piece) { text += piece; });
  return text;
}

int main(int argc, char **argv) {
  // Everything one or two presses can type: each key at each level, alone and after each dead key.
  std::set<std::string> reachable;
  std::vector<Press> all;
  for (const Key &key : kKeys)
    for (int pass = 0; pass < 4; pass++) all.push_back({key.column, key.row, pass == 1 || pass == 3, pass >= 2});
  all.push_back({13, 3, false, false});  // Space.
  for (const Press &first : all) {
    Typist alone; reachable.insert(typeAll(alone, {first}));
    for (const Press &second : all) {
      Typist typist; const std::string text = typeAll(typist, {first, second});
      reachable.insert(text);
    }
  }
  // Single characters reached, as a press or a dead-key pair, counting only results of one character.
  auto has = [&](const std::string &wanted) { return reachable.count(wanted) > 0; };
  int missing = 0;
  for (const char *wanted : kRequired) if (!has(wanted)) { std::printf("missing required: %s\n", wanted); missing++; }
  for (char character = 0x20; character < 0x7f; character++) {
    const std::string wanted(1, character);
    if (!has(wanted)) { std::printf("missing ASCII: '%s'\n", wanted.c_str()); missing++; }
  }
  for (const char *capital : {u8"É", u8"È", u8"À", u8"Ç", u8"Ê", u8"Ù"}) if (!has(capital)) { std::printf("missing capital: %s\n", capital); missing++; }
  assert(missing == 0);

  // The CSA positions the kids were told about: ? is Shift+6, é the / key, ç the ] key.
  { Typist t; assert(typeAll(t, {{6, 0, true, false}}) == "?"); }
  { Typist t; assert(typeAll(t, {{12, 3, false, false}}) == u8"é"); }
  { Typist t; assert(typeAll(t, {{12, 1, false, false}}) == u8"ç"); }
  { Typist t; assert(typeAll(t, {{10, 3, true, false}}) == "'"); }
  { Typist t; assert(typeAll(t, {{11, 3, true, false}}) == "\""); }
  // Dead keys: ^ then e is ê; ^ then x is ^x; ^ then Space is ^; ^ twice is ^; Del drops it.
  { Typist t; assert(typeAll(t, {{11, 1, false, false}}) == "" && t.pending == Dead::Circumflex); assert(typeAll(t, {{3, 1, false, false}}) == u8"ê"); }
  { Typist t; assert(typeAll(t, {{11, 1, false, false}, {4, 3, false, false}}) == "^x"); }
  { Typist t; assert(typeAll(t, {{11, 1, false, false}, {13, 3, false, false}}) == "^"); }
  { Typist t; assert(typeAll(t, {{11, 1, false, false}, {11, 1, false, false}}) == "^" && t.pending == Dead::None); }
  { Typist t; assert(typeAll(t, {{11, 1, true, false}, {8, 1, false, false}}) == u8"ï"); }
  { Typist t; assert(typeAll(t, {{11, 1, false, true}, {7, 1, false, false}}) == u8"ù"); }
  { Typist t; typeAll(t, {{11, 1, false, false}}); assert(t.cancel() && t.pending == Dead::None && !t.cancel()); }
  { Typist t; typeAll(t, {{11, 1, false, false}}); std::string flushed; t.flush([&](const char *p) { flushed += p; }); assert(flushed == "^"); }
  // Modifier and special keys type nothing.
  { Typist t; assert(typeAll(t, {{0, 2, false, false}, {1, 2, false, false}, {2, 3, false, false}, {13, 0, false, false}, {13, 2, false, false}, {0, 1, false, false}}) == ""); }

  // The KEYS screen: generated, and listing every required character that isn't on its keycap.
  Recipe list[64];
  const int count = recipes(list, 64);
  assert(count <= 64);
  std::set<std::string> listed;
  for (int index = 0; index < count; index++) listed.insert(list[index].text);
  for (const char *wanted : kRequired) {
    const bool onKeycap = strlen(wanted) == 1 && std::strchr("!.,;:()-_@#", wanted[0]);
    if (!onKeycap && !listed.count(wanted)) { std::printf("not on the KEYS screen: %s\n", wanted); assert(false); }
  }
  if (argc > 1 && std::strcmp(argv[1], "--list") == 0)
    for (int index = 0; index < count; index++) std::printf("%-4s %s\n", list[index].text, list[index].keys);
  std::printf("french keyboard ok: %zu reachable, %d on the KEYS screen\n", reachable.size(), count);
  return 0;
}
