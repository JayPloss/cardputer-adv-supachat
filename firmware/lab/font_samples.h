// The fonts worth considering on the Cardputer's 240x135 screen, and one screen of sample text for
// each. The native specimen (tools/font-specimen.cpp in the emulator repo) and the device's font lab
// draw from this same list, so a measurement and what is seen on the glass are the same drawing.
#pragma once
#include <cstdint>
#include <cstring>

namespace fontlab {

struct Sample {
  const char *name;
  const lgfx::IFont *font;
  float scale;
  bool cp437;  // Font0 holds CP437 bitmaps: UTF-8 accents must be mapped to its slots.
  bool byteFont = false;  // Says it has every code but draws its own letters for accents, so it can't be asked.
};

inline const Sample kSamples[] = {
    {"Font0 x1", &fonts::Font0, 1.0f, true},
    {"Font0 x1.5", &fonts::Font0, 1.5f, true},
    {"Font0 x2", &fonts::Font0, 2.0f, true},
    {"Font0 x3", &fonts::Font0, 3.0f, true},
    {"DejaVu9", &fonts::DejaVu9, 1.0f, false},
    {"DejaVu12", &fonts::DejaVu12, 1.0f, false},
    {"DejaVu18", &fonts::DejaVu18, 1.0f, false},
    {"Font2", &fonts::Font2, 1.0f, false},
    {"AsciiFont8x16", &fonts::AsciiFont8x16, 1.0f, false, true},
    {"efont 10", &fonts::efontCN_10, 1.0f, false},
    {"efont 12", &fonts::efontCN_12, 1.0f, false},
    {"efont 14", &fonts::efontCN_14, 1.0f, false},
    {"efont 16", &fonts::efontCN_16, 1.0f, false},
    {"efont 16 bold", &fonts::efontCN_16_b, 1.0f, false},
    {"FreeSans9", &fonts::FreeSans9pt7b, 1.0f, false},
};
constexpr int kSampleCount = sizeof(kSamples) / sizeof(kSamples[0]);

// Real text from the device: both languages, longest labels, digits people confuse.
inline const char *const kLines[] = {
    "CHASSE AU RENARD",
    "VOLUME  RÉSEAUX  ÉTAT",
    "Naomie: à ce soir, ça va?",
    "Papa: back at 8:30, ok?",
    "Wi-Fi 78%  1:23:45  0O 1lI",
    "Harry Potter 5 - Order of",
    "the Phoenix  à é è ê ç ô ù",
    "SAVED  NO CARD  NOTHING HERE",
};
constexpr int kLineCount = sizeof(kLines) / sizeof(kLines[0]);

constexpr int kHeaderHeight = 16;

// UTF-8 to the CP437 slots Font0 draws, for the accents French needs.
inline void toCp437(const char *text, char *out, size_t size) {
  struct Pair { uint8_t utf8; uint8_t slot; };
  static const Pair pairs[] = {{0xA0, 0x85}, {0xA2, 0x83}, {0xA7, 0x87}, {0xA8, 0x8A}, {0xA9, 0x82}, {0xAA, 0x88}, {0xAB, 0x89},
                               {0xAE, 0x8C}, {0xAF, 0x8B}, {0xB4, 0x93}, {0xB9, 0x97}, {0xBB, 0x96}, {0xBC, 0x81}, {0x89, 0x90}};
  size_t at = 0;
  for (const uint8_t *in = reinterpret_cast<const uint8_t *>(text); *in && at + 1 < size; in++) {
    if (*in != 0xC3) { out[at++] = static_cast<char>(*in); continue; }
    in++;
    char slot = '?';
    for (const Pair &pair : pairs) if (pair.utf8 == *in) slot = static_cast<char>(pair.slot);
    out[at++] = slot;
  }
  out[at] = 0;
}

template <class Gfx>
void useSample(Gfx &display, const Sample &sample) {
  display.setFont(sample.font);
  display.setTextSize(sample.scale);
  display.setTextWrap(false);
  display.cp437(sample.cp437);
}

template <class Gfx>
void printLine(Gfx &display, const Sample &sample, const char *text) {
  if (!sample.cp437) { display.print(text); return; }
  char encoded[96];
  toCp437(text, encoded, sizeof(encoded));
  display.print(encoded);
}

// One full screen: a name bar in the smallest font, then as many sample lines as fit.
template <class Gfx>
void drawSample(Gfx &display, int index) {
  const Sample &sample = kSamples[index];
  display.fillScreen(TFT_BLACK);
  display.fillRect(0, 0, 240, kHeaderHeight, TFT_DARKGREEN);
  display.setFont(&fonts::Font0); display.setTextSize(1); display.cp437(false); display.setTextWrap(false);
  display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  display.setCursor(4, 4); display.print(sample.name);
  display.setCursor(204, 4); display.print(index + 1); display.print('/'); display.print(kSampleCount);
  useSample(display, sample);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  const int lineHeight = display.fontHeight() + 2;
  int y = kHeaderHeight + 3;
  for (int line = 0; line < kLineCount && y + lineHeight <= 135 + 2; line++, y += lineHeight) {
    display.setCursor(3, y);
    printLine(display, sample, kLines[line]);
  }
}

}  // namespace fontlab
