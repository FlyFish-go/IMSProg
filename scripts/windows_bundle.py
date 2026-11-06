#!/usr/bin/env python3
"""Collect/check all x64 PE imports, including those of deployed Qt plugins.

Only the bundle, an explicitly supplied build runtime directory, and Windows
System32 participate in resolution. Developer PATH must never satisfy an import.
Run again without --runtime-dir against the extracted release ZIP to verify it.
"""

import argparse
from collections import deque
from pathlib import Path
import re
import shutil
import subprocess
import sys


def pe_imports(path, objdump):
    result = subprocess.run(
        [str(objdump), "-f", "-p", str(path)],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
        check=True,
    )
    if not re.search(r"architecture: i386:x86-64(?:,|\s)", result.stdout):
        raise RuntimeError(f"Not an x64 PE binary: {path}")
    if "file format pei-x86-64" not in result.stdout:
        raise RuntimeError(f"Not an x64 PE image: {path}")
    return re.findall(r"DLL Name:\s*(\S+)", result.stdout)


def file_index(directory):
    result = {}
    for path in sorted(directory.iterdir()):
        if path.is_file():
            key = path.name.casefold()
            if key in result:
                raise RuntimeError(f"Ambiguous Windows filename: {path}")
            result[key] = path
    return result


def resolve_bundle(bundle, system_dir, objdump, runtime_dir=None):
    bundle = bundle.resolve(strict=True)
    system = file_index(system_dir.resolve(strict=True))
    # Catch a mistyped/empty system directory instead of copying OS DLLs.
    if "kernel32.dll" not in system:
        raise RuntimeError(f"Not a Windows system directory: {system_dir}")
    runtime = file_index(runtime_dir.resolve(strict=True)) if runtime_dir else {}
    root = file_index(bundle)
    pending = deque(sorted(
        p for p in bundle.rglob("*")
        if p.is_file() and p.suffix.casefold() in (".exe", ".dll")
        # driver\ is the signed WCH driver package (x64 and 32-bit WOW DLLs),
        # installed by pnputil; it is not part of the application's imports.
        and p.relative_to(bundle).parts[0].casefold() != "driver"
    ))
    if not pending:
        raise RuntimeError(f"No PE binaries in {bundle}")
    visited = set()
    copied = []
    missing = []
    while pending:
        binary = pending.popleft()
        if binary in visited:
            continue
        visited.add(binary)
        for name in pe_imports(binary, objdump):
            key = name.casefold()
            if Path(name).name != name or "/" in name or "\\" in name:
                raise RuntimeError(f"Invalid import name in {binary}: {name}")
            # API sets are contracts supplied by Windows, not distributable DLLs.
            if key.startswith(("api-ms-win-", "ext-ms-win-")):
                continue
            dependency = root.get(key)
            if dependency is None and binary.parent != bundle:
                dependency = file_index(binary.parent).get(key)
            if dependency is not None:
                pending.append(dependency)
            elif key in system:
                continue
            elif key in runtime:
                dependency = bundle / runtime[key].name
                shutil.copy2(runtime[key], dependency)
                root[key] = dependency
                copied.append(dependency.name)
                pending.append(dependency)
                print(f"Copied {dependency.name} (required by {binary.relative_to(bundle)})")
            else:
                missing.append(f"{binary.relative_to(bundle)} -> {name}")
    if missing:
        raise RuntimeError("Unresolved portable imports:\n  " + "\n  ".join(sorted(set(missing))))
    print(f"Verified {len(visited)} x64 PE binaries; {len(copied)} runtime DLLs added")
    return copied


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--system-dir", required=True, type=Path)
    parser.add_argument("--objdump", required=True, type=Path)
    parser.add_argument("--runtime-dir", type=Path,
                        help="Copy missing imports from this matching build toolchain only")
    args = parser.parse_args()
    try:
        resolve_bundle(args.bundle, args.system_dir, args.objdump, args.runtime_dir)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
