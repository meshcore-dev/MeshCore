"""Execute production Dispatcher/Packet/StaticPool routing without hardware."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RadioProfileDispatcherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
        if not compiler:
            raise RuntimeError("A native C++17 compiler is required")
        cls.directory = tempfile.TemporaryDirectory(prefix="radio-profile-dispatcher-")
        cls.addClassCleanup(cls.directory.cleanup)
        work = Path(cls.directory.name)
        (work / "Identity.h").write_text("#pragma once\n", encoding="ascii")
        # SimpleMeshTables depends only on this production interface. Keep
        # unrelated cryptography/role dependencies out of the native harness.
        (work / "Mesh.h").write_text(
            "#pragma once\n#include <Packet.h>\nnamespace mesh { class MeshTables { public:\n"
            "virtual bool wasSeen(const Packet*)=0; virtual void markSeen(const Packet*)=0;\n"
            "virtual void clear(const Packet*)=0; }; }\n", encoding="ascii")
        cls.binary = work / "dispatcher.exe"
        command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
                   "-I", str(work), "-I", str(ROOT / "src"), "-I", str(ROOT / "test/mocks"),
                   str(ROOT / "test/fixtures/radio_profiles/dispatcher.cpp"),
                   str(ROOT / "src/Dispatcher.cpp"), str(ROOT / "src/Packet.cpp"),
                   str(ROOT / "src/helpers/StaticPoolPacketManager.cpp"), "-o", str(cls.binary)]
        if os.name != "nt":
            command[1:1] = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                            "-fno-pie", "-no-pie"]
        built = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)

    def run_case(self, scenario):
        environment = os.environ.copy()
        # The upstream packet pool intentionally allocates for process lifetime
        # and has no destructor. Free-count assertions check packet ownership;
        # ASAN still checks invalid accesses, without flagging that base design.
        environment["ASAN_OPTIONS"] = environment.get("ASAN_OPTIONS", "") + ":detect_leaks=0"
        result = subprocess.run([str(self.binary), scenario], capture_output=True, text=True,
                                timeout=10, env=environment)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_receive_reply_affinity_and_local_default(self): self.run_case("affinity")
    def test_rx_rxtx_cross_and_temporary_isolation_matrix(self): self.run_case("matrix")
    def test_stale_inbound_outbound_and_cross_policy(self): self.run_case("stale")
    def test_busy_profile_does_not_starve_other_profile(self): self.run_case("cad")
    def test_pending_rx_busy_and_failed_retune_ownership(self): self.run_case("busy")
    def test_profile_replacement_resets_its_cad_clock(self): self.run_case("cad_generation")
    def test_no_retune_before_shared_duty_credit(self): self.run_case("budget")
    def test_both_profile_sends_debit_one_budget(self): self.run_case("shared_budget")
    def test_queue_reschedule_wrap_and_pool_exhaustion(self): self.run_case("queue")
    def test_exact_packet_completion_success_failure_and_crossed_copies(self): self.run_case("completion")
    def test_profiles_share_node_duplicate_suppression(self): self.run_case("dedup")
    def test_wire_bytes_and_legacy_single_profile_unchanged(self): self.run_case("wire")
    def test_dispatcher_clock_wrap(self): self.run_case("wrap")


if __name__ == "__main__":
    unittest.main()
