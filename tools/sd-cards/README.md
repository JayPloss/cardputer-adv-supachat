# Kids' music: Spotify playlists to Navidrome and SD cards

Run on paqman. Data (Spotify pages, matches, file map) lives in `~/.local/share/cardputer-music`
(`$CARDPUTER_MUSIC_DATA`), the MP3s in `~/Music/wolfpack`. Neither is in this repo.

1. Save each playlist's embed page as `spotify/<id>.html` in the data folder
   (`curl -sL -A Mozilla/5.0 https://open.spotify.com/embed/playlist/<id>`). The playlist's name is the kid's name.
2. `uv run --with ytmusicapi python -I resolve.py`: one YouTube Music match per song, scored on title,
   artist, length and explicit flag. Read the weak ones and add corrections to `FIX` in `rip.py`.
3. `uv run --with "yt-dlp[default]" --with yt-dlp-ejs --with mutagen python -I rip.py`: rips through
   `jay-ai-arsenal/tools/yt2phone.py`, tags from Spotify (album and album artist Wolfpack). Rerun to retry failures; done files are kept.
   Songs not on YouTube (Jay's own Suno songs) are tagged the same way by hand and added to `playlists.json` / `files.json`.
4. `rsync -a --chmod=F644 ~/Music/wolfpack/ root@ori-san-host:/srv/music/library/Wolfpack/`, then
   `navidrome-playlists.py scan`, and when `status` says it's done, `navidrome-playlists.py`: each kid's playlist, as that kid.
5. Card in the reader: `uv run --with mutagen python -I card.py <mount> <Kid>` copies the kid's list to `/music/<Kid>` as `001 Title - Artist.mp3`
   (the player sorts by name) with no ID3v2 tag (the player reads a tag as a broken frame: NOT AN MP3), and appends a dated entry to `CARTE-<KID>.txt`, the card's owner note and log.
   Rerunning copies only what's missing.
6. `pkexec label-card CRD-DREW|CRD-EMMA|CRD-NAO` unmounts the card and sets its volume label; it refuses
   unless the matching `CARTE-<KID>.txt` is on the card. Root, so it opens the polkit dialog.

Papa's card (label CARDPUTERSD) holds the family library and books: none of this touches it.
