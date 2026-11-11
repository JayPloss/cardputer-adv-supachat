"""card.py <mount> <Kid>: kid's playlist in order into /music/<Kid>, plus CARTE-<KID>.txt (owner + a log of operations).

Run with mutagen: uv run --with mutagen python -I card.py <mount> <Kid>
The player doesn't skip an ID3v2 tag (media_player.cpp openAt): it takes the first 0xFFE bit pattern in the
file's first 4 KB as a frame, so cover art, or even a stray byte pair in a small tag, gives NOT AN MP3. Card
copies carry no ID3v2 tag at all; the player shows the file name anyway. Files already on the card that still
have one are stripped in place.
"""
import json, re, shutil, sys, datetime
from mutagen.id3 import delete as delete_id3
from pathlib import Path
import os
sp = Path(os.environ.get("CARDPUTER_MUSIC_DATA", Path.home() / ".local/share/cardputer-music"))
card, kid = Path(sys.argv[1]), sys.argv[2]
pl = json.loads((sp / "playlists.json").read_text()); files = json.loads((sp / "files.json").read_text())
src = Path.home() / "Music" / "wolfpack"; dst = card / "music" / kid
safe = lambda s: re.sub(r'[\\/:*?"<>|]', "_", s).strip(". ")
want = {f'{n:03d} {safe(pl["songs"][k]["title"])} - {safe(pl["songs"][k]["artists"].split(", ")[0])}.mp3': files[k]
        for n, k in enumerate(pl["lists"][kid], 1)}
dst.mkdir(parents=True, exist_ok=True)
added = [n for n in want if not (dst / n).exists()]
for n in added: shutil.copyfile(src / want[n], dst / n)

def tag_bytes(path):
    head = path.read_bytes()[:10] if path.stat().st_size >= 10 else b""
    return 10 + ((head[6] << 21) | (head[7] << 14) | (head[8] << 7) | head[9]) if head[:3] == b"ID3" else 0

slimmed = []
for n in want:
    f = dst / n
    if tag_bytes(f):
        delete_id3(f, delete_v1=False, delete_v2=True)
        if n not in added: slimmed.append(n)
    if tag_bytes(f) or f.read_bytes()[:2] not in (b"\xff\xfb", b"\xff\xfa", b"\xff\xf3", b"\xff\xf2"):
        raise SystemExit(f"does not start with an MP3 frame: {f}")
log = card / f"CARTE-{kid.upper()}.txt"
if not log.exists() or "Journal" not in log.read_text():
    log.write_text(f"Carte SD de {kid} (Cardputer, Wolfpack).\nMusique : /music/{kid}, lue dans l'ordre des numéros.\n\nJournal\n=======\n")
now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
entry = f"\n{now}  {len(added)} chanson(s) copiée(s) dans /music/{kid} ({len(want)} au total), depuis paqman ~/Music/wolfpack.\n"
entry += f"  Source : la liste Spotify « {kid} » de Josée, plus les chansons de Jaybird and the Tokenizers (Suno).\n"
entry += "".join(f"  + {n}\n" for n in added)
if slimmed: entry += f"  {len(slimmed)} fichier(s) allégé(s) : étiquette ID3 retirée (le Cardputer ne lisait pas ces MP3).\n"
with log.open("a") as f: f.write(entry)
print(f"{len(added)} added, {len(slimmed)} slimmed, {len(want)} in list; log {log.name}")
