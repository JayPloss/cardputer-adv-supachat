// The font lab: a separate firmware, never part of a normal build. Left and Right step through the
// candidate fonts on the real screen, because how readable a font is can only be judged on the glass.
#include <M5Cardputer.h>

#include "font_samples.h"

namespace {
int current = 0;
}

void setup() {
  auto config = M5.config();
  M5Cardputer.begin(config, true);
  M5Cardputer.Display.setRotation(1);
  fontlab::drawSample(M5Cardputer.Display, current);
}

void loop() {
  M5Cardputer.update();
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) { delay(5); return; }
  const auto keys = M5Cardputer.Keyboard.keysState();
  int step = 0;
  for (const char key : keys.word) {
    if (key == '/') step = 1;
    if (key == ',') step = -1;
  }
  if (!step) return;
  current = (current + step + fontlab::kSampleCount) % fontlab::kSampleCount;
  fontlab::drawSample(M5Cardputer.Display, current);
}
