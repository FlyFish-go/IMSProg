#!/usr/bin/env python3
"""Windows-branch source conformance only; not Windows runtime verification."""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
source = (root / "IMSProg_programmer/mainwindow.cpp").read_text()
checks = 0

def body(name):
    start = source.index("void MainWindow::" + name + "(")
    start = source.index("{", start)
    # Preserve offsets while ignoring braces in strings and comments.
    mask = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: " " * len(m.group()), source)
    depth = 0
    for pos in range(start, len(source)):
        if mask[pos] == "{": depth += 1
        elif mask[pos] == "}":
            depth -= 1
            if not depth: return source[start:pos + 1]
    raise AssertionError("unclosed function " + name)

def require(condition, message):
    global checks
    checks += 1
    if not condition: raise AssertionError(message)

platform = body("applyPlatformRestrictions")
windows = platform[platform.index("#ifdef _WIN32"):]
require("current_programmer == 4 || current_programmer > 5" in windows,
        "valid CH341 indices 0/1 and CH347T/F indices 2/3/5 must not be normalized")
require("ui->actionCH347F->setChecked(current_programmer == 5)" in windows,
        "CH347F checked action tracks exact selected index")
for index, action in enumerate(("actionCH341A_B_v1_2", "actionCH341A_v1_7")):
    require(f"ui->{action}->setEnabled(true)" in windows, "CH341 selector available on Windows")
    require(f"ui->{action}->setChecked(current_programmer == {index})" in windows,
            "CH341 checked action tracks exact selected index")
for index in (1, 2):
    require(f'SetItemStatus("comboBox_type", {index}, true)' in windows,
            "Windows I2C and MicroWire remain unsupported")
for action in ("actionFT232H_v1_2", "actionShow_programmer_version"):
    require(f"ui->{action}->setEnabled(false)" in windows, "unsupported Windows action remains disabled")
for callback in ("on_actionCH341A_B_v1_2_triggered", "on_actionCH341A_v1_7_triggered"):
    require("applyPlatformRestrictions();" in body(callback),
            "CH341 callback reapplies restrictions after inherited MicroWire enable")
for action in ("actionCH341A_B_v1_2", "actionCH341A_v1_7", "actionCH347T", "actionCH347T_v1_1", "actionCH347F"):
    require(f"ui->{action}->setDisabled(true)" in body("doNotDisturb"),
            "all programmer selectors remain locked during I/O")
require("applyPlatformRestrictions();" in body("doNotDisturbCancel"),
        "operation cleanup reapplies Windows restrictions")
diag = body("on_actionConnection_diagnostics_triggered")
require(re.search(r'if\s*\(current_programmer\s*<\s*2\)\s*ch341_windows_diagnose\(\);\s*else\s*ch347_windows_diagnose\(\);', diag),
        "diagnostics explicitly routes by the user's selected family")
require("ch347_select_model(current_programmer == 5 ? CH347_MODEL_F : CH347_MODEL_T)" in diag,
        "diagnostics enumerates only the selected CH347 variant")
require("if (ui->actionStop->isEnabled()) return;" in diag and "timer->stop();" in diag,
        "diagnostics refuses concurrent I/O and pauses timer")
ui = ET.parse(root / "IMSProg_programmer/mainwindow.ui")
text = ui.find(".//action[@name='actionConnection_diagnostics']/property[@name='text']/string")
require(text is not None and "Programmer connection diagnostics" in text.text,
        "diagnostics menu label covers both families")
print(f"PASS: {checks} Windows UI source-conformance checks (not Windows runtime tests)")
