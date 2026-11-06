"""Dependency-closure regressions; no Windows installation or hardware needed."""

import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "windows_bundle", Path(__file__).resolve().parents[1] / "scripts/windows_bundle.py")
bundle_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle_tool)


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        base = Path(self.temp.name)
        self.bundle = base / "portable bundle"
        self.runtime = base / "toolchain"
        self.system = base / "System32"
        for path in (self.bundle, self.runtime, self.system):
            path.mkdir()
        (self.system / "KERNEL32.dll").touch()
        (self.bundle / "app.exe").touch()
        self.imports = {"app.exe": ["KERNEL32.dll", "api-ms-win-crt-runtime-l1-1-0.dll"]}
        self.addCleanup(patch.stopall)
        patch.object(bundle_tool, "pe_imports", side_effect=lambda p, _: self.imports[p.name]).start()

    def resolve(self, copy=False):
        with contextlib.redirect_stdout(io.StringIO()):
            return bundle_tool.resolve_bundle(
                self.bundle, self.system, Path("objdump"), self.runtime if copy else None)

    def test_system_imports_need_no_copy(self):
        self.assertEqual(self.resolve(), [])

    def test_missing_dll_fails_even_if_on_developer_path(self):
        (self.runtime / "missing.dll").touch()
        self.imports["app.exe"].append("missing.dll")
        with patch.dict("os.environ", {"PATH": str(self.runtime)}):
            with self.assertRaisesRegex(RuntimeError, "app.exe -> missing.dll"):
                self.resolve()

    def test_recursive_copy_handles_cycles_and_case(self):
        self.imports["app.exe"].append("First.DLL")
        self.imports["first.dll"] = ["SECOND.dll"]
        self.imports["second.dll"] = ["FIRST.dll"]
        for name in ("first.dll", "second.dll"):
            (self.runtime / name).write_bytes(name.encode())
        self.assertEqual(self.resolve(copy=True), ["first.dll", "second.dll"])
        self.assertEqual((self.bundle / "second.dll").read_bytes(), b"second.dll")
        self.assertEqual(self.resolve(), [])

    def test_plugin_dependency_is_checked_without_exe_import(self):
        plugin = self.bundle / "imageformats/qjpeg.dll"
        plugin.parent.mkdir()
        plugin.touch()
        self.imports["qjpeg.dll"] = ["libjpeg-8.dll"]
        with self.assertRaisesRegex(RuntimeError, "qjpeg.dll -> libjpeg-8.dll"):
            self.resolve()

    def test_plugin_subdirectory_cannot_satisfy_app_import(self):
        plugin = self.bundle / "plugins/hidden.dll"
        plugin.parent.mkdir()
        plugin.touch()
        self.imports["hidden.dll"] = []
        self.imports["app.exe"].append("hidden.dll")
        with self.assertRaisesRegex(RuntimeError, "app.exe -> hidden.dll"):
            self.resolve()

    def test_copied_transitive_missing_import_fails(self):
        self.imports["app.exe"].append("first.dll")
        (self.runtime / "first.dll").touch()
        self.imports["first.dll"] = ["deeper.dll"]
        with self.assertRaisesRegex(RuntimeError, "first.dll -> deeper.dll"):
            self.resolve(copy=True)

    def test_unusable_system_directory_fails(self):
        (self.system / "KERNEL32.dll").unlink()
        with self.assertRaisesRegex(RuntimeError, "Not a Windows system directory"):
            self.resolve()

    def test_empty_bundle_fails(self):
        (self.bundle / "app.exe").unlink()
        with self.assertRaisesRegex(RuntimeError, "No PE binaries"):
            self.resolve()


class ObjdumpTests(unittest.TestCase):
    def inspect(self, output):
        with patch.object(bundle_tool.subprocess, "run") as run:
            run.return_value.stdout = output
            return bundle_tool.pe_imports(Path("app.exe"), Path("objdump"))

    def test_parse_x64_imports(self):
        self.assertEqual(self.inspect(
            "file format pei-x86-64\narchitecture: i386:x86-64, flags\n"
            "\tDLL Name: KERNEL32.dll\n\tDLL Name: Qt6Gui.dll\n"),
            ["KERNEL32.dll", "Qt6Gui.dll"])

    def test_reject_wrong_architecture(self):
        with self.assertRaisesRegex(RuntimeError, "Not an x64 PE binary"):
            self.inspect("file format pei-i386\narchitecture: i386, flags\n")

    def test_reject_object_file(self):
        with self.assertRaisesRegex(RuntimeError, "Not an x64 PE image"):
            self.inspect("file format pe-x86-64\narchitecture: i386:x86-64, flags\n")


if __name__ == "__main__":
    unittest.main()
