from pathlib import Path
import re

from PIL import Image, ImageEnhance, ImageOps


ROOT = Path(__file__).resolve().parents[1]
WIDTH = 240
HEIGHT = 135
SOURCES = {
    "albie": ROOT / "assets" / "boot" / "supachat-albie-source.png",
    "juju": ROOT / "assets" / "boot" / "supachat-julien-source.png",
    "papa": ROOT / "assets" / "boot" / "supachat-logo-source.png",
    "emmanuelle": ROOT / "assets" / "boot" / "supachat-emmanuelle-source.png",
    "naomie": ROOT / "assets" / "boot" / "supachat-naomie-source.png",
    "andrew": ROOT / "assets" / "boot" / "supachat-andrew-source.png",
}


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def unpack_rgb565(value: int) -> tuple[int, int, int]:
    return ((value >> 11) & 0x1F) << 3, ((value >> 5) & 0x3F) << 2, (value & 0x1F) << 3


assert rgb565(255, 0, 0) == 0xF800
assert rgb565(0, 255, 0) == 0x07E0
assert rgb565(0, 0, 255) == 0x001F
assert rgb565(255, 255, 255) == 0xFFFF
assert rgb565(0, 0, 0) == 0x0000


include_dir = ROOT / "include"
include_dir.mkdir(parents=True, exist_ok=True)

selector = include_dir / "splash_logo.h"
selector.write_text(
    '#pragma once\n\n#if defined(SUPACHAT_DEVICE_JUJU)\n'
    '#include "splash_logo_juju.h"\n#elif defined(SUPACHAT_DEVICE_PAPA)\n'
    '#include "splash_logo_papa.h"\n#elif defined(SUPACHAT_DEVICE_EMMANUELLE)\n'
    '#include "splash_logo_emmanuelle.h"\n#elif defined(SUPACHAT_DEVICE_NAOMIE)\n'
    '#include "splash_logo_naomie.h"\n#elif defined(SUPACHAT_DEVICE_ANDREW)\n'
    '#include "splash_logo_andrew.h"\n#else\n'
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

    emitted = [int(value, 16) for value in re.findall(r"0x([0-9A-F]{4})", header.read_text(encoding="utf-8"))]
    if emitted != pixels or len(emitted) != WIDTH * HEIGHT:
        raise SystemExit(f"RGB565 array mismatch for {identity}")
    decoded = [unpack_rgb565(value) for value in emitted]
    source_pixels = list(image.getdata())
    for index, (expected, actual) in enumerate(zip(source_pixels, decoded)):
        if abs(expected[0] - actual[0]) > 7 or abs(expected[1] - actual[1]) > 3 or abs(expected[2] - actual[2]) > 7:
            x, y = index % WIDTH, index // WIDTH
            raise SystemExit(f"RGB565 decode mismatch for {identity} at ({x},{y}): {expected} != {actual}")
    reference_indexes = {
        max(range(len(source_pixels)), key=lambda i: source_pixels[i][0] - source_pixels[i][1] - source_pixels[i][2]),
        max(range(len(source_pixels)), key=lambda i: source_pixels[i][1] - source_pixels[i][0] - source_pixels[i][2]),
        max(range(len(source_pixels)), key=lambda i: source_pixels[i][2] - source_pixels[i][0] - source_pixels[i][1]),
        max(range(len(source_pixels)), key=lambda i: sum(source_pixels[i])),
        min(range(len(source_pixels)), key=lambda i: sum(source_pixels[i])),
    }
    if len(reference_indexes) < 5:
        raise SystemExit(f"Insufficient distinct RGB565 reference pixels for {identity}")
    for index in reference_indexes:
        if emitted[index] != rgb565(*source_pixels[index]):
            x, y = index % WIDTH, index // WIDTH
            raise SystemExit(f"RGB565 reference mismatch for {identity} at ({x},{y})")

    print(preview)
    print(header)

print(selector)
