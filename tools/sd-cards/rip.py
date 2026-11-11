"""rip.py: rip every matched song with yt2phone.fetch into ~/Music/wolfpack/<Artist - Title>.mp3, tagged from Spotify (album Wolfpack). FIX overrides wrong matches by title."""
import sys, json, re, os
from pathlib import Path
sys.path.insert(0, os.path.expanduser("~/Work/jay-ai-arsenal/tools"))
from yt2phone import fetch
from mutagen.id3 import ID3, TIT2, TPE1, TALB, TIT1, TXXX

sp = Path(os.environ.get("CARDPUTER_MUSIC_DATA", Path.home() / ".local/share/cardputer-music"))
out = Path.home() / "Music" / "wolfpack"; out.mkdir(parents=True, exist_ok=True)
FIX = {"Mauvais Garçon": "EM66I-uVseQ", "Tous les cris les S.O.S.": "dq-032PmXU4", "Ce soir - j'suis dans ma tête": "HZKEUbNNTzI"}
m = json.loads((sp / "matches.json").read_text())
prog = sp / "progress.txt"; log = open(sp / "rip.log", "a")
safe = lambda s: re.sub(r'[\\/:*?"<>|]', "_", s).strip(". ")
done = {}
for i, (k, s) in enumerate(m.items(), 1):
    vid = FIX.get(s["title"]) or s["match"]["videoId"]
    name = f'{safe(s["artists"].split(",")[0])} - {safe(s["title"])}.mp3'
    dst = out / name
    prog.write_text(f"Wolfpack songs, {len(m) - i + 1} to go\n")
    if not dst.exists():
        tmp = out / ".tmp"; tmp.mkdir(exist_ok=True)
        got = fetch([f"https://music.youtube.com/watch?v={vid}"], tmp, log=log)
        if not got:
            print(f"!! FAILED {name} {vid}", file=log, flush=True); continue
        Path(got[0]["path"]).rename(dst)
        t = ID3(dst)
        for key in ("TIT2", "TPE1", "TALB", "TIT1"): t.delall(key)
        t.add(TPE1(encoding=3, text=s["artists"].split(", "))); t.add(TIT2(encoding=3, text=s["title"]))
        t.add(TALB(encoding=3, text="Wolfpack")); t.add(TIT1(encoding=3, text="Wolfpack"))
        t.add(TXXX(encoding=3, desc="spotify_uri", text=k)); t.save(dst, v2_version=3)
    done[k] = name
(sp / "files.json").write_text(json.dumps(done, ensure_ascii=False, indent=1))
prog.write_text("")
