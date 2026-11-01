Import("env")

import os
import subprocess
from SCons.Script import COMMAND_LINE_TARGETS


def run_device_contract_gate(source, target, env):
    repo = os.path.abspath(os.path.join(env["PROJECT_DIR"], ".."))
    checks = [
        ["node", os.path.join(repo, "emulator", "device-contract.mjs")],
        ["node", os.path.join(repo, "emulator", "test-flow.mjs")],
    ]
    # The runtime emulator, when it is checked out beside this repo: real firmware on a device-sized heap.
    # The power lab is its own program and has no scenarios.
    runtime = os.path.abspath(os.path.join(repo, "..", "cardputer-emulator"))
    if os.path.isdir(runtime) and env["PIOENV"] != "power-lab":
        checks.append(["python3", os.path.join(runtime, "tools", "check.py")])
    for command in checks:
        completed = subprocess.run(command, cwd=repo, check=False)
        if completed.returncode:
            raise RuntimeError(f"Firmware upload blocked by emulator gate: {' '.join(command)}")


if any(target in COMMAND_LINE_TARGETS for target in ("upload", "program")):
    env.AddPreAction("upload", run_device_contract_gate)
