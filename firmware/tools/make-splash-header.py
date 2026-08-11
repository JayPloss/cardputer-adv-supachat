from pathlib import Path

from PIL import Image, ImageEnhance, ImageOps


ROOT = Path(__file__).resolve().parents[1]
WIDTH = 240
HEIGHT = 135
SOURCES = {
    "albie": ROOT / "assets" / "boot" / "supachat-albie-source.png",
    "juju": ROOT / "assets" / "boot" / "supachat-julien-source.png",
    "papa": ROOT / "assets" / "boot" / "supachat-logo-source.png",
}


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


include_dir = ROOT / "include"
include_dir.mkdir(parents=True, exist_ok=True)

selector = include_dir / "splash_logo.h"
selector.write_text(
    '#pragma once\n\n#if defined(SUPACHAT_DEVICE_JUJU)\n'
    '#include "splash_logo_juju.h"\n#elif defined(SUPACHAT_DEVICE_PAPA)\n'
    '#include "splash_logo_papa.h"\n#else\n'
    '#include "splash_logo_albie.h"\n#endif\n',
    encoding="utf-8",
    newline="\n",
)

for identity, source_path in SOURCES.items():
    if not source_path.is_file():
        raise SystemExit(f"Splash source not found: {source_path}")

    # Crop only excess black canvas, then make exactly one high-quality resize.
    source = Image.open(source_path).convert("RGB")
    image = ImageOps.fit(
        source,
        (WIDTH, HEIGHT),
        method=Image.Resampling.LANCZOS,
        centering=(0.5, 0.5),
    )
    image = ImageEnhance.Sharpness(image).enhance(1.04)

    preview = ROOT / "assets" / "boot" / f"supachat-splash-{identity}-240x135.png"
    image.save(preview, optimize=True)

    raw = image.tobytes()
    pixels = [
        rgb565(raw[index], raw[index + 1], raw[index + 2])
        for index in range(0, len(raw), 3)
    ]
    header = include_dir / f"splash_logo_{identity}.h"
    with header.open("w", encoding="utf-8", newline="\n") as output:
        output.write("#pragma once\n\n#include <Arduino.h>\n\n")
        output.write(f"constexpr int kSupaChatSplashWidth = {WIDTH};\n")
        output.write(f"constexpr int kSupaChatSplashHeight = {HEIGHT};\n")
        output.write("const uint16_t kSupaChatSplash[] PROGMEM = {\n")
        for index in range(0, len(pixels), 12):
            values = ", ".join(
                f"0x{value:04X}" for value in pixels[index : index + 12]
            )
            output.write(f"    {values},\n")
        output.write("};\n")

    print(preview)
    print(header)

print(selector)
