#!/usr/bin/env python3
"""Source conformance for CH347F auxiliary dialogs; no hardware is accessed."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
programmer = root / "IMSProg_programmer"
checks = 0


def require(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)


auxiliary_calls = {
    "dialogbbm.cpp": ("programmerType, 6, 15000", 2),
    "dialognandsecurity.cpp": ("programmerType, 6, 15000", 2),
    "dialognandsr.cpp": ("programmerType, 6, 15000", 2),
    "dialogsecurity.cpp": ("programmerType, 0, 5000", 3),
    "dialogsfdp.cpp": ("programmerType, 0, 5000", 5),
}
for filename, (arguments, count) in auxiliary_calls.items():
    source = (programmer / filename).read_text()
    call = f"getAuxiliaryInterfaceSpeed({arguments})"
    require(source.count(call) == count,
            f"{filename} must route every fixed auxiliary speed through the CH347F cap")

status_source = (programmer / "dialogsr.cpp").read_text()
require(status_source.count(
    "getAuxiliaryInterfaceSpeed(programmerType, currentChipType, 5000)") == 2,
    "EEPROM status read/write must honor the selected CH347F preset")
require(not re.search(r"ProgDeviceInit\([^,]+,\s*[^,]+,\s*(?:15000|5000)\s*\)", "\n".join(
    (programmer / name).read_text()
    for name in (*auxiliary_calls, "dialogsr.cpp")
)),
    "auxiliary dialogs must not bypass the CH347F speed cap with a fixed clock")

info_source = (programmer / "dialoginfo.cpp").read_text()
guard = info_source.index("if (PROG_IS_CH347F(currentProg))")
switch = info_source.index("switch (chipType)", guard)
guard_body = info_source[guard:switch]
require("ui->label->setPixmap(*pixnone);" in guard_body,
        "CH347F must use the generic/unknown pinout image")
require("Unknown CH347F pinout.\\nCheck the board documentation." in guard_body,
        "CH347F must tell the user to verify the board documentation")
require("return;" in guard_body,
        "CH347F must not fall through to a different programmer's pinout")

print(f"PASS: {checks} CH347F auxiliary-dialog conformance checks")
