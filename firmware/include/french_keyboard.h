#pragma once

#include <stdint.h>
#include <string.h>

// The French keyboard: the Canadian French CSA layout (xkb "ca(multix)") on the Cardputer's
// English keycaps. Pure data and lookups, no hardware, so a host test can prove every character
// in kRequired is reachable (firmware/tools/french-keyboard-test.cpp), and the KEYS screen is
// generated from this table rather than written by hand.
//
// Levels: the key alone, Shift (or Ctrl, which the M5 keyboard treats as Shift), Alt (CSA's
// AltGr), and Alt+Shift. The CSA key between left Shift and Z (ù) is missing here, so ù sits on
// Alt+U. A dead key (the CSA [ key: ^, ¨ and `) waits for the next letter; Space after it, or the
// same dead key again, types the accent itself.
namespace frkb {

enum class Dead : uint8_t { None, Circumflex, Diaeresis, Grave };

struct Level {
  const char *text;  // UTF-8, or nullptr: nothing.
  Dead dead;
};

struct Key {
  uint8_t column, row;  // The M5 keyboard's position (row 0 is the number row).
  char printed, printedShift;  // What the keycap shows, for the KEYS screen.
  Level plain, shift, alt, altShift;
};

#define FRKB_T(text) Level{text, Dead::None}
#define FRKB_D(dead) Level{nullptr, Dead::dead}
#define FRKB_N Level{nullptr, Dead::None}

// Every key that types. Letters and digits keep their keycaps; Shift gives the capital.
constexpr Key kKeys[] = {
    {0, 0, '`', '~', FRKB_T("/"), FRKB_T("\\"), FRKB_T("|"), FRKB_N},
    {1, 0, '1', '!', FRKB_T("1"), FRKB_T("!"), FRKB_N, FRKB_N},
    {2, 0, '2', '@', FRKB_T("2"), FRKB_T("@"), FRKB_N, FRKB_N},
    {3, 0, '3', '#', FRKB_T("3"), FRKB_T("#"), FRKB_N, FRKB_N},
    {4, 0, '4', '$', FRKB_T("4"), FRKB_T("$"), FRKB_N, FRKB_N},
    {5, 0, '5', '%', FRKB_T("5"), FRKB_T("%"), FRKB_N, FRKB_N},
    {6, 0, '6', '^', FRKB_T("6"), FRKB_T("?"), FRKB_N, FRKB_N},
    {7, 0, '7', '&', FRKB_T("7"), FRKB_T("&"), FRKB_T("{"), FRKB_N},
    {8, 0, '8', '*', FRKB_T("8"), FRKB_T("*"), FRKB_T("}"), FRKB_N},
    {9, 0, '9', '(', FRKB_T("9"), FRKB_T("("), FRKB_T("["), FRKB_N},
    {10, 0, '0', ')', FRKB_T("0"), FRKB_T(")"), FRKB_T("]"), FRKB_N},
    {11, 0, '-', '_', FRKB_T("-"), FRKB_T("_"), FRKB_N, FRKB_N},
    {12, 0, '=', '+', FRKB_T("="), FRKB_T("+"), FRKB_N, FRKB_N},
    {1, 1, 'q', 'Q', FRKB_T("q"), FRKB_T("Q"), FRKB_N, FRKB_N},
    {2, 1, 'w', 'W', FRKB_T("w"), FRKB_T("W"), FRKB_N, FRKB_N},
    {3, 1, 'e', 'E', FRKB_T("e"), FRKB_T("E"), FRKB_N, FRKB_N},
    {4, 1, 'r', 'R', FRKB_T("r"), FRKB_T("R"), FRKB_N, FRKB_N},
    {5, 1, 't', 'T', FRKB_T("t"), FRKB_T("T"), FRKB_N, FRKB_N},
    {6, 1, 'y', 'Y', FRKB_T("y"), FRKB_T("Y"), FRKB_N, FRKB_N},
    {7, 1, 'u', 'U', FRKB_T("u"), FRKB_T("U"), FRKB_T(u8"ù"), FRKB_T(u8"Ù")},
    {8, 1, 'i', 'I', FRKB_T("i"), FRKB_T("I"), FRKB_N, FRKB_N},
    {9, 1, 'o', 'O', FRKB_T("o"), FRKB_T("O"), FRKB_N, FRKB_N},
    {10, 1, 'p', 'P', FRKB_T("p"), FRKB_T("P"), FRKB_N, FRKB_N},
    {11, 1, '[', '{', FRKB_D(Circumflex), FRKB_D(Diaeresis), FRKB_D(Grave), FRKB_N},
    {12, 1, ']', '}', FRKB_T(u8"ç"), FRKB_T(u8"Ç"), FRKB_T("~"), FRKB_N},
    {13, 1, '\\', '|', FRKB_T(u8"à"), FRKB_T(u8"À"), FRKB_T("}"), FRKB_N},
    {2, 2, 'a', 'A', FRKB_T("a"), FRKB_T("A"), FRKB_N, FRKB_N},
    {3, 2, 's', 'S', FRKB_T("s"), FRKB_T("S"), FRKB_N, FRKB_N},
    {4, 2, 'd', 'D', FRKB_T("d"), FRKB_T("D"), FRKB_N, FRKB_N},
    {5, 2, 'f', 'F', FRKB_T("f"), FRKB_T("F"), FRKB_N, FRKB_N},
    {6, 2, 'g', 'G', FRKB_T("g"), FRKB_T("G"), FRKB_N, FRKB_N},
    {7, 2, 'h', 'H', FRKB_T("h"), FRKB_T("H"), FRKB_N, FRKB_N},
    {8, 2, 'j', 'J', FRKB_T("j"), FRKB_T("J"), FRKB_N, FRKB_N},
    {9, 2, 'k', 'K', FRKB_T("k"), FRKB_T("K"), FRKB_N, FRKB_N},
    {10, 2, 'l', 'L', FRKB_T("l"), FRKB_T("L"), FRKB_N, FRKB_N},
    {11, 2, ';', ':', FRKB_T(";"), FRKB_T(":"), FRKB_N, FRKB_N},
    {12, 2, '\'', '"', FRKB_T(u8"è"), FRKB_T(u8"È"), FRKB_T("{"), FRKB_N},
    {3, 3, 'z', 'Z', FRKB_T("z"), FRKB_T("Z"), FRKB_T(u8"«"), FRKB_N},
    {4, 3, 'x', 'X', FRKB_T("x"), FRKB_T("X"), FRKB_T(u8"»"), FRKB_N},
    {5, 3, 'c', 'C', FRKB_T("c"), FRKB_T("C"), FRKB_N, FRKB_N},
    {6, 3, 'v', 'V', FRKB_T("v"), FRKB_T("V"), FRKB_N, FRKB_N},
    {7, 3, 'b', 'B', FRKB_T("b"), FRKB_T("B"), FRKB_N, FRKB_N},
    {8, 3, 'n', 'N', FRKB_T("n"), FRKB_T("N"), FRKB_N, FRKB_N},
    {9, 3, 'm', 'M', FRKB_T("m"), FRKB_T("M"), FRKB_N, FRKB_N},
    {10, 3, ',', '<', FRKB_T(","), FRKB_T("'"), FRKB_T("<"), FRKB_N},
    {11, 3, '.', '>', FRKB_T("."), FRKB_T("\""), FRKB_T(">"), FRKB_N},
    {12, 3, '/', '?', FRKB_T(u8"é"), FRKB_T(u8"É"), FRKB_N, FRKB_N},
};
constexpr int kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

#undef FRKB_T
#undef FRKB_D
#undef FRKB_N

// What each dead key makes of a letter, and of Space.
struct Composition {
  Dead dead;
  char base;
  const char *text;
};
constexpr Composition kCompositions[] = {
    {Dead::Circumflex, 'a', u8"â"}, {Dead::Circumflex, 'e', u8"ê"}, {Dead::Circumflex, 'i', u8"î"},
    {Dead::Circumflex, 'o', u8"ô"}, {Dead::Circumflex, 'u', u8"û"}, {Dead::Circumflex, 'A', u8"Â"},
    {Dead::Circumflex, 'E', u8"Ê"}, {Dead::Circumflex, 'I', u8"Î"}, {Dead::Circumflex, 'O', u8"Ô"},
    {Dead::Circumflex, 'U', u8"Û"}, {Dead::Circumflex, ' ', "^"},
    {Dead::Diaeresis, 'e', u8"ë"}, {Dead::Diaeresis, 'i', u8"ï"}, {Dead::Diaeresis, 'u', u8"ü"},
    {Dead::Diaeresis, 'y', u8"ÿ"}, {Dead::Diaeresis, 'E', u8"Ë"}, {Dead::Diaeresis, 'I', u8"Ï"},
    {Dead::Diaeresis, 'U', u8"Ü"}, {Dead::Diaeresis, ' ', u8"¨"},
    {Dead::Grave, 'a', u8"à"}, {Dead::Grave, 'e', u8"è"}, {Dead::Grave, 'u', u8"ù"},
    {Dead::Grave, 'A', u8"À"}, {Dead::Grave, 'E', u8"È"}, {Dead::Grave, 'U', u8"Ù"},
    {Dead::Grave, ' ', "`"},
};
constexpr int kCompositionCount = sizeof(kCompositions) / sizeof(kCompositions[0]);

// The characters the kids must be able to type (Jay, 2026-11-10), checked by the host test along
// with every printable ASCII character, which Wi-Fi passwords and web addresses need.
constexpr const char *kRequired[] = {"?", "!", ".", ",", ";", ":", "'", "\"", "(", ")", "-", "_", "@", "#", "/",
                                     u8"é", u8"è", u8"ê", u8"à", u8"â", u8"ç", u8"ù", u8"û", u8"ô", u8"î",
                                     u8"ï", u8"ë", u8"«", u8"»"};
constexpr int kRequiredCount = sizeof(kRequired) / sizeof(kRequired[0]);

inline const Key *find(uint8_t column, uint8_t row) {
  for (const Key &key : kKeys)
    if (key.column == column && key.row == row) return &key;
  return nullptr;
}

inline Level level(const Key &key, bool shift, bool alt) {
  if (alt && shift) return key.altShift;
  if (alt) return key.alt;
  return shift ? key.shift : key.plain;
}

inline const char *deadText(Dead dead) {
  return dead == Dead::Circumflex ? "^" : dead == Dead::Diaeresis ? u8"¨" : dead == Dead::Grave ? "`" : "";
}

inline const char *compose(Dead dead, char base) {
  for (const Composition &entry : kCompositions)
    if (entry.dead == dead && entry.base == base) return entry.text;
  return nullptr;
}

// The typing state: one pending dead key at most.
struct Typist {
  Dead pending = Dead::None;

  // Calls out(text) for what one key press types: zero, one or two pieces of text.
  template <typename Out>
  void press(uint8_t column, uint8_t row, bool shift, bool alt, Out out) {
    if (column == 13 && row == 3) { space(out); return; }
    const Key *key = find(column, row);
    if (!key) return;
    const Level result = level(*key, shift, alt);
    if (result.dead != Dead::None) {
      if (pending == Dead::None) { pending = result.dead; return; }
      // The same dead key twice types its accent; a different one types the first and waits.
      const Dead first = pending;
      out(deadText(first));
      pending = first == result.dead ? Dead::None : result.dead;
      return;
    }
    if (!result.text) return;
    if (pending != Dead::None) {
      const Dead dead = pending;
      pending = Dead::None;
      const char *composed = result.text[1] == '\0' ? compose(dead, result.text[0]) : nullptr;
      if (composed) { out(composed); return; }
      out(deadText(dead));
    }
    out(result.text);
  }

  template <typename Out>
  void space(Out out) {
    if (pending != Dead::None) { const Dead dead = pending; pending = Dead::None; out(compose(dead, ' ')); return; }
    out(" ");
  }

  // Enter sends what is pending as its accent; Del drops it.
  template <typename Out>
  void flush(Out out) {
    if (pending == Dead::None) return;
    const Dead dead = pending;
    pending = Dead::None;
    out(deadText(dead));
  }
  bool cancel() {
    const bool had = pending != Dead::None;
    pending = Dead::None;
    return had;
  }
};

// One line of the KEYS screen: a character and the keys that type it, in ASCII for Font0. Generated
// from the tables above, never written by hand.
struct Recipe {
  const char *text;
  char keys[20];
};

inline void append(char *out, size_t size, const char *text) { strncat(out, text, size - strlen(out) - 1); }

// "SHIFT+6", "ALT+U", "[": the keycaps to hold and press, as printed on them.
inline void keysFor(char *out, size_t size, const Key &key, bool shift, bool alt) {
  const char printed[2] = {key.printed >= 'a' && key.printed <= 'z' ? static_cast<char>(key.printed - 32) : key.printed, '\0'};
  out[0] = '\0';
  if (alt) append(out, size, "ALT+");
  if (shift) append(out, size, "SHIFT+");
  append(out, size, printed);
}

// A key whose plain level is a letter: Shift on it gives the capital, which the screen covers in
// one line instead of listing each.
inline bool isLetterKey(const Key &key) {
  const char *text = key.plain.text;
  return text && ((text[0] >= 'a' && text[0] <= 'z') || static_cast<uint8_t>(text[0]) == 0xC3);
}

// Adds a recipe unless that character is listed already (the first, shortest way wins).
inline void addRecipe(Recipe *out, int capacity, int &count, const char *text, const char *keys) {
  for (int index = 0; index < count && index < capacity; index++)
    if (strcmp(out[index].text, text) == 0) return;
  if (count < capacity) {
    out[count].text = text;
    out[count].keys[0] = '\0';
    append(out[count].keys, sizeof(out[count].keys), keys);
  }
  count++;
}

// Every way of typing a character that is not on its keycap: each key at each level, then each
// dead key with each small letter and with Space (Shift makes the capital). Required characters
// come first, in kRequired's order, so the most needed are on the first page. ¨ alone is left
// out: Font0 can't draw it and no one types it. capacity must hold them all (the test checks).
inline int recipes(Recipe *out, int capacity) {
  Recipe all[64];
  int total = 0;
  char keys[20];
  for (int pass = 0; pass < 4; pass++) {
    const bool shift = pass == 1 || pass == 3, alt = pass >= 2;
    for (const Key &key : kKeys) {
      const Level result = level(key, shift, alt);
      if (!result.text || (shift && isLetterKey(key))) continue;
      const char keycap = alt ? 0 : shift ? key.printedShift : key.printed;
      if (!alt && result.text[1] == '\0' && result.text[0] == keycap) continue;
      keysFor(keys, sizeof(keys), key, shift, alt);
      addRecipe(all, 64, total, result.text, keys);
    }
  }
  for (const Composition &entry : kCompositions) {
    if ((entry.base >= 'A' && entry.base <= 'Z') || strcmp(entry.text, u8"¨") == 0) continue;
    for (const Key &key : kKeys)
      for (int pass = 0; pass < 4; pass++) {
        const bool shift = pass == 1 || pass == 3, alt = pass >= 2;
        if (level(key, shift, alt).dead != entry.dead) continue;
        keysFor(keys, sizeof(keys), key, shift, alt);
        append(keys, sizeof(keys), " PUIS ");
        const char base[2] = {static_cast<char>(entry.base - 32), '\0'};
        append(keys, sizeof(keys), entry.base == ' ' ? "ESPACE" : base);
        addRecipe(all, 64, total, entry.text, keys);
      }
  }
  int count = 0;
  for (const char *wanted : kRequired)
    for (int index = 0; index < total && index < 64; index++)
      if (strcmp(all[index].text, wanted) == 0) addRecipe(out, capacity, count, all[index].text, all[index].keys);
  for (int index = 0; index < total && index < 64; index++) addRecipe(out, capacity, count, all[index].text, all[index].keys);
  return total > 64 ? total : count;
}

}  // namespace frkb
