"""card.py <mount> <Kid>: kid's playlist in order into /music/<Kid>, plus CARTE-<KID>.txt (owner + a log of operations)."""
import json, re, shutil, sys, datetime
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
log = card / f"CARTE-{kid.upper()}.txt"
if not log.exists() or "Journal" not in log.read_text():
    log.write_text(f"Carte SD de {kid} (Cardputer, Wolfpack).\nMusique : /music/{kid}, lue dans l'ordre des numéros.\n\nJournal\n=======\n")
now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
entry = f"\n{now}  {len(added)} chanson(s) copiée(s) dans /music/{kid} ({len(want)} au total), depuis paqman ~/Music/wolfpack.\n"
entry += f"  Source : la liste Spotify « {kid} » de Josée, plus les chansons de Jaybird and the Tokenizers (Suno).\n"
entry += "".join(f"  + {n}\n" for n in added)
with log.open("a") as f: f.write(entry)
print(f"{len(added)} added, {len(want)} in list; log {log.name}")
