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
    for command in checks:
        completed = subprocess.run(command, cwd=repo, check=False)
        if completed.returncode:
            raise RuntimeError(f"Firmware upload blocked by emulator gate: {' '.join(command)}")


if any(target in COMMAND_LINE_TARGETS for target in ("upload", "program")):
    env.AddPreAction("upload", run_device_contract_gate)
