#!/usr/bin/env python3
"""mp3-fixtures.py OUT: writes MP3s of the kinds real libraries hold, plus OUT/manifest.tsv
(name, expect ok|mp2|mpeg25|fail, true duration in seconds from ffprobe). Needs ffmpeg and mutagen
(uv run --with mutagen python -I mp3-fixtures.py OUT). Used by mp3-probe-test.cpp and the emulator's
card-formats scenario."""
import os, struct, subprocess, sys
from pathlib import Path
from mutagen.id3 import ID3, TIT2, TPE1, APIC, TXXX

out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
tone = ["-f", "lavfi", "-i", "sine=frequency=440:duration=12,aformat=channel_layouts=stereo"]
def ff(name, *args, src=tone):
    subprocess.run(["ffmpeg", "-v", "error", "-y", *src, *args, str(out / name)], check=True)
def dur(name):
    return float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", str(out / name)],
                                capture_output=True, text=True).stdout)
def syncsafe(n): return bytes([(n >> 21) & 127, (n >> 14) & 127, (n >> 7) & 127, n & 127])
def prepend(name, data):
    p = out / name; p.write_bytes(data + p.read_bytes())

rows = []
ff("cbr128-plain.mp3", "-c:a", "libmp3lame", "-b:a", "128k", "-write_xing", "0", "-id3v2_version", "0")
ff("vbr-v0-48k-xing.mp3", "-ar", "48000", "-c:a", "libmp3lame", "-q:a", "0", "-id3v2_version", "0")
ff("mpeg2-22k-mono.mp3", "-ar", "22050", "-ac", "1", "-c:a", "libmp3lame", "-b:a", "32k", "-id3v2_version", "0")
ff("mpeg25-8k-mono.mp3", "-ar", "8000", "-ac", "1", "-c:a", "libmp3lame", "-b:a", "16k", "-id3v2_version", "0")
ff("mp2.mp3", "-c:a", "mp2", "-b:a", "192k", "-f", "mp2")
# Cover art: a 300 KB picture (noise does not compress), as a phone's tagger leaves it.
art = os.urandom(300_000)
ff("vbr-cover-art.mp3", "-c:a", "libmp3lame", "-q:a", "2", "-id3v2_version", "0")
t = ID3(); t.add(TIT2(encoding=3, text="Art")); t.add(TPE1(encoding=3, text="Test"))
t.add(APIC(encoding=3, mime="image/jpeg", type=3, desc="", data=art)); t.save(out / "vbr-cover-art.mp3", v2_version=3)
# A small tag whose text holds a frame header's bytes: what broke the kids' cards.
ff("false-sync-tag.mp3", "-c:a", "libmp3lame", "-b:a", "192k", "-id3v2_version", "0")
t = ID3(); t.add(TXXX(encoding=0, desc="x", text="\xff\xfb\x90\x64 fake")); t.save(out / "false-sync-tag.mp3", v2_version=3)
# An ID3v2.4 tag with a footer, written by hand.
ff("id3v24-footer.mp3", "-c:a", "libmp3lame", "-b:a", "128k", "-id3v2_version", "0")
body = b"TIT2" + syncsafe(6) + b"\x00\x00" + b"\x03Title"
prepend("id3v24-footer.mp3", b"ID3\x04\x00\x10" + syncsafe(len(body)) + body + b"3DI\x04\x00\x10" + syncsafe(len(body)))
# Junk between the tag and the audio, where the tag size doesn't reach.
ff("junk-after-tag.mp3", "-c:a", "libmp3lame", "-b:a", "128k", "-id3v2_version", "0")
prepend("junk-after-tag.mp3", b"ID3\x03\x00\x00" + syncsafe(0) + bytes([0xFF, 0xF0, 0, 0]) * 500 + os.urandom(3000).replace(b"\xff", b"\x00"))
# Tags at the end: APEv2 and ID3v1.
ff("tail-tags.mp3", "-c:a", "libmp3lame", "-b:a", "128k", "-id3v2_version", "0")
with open(out / "tail-tags.mp3", "ab") as f:
    f.write(b"APETAGEX" + struct.pack("<IIII", 2000, 32, 0, 0) + b"\x00" * 8 + b"TAG" + b"Title".ljust(125, b"\x00"))
(out / "not-audio.mp3").write_bytes(os.urandom(50_000).replace(b"\xff", b"\x00"))

for name, expect in [("cbr128-plain.mp3", "ok"), ("vbr-v0-48k-xing.mp3", "ok"), ("mpeg2-22k-mono.mp3", "ok"), ("mpeg25-8k-mono.mp3", "mpeg25"),
                     ("mp2.mp3", "mp2"), ("vbr-cover-art.mp3", "ok"), ("false-sync-tag.mp3", "ok"), ("id3v24-footer.mp3", "ok"),
                     ("junk-after-tag.mp3", "ok"), ("tail-tags.mp3", "ok"), ("not-audio.mp3", "fail")]:
    rows.append(f"{name}\t{expect}\t{dur(name) if expect == 'ok' else 0:.3f}")
(out / "manifest.tsv").write_text("\n".join(rows) + "\n")
print(f"{len(rows)} files in {out}")
