"""navidrome-playlists.py [scan|status]: make each kid's Navidrome playlist (Spotify order) from the Wolfpack album, logged in as that kid."""
import os, sys, json, subprocess, hashlib, secrets, urllib.request, urllib.parse
from pathlib import Path
sp = Path(os.environ.get("CARDPUTER_MUSIC_DATA", Path.home() / ".local/share/cardputer-music")); HOST = "http://192.168.0.235:4533"
pl = json.loads((sp / "playlists.json").read_text()); files = json.loads((sp / "files.json").read_text())
def call(user, ep, params):
    pw = subprocess.run(["secret-tool", "lookup", "service", "navidrome", "account", user], capture_output=True, text=True).stdout.strip()
    salt = secrets.token_hex(6)
    q = [("u", user), ("t", hashlib.md5((pw + salt).encode()).hexdigest()), ("s", salt), ("v", "1.16.1"), ("c", "wolfpack-rip"), ("f", "json")] + params
    with urllib.request.urlopen(f"{HOST}/rest/{ep}?{urllib.parse.urlencode(q)}", timeout=60) as r:
        d = json.load(r)["subsonic-response"]
    if d["status"] != "ok": raise SystemExit(f"{ep} as {user}: {d}")
    return d
if sys.argv[1:] == ["scan"]:
    print(call("admin", "startScan", [("fullScan", "false")])["scanStatus"]); raise SystemExit
if sys.argv[1:] == ["status"]:
    print(call("admin", "getScanStatus", [])["scanStatus"]); raise SystemExit
for kid, keys in pl["lists"].items():
    user = kid.lower()
    albums = call(user, "search3", [("query", "Wolfpack"), ("songCount", "0"), ("artistCount", "0"), ("albumCount", "50")]).get("searchResult3", {}).get("album", [])
    found = [t for a in albums if a["name"] == "Wolfpack" for t in call(user, "getAlbum", [("id", a["id"])])["album"].get("song", [])]
    def pick(k):
        want = pl["songs"][k]; first = want["artists"].split(", ")[0].lower()
        hits = [t["id"] for t in found if t["title"] == want["title"] and first in (t.get("artist") or "").lower()]
        return hits[0] if len(hits) == 1 else None
    ids = [pick(k) for k in keys]
    missing = [files.get(k) or k for k, i in zip(keys, ids) if not i]
    for p in call(user, "getPlaylists", []).get("playlists", {}).get("playlist", []):
        if p["name"] == kid: call(user, "deletePlaylist", [("id", p["id"])])
    call(user, "createPlaylist", [("name", kid)] + [("songId", i) for i in ids if i])
    print(f"{kid}: {sum(1 for i in ids if i)}/{len(keys)} in playlist" + (f"; missing {missing}" if missing else ""))
