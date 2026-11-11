"""resolve.py: Spotify playlist embed pages (DATA/spotify/*.html) -> DATA/playlists.json, plus one YouTube Music match per song (DATA/matches.json)."""
import os, sys, re, json, unicodedata, difflib
from pathlib import Path
from ytmusicapi import YTMusic

DATA = Path(os.environ.get("CARDPUTER_MUSIC_DATA", Path.home() / ".local/share/cardputer-music"))
sp = DATA
def norm(s):
    s = unicodedata.normalize("NFKD", s.lower()); s = "".join(c for c in s if not unicodedata.combining(c))
    return re.sub(r"[^a-z0-9а-я ]+", " ", s).split()
lists, songs = {}, {}
for f in sorted((sp / "spotify").glob("*.html")):
    d = json.loads(re.search(r'<script id="__NEXT_DATA__"[^>]*>(.*?)</script>', f.read_text(), re.S).group(1))
    e = d["props"]["pageProps"]["state"]["data"]["entity"]
    keys = []
    for t in e["trackList"]:
        k = t["uri"]; keys.append(k)
        songs[k] = {"artists": t["subtitle"], "title": t["title"], "ms": t["duration"], "explicit": t["isExplicit"]}
    lists[e["name"]] = keys
(sp / "playlists.json").write_text(json.dumps({"lists": lists, "songs": songs}, ensure_ascii=False, indent=1))
yt = YTMusic()
out = {}
for k, s in songs.items():
    want = s["ms"] / 1000; tw = set(norm(s["title"])); aw = set(norm(s["artists"]))
    best = None
    for r in yt.search(f'{s["artists"].split(",")[0]} {s["title"]}', filter="songs", limit=10)[:8]:
        if not r.get("videoId"): continue
        ra = " ".join(a["name"] for a in r.get("artists") or [])
        ts = difflib.SequenceMatcher(None, " ".join(norm(s["title"])), " ".join(norm(r["title"]))).ratio()
        as_ = len(aw & set(norm(ra))) / max(1, len(set(norm(s["artists"].split(",")[0]))))
        dd = abs((r.get("duration_seconds") or 0) - want)
        score = ts * 2 + min(as_, 1) * 1.5 - min(dd, 60) / 15 - (0.5 if bool(r.get("isExplicit")) != s["explicit"] else 0)
        cand = {"videoId": r["videoId"], "yt_title": r["title"], "yt_artists": ra, "yt_s": r.get("duration_seconds"), "dd": round(dd), "ts": round(ts, 2), "score": round(score, 2), "yt_explicit": r.get("isExplicit")}
        if not best or score > best["score"]: best = cand
    out[k] = {**s, "match": best}
    m = best or {}
    print(f'{m.get("score","--"):>5} dd={m.get("dd","-"):>3}  {s["artists"][:30]} - {s["title"][:40]}  =>  {m.get("yt_artists","")[:30]} - {m.get("yt_title","")[:40]}', flush=True)
(sp / "matches.json").write_text(json.dumps(out, ensure_ascii=False, indent=1))
