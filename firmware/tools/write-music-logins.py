#!/usr/bin/env python3
"""Puts each device's own Navidrome login into include/music_secrets.h (gitignored), from the keyring.

Each person has one account; Papa uses Jay's. The host and any home Wi-Fi lines already in the file stay.
Rerun after adding a device here and its account on the server. Prints no passwords.
"""
import re
import subprocess
from pathlib import Path

HEADER = Path(__file__).resolve().parent.parent / "include" / "music_secrets.h"
# Build flag from platformio.ini -> account on the server.
LOGINS = {"PAPA": "jay", "JUJU": "juju", "MAMA": "mama", "EMMANUELLE": "emmanuelle", "NAOMIE": "naomie", "ANDREW": "andrew"}
BEGIN, END = "// Logins begin: written by tools/write-music-logins.py", "// Logins end"


def password(account):
    done = subprocess.run(["secret-tool", "lookup", "service", "navidrome", "account", account], capture_output=True, text=True)
    value = done.stdout.strip()
    if not value:
        raise SystemExit(f"no keyring entry for Navidrome account {account}")
    return value


def define(account):
    return f'#define SUPACHAT_MUSIC_USER "{account}"\n#define SUPACHAT_MUSIC_PASSWORD "{password(account)}"\n'


text = HEADER.read_text()
text = re.sub(re.escape(BEGIN) + r".*?" + re.escape(END) + r"\n", "", text, flags=re.S)
text = re.sub(r'^#define SUPACHAT_MUSIC_(USER|PASSWORD) .*\n', "", text, flags=re.M)

block = BEGIN + "\n"
for index, (flag, account) in enumerate(LOGINS.items()):
    block += f'#{"if" if index == 0 else "elif"} defined(SUPACHAT_DEVICE_{flag})\n' + define(account)
# Albie's build names no device, and neither does QEMU's; the emulator builds a test device on this login.
block += "#elif !defined(SUPACHAT_DEVICE_ID) || defined(EMU_HEAP_START)\n" + define("albie")
block += '#else\n#error "This device has no music login: add it to tools/write-music-logins.py"\n#endif\n' + END + "\n"

host = re.search(r'^#define SUPACHAT_MUSIC_HOST .*\n', text, flags=re.M)
if not host:
    raise SystemExit("no SUPACHAT_MUSIC_HOST line in the header")
HEADER.write_text(text[:host.end()] + block + text[host.end():])
print(f"wrote {len(LOGINS) + 1} logins")
