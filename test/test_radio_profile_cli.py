"""Exercise the production radio CLI, scheduler and existing JSON serializer."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class RadioProfileCLITests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
        if not compiler:
            raise RuntimeError("A native C++11 compiler is required")
        cls.directory = tempfile.TemporaryDirectory(prefix="radio-profile-cli-")
        cls.addClassCleanup(cls.directory.cleanup)
        work = Path(cls.directory.name)
        (work / "Arduino.h").write_text(
            '#pragma once\n#include <stdint.h>\n#include <math.h>\n#include "Stream.h"\n'
            'extern uint32_t test_millis; inline uint32_t millis(){ return test_millis; }\n')
        (work / "Dispatcher.h").write_text((ROOT / "test/fixtures/radio_profiles/cli_backend.h").read_text())
        cls.binary = work / "cli.exe"
        command = [compiler, "-std=c++11", "-Wall", "-Wextra", "-Wno-unused-parameter",
                   "-I", str(work), "-I", str(ROOT / "src"), "-I", str(ROOT / "test/mocks"),
                   str(ROOT / "test/fixtures/radio_profiles/cli.cpp"),
                   str(ROOT / "src/helpers/RadioProfileCLI.cpp"),
                   str(ROOT / "src/helpers/ConfigSerializer.cpp"), str(ROOT / "src/Utils.cpp"), "-o", str(cls.binary)]
        if os.name != "nt":
            command[1:1] = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                            "-fno-pie", "-no-pie"]
        built = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)

    def run_case(self, scenario):
        result = subprocess.run([str(self.binary), scenario], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_strict_tuple_modes_preamble_and_duration(self): self.run_case("parse")
    def test_saved_primary_reboot_and_temporary_isolation(self): self.run_case("primary")
    def test_monotonic_leases_wrap_and_wall_clock_changes(self): self.run_case("lease")
    def test_expiry_retires_old_generation_while_replacement_waits(self): self.run_case("replacement")
    def test_one_slot_per_family_replacement_cancel_and_order(self): self.run_case("schedule")
    def test_busy_and_failed_primary_apply_preserve_saved_prefs(self): self.run_case("failure")
    def test_exact_remote_response_success_failure_long_airtime(self): self.run_case("remote")
    def test_remote_acceptance_deadlines_survive_rtc_jumps(self): self.run_case("remote_clock")
    def test_existing_json_roundtrip_and_legacy_defaults(self): self.run_case("serialize")
    def test_unsupported_backend_preserves_legacy_parser(self): self.run_case("legacy")

if __name__ == "__main__":
    unittest.main()
