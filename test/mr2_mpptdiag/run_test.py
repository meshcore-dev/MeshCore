#!/usr/bin/env python3
"""Compile the real MR2 MPPT diagnostic method against a checked I2C mock.

Run with: python test/mr2_mpptdiag/run_test.py [--cxx path/to/g++]
The temporary C++ source stays outside PlatformIO's test discovery.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct BqMock {
  uint8_t registers[256] = {};
  int fail = -1;
  std::vector<uint8_t> reads;
  bool readReg(uint8_t reg, uint8_t& value) {
    reads.push_back(reg);
    if (reg == fail) return false;  // Leave value untouched on failure.
    value = registers[reg];
    return true;
  }
  // Intentionally no unchecked read, writes, or ADC-start methods.
} bq;

struct BoardConfigContainer {
  bool bqInitialized = true;
  bool wish = true;
  bool getMPPTEnabled() const { return wish; }
  void getMpptDiagnostics(char* buffer, uint32_t bufferSize);
};

// The runner inserts the production method here, without rewriting it.
@METHOD@

static void set16(uint8_t reg, unsigned value) {
  bq.registers[reg] = value >> 8;
  bq.registers[reg + 1] = value & 0xFF;
}

static std::string snapshot(BoardConfigContainer& cfg) {
  char buffer[100];
  memset(buffer, '!', sizeof(buffer));
  cfg.getMpptDiagnostics(buffer, sizeof(buffer));
  assert(memchr(buffer, '\0', sizeof(buffer)) != nullptr);
  return buffer;
}

int main() {
  BoardConfigContainer cfg;

  // Na-ion before the change: configured MPPT on, hardware off, VSYS minimum active.
  bq.registers[0x00] = 1;
  set16(0x01, 390);
  set16(0x03, 20);
  bq.registers[0x05] = 37;
  bq.registers[0x1B] = 0x08;
  bq.registers[0x1E] = 0x10;
  assert(snapshot(cfg) ==
         "MPPT:cfg=1/hw=0 VSYSMIN:2750mV VINDPM:3700mV VSYS_MIN:1 PG:1 CELL:1S ICHG:200mA VREG:3900mV");

  // Expected RC state with the same cell and an enabled hardware MPPT loop.
  bq.registers[0x00] = 0;
  bq.registers[0x05] = 60;
  bq.registers[0x15] = 1;
  bq.registers[0x1E] = 0;
  assert(snapshot(cfg) ==
         "MPPT:cfg=1/hw=1 VSYSMIN:2500mV VINDPM:6000mV VSYS_MIN:0 PG:1 CELL:1S ICHG:200mA VREG:3900mV");

  // LTO2S: register byte order and masks, with unrelated/reserved bits set.
  // Config and hardware can disagree in either direction.
  cfg.wish = false;
  bq.registers[0x00] = 0xC0;
  set16(0x01, 0xF800 | 540);
  set16(0x03, 0xFE00 | 40);
  bq.registers[0x0A] = 0x7F;
  bq.registers[0x15] = 0xFF;
  bq.registers[0x1B] = 0xF7;
  bq.registers[0x1E] = 0xEF;
  assert(snapshot(cfg) ==
         "MPPT:cfg=0/hw=1 VSYSMIN:2500mV VINDPM:6000mV VSYS_MIN:0 PG:0 CELL:2S ICHG:400mA VREG:5400mV");

  // Even out-of-range/reserved encodings fit in a CLI reply without truncation.
  cfg.wish = true;
  memset(bq.registers, 0xFF, sizeof(bq.registers));
  const auto maximum = snapshot(cfg);
  assert(maximum ==
         "MPPT:cfg=1/hw=1 VSYSMIN:18250mV VINDPM:25500mV VSYS_MIN:1 PG:1 CELL:4S ICHG:5110mA VREG:20470mV");
  assert(maximum.size() == 95);

  // Check every register read failure, including both bytes of each 16-bit field.
  // No partial or zero-valued success response may escape.
  const uint8_t expected_reads[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x0A, 0x15, 0x1B, 0x1E};
  for (unsigned i = 0; i < sizeof(expected_reads); ++i) {
    bq.reads.clear();
    bq.fail = expected_reads[i];
    char expected[32];
    snprintf(expected, sizeof(expected), "Err: BQ read %02X", static_cast<unsigned>(expected_reads[i]));
    assert(snapshot(cfg) == expected);
    assert(bq.reads == std::vector<uint8_t>(expected_reads, expected_reads + i + 1));
  }
  bq.fail = -1;

  // Truncated callers still receive a terminated string and never lose canaries.
  for (unsigned capacity = 1; capacity <= 100; ++capacity) {
    char guarded[102];
    memset(guarded, '#', sizeof(guarded));
    cfg.getMpptDiagnostics(guarded + 1, capacity);
    assert(guarded[0] == '#');
    assert(guarded[capacity + 1] == '#');
    assert(memchr(guarded + 1, '\0', capacity) != nullptr);
    assert(std::string(guarded + 1) == maximum.substr(0, capacity - 1));
  }

  bq.reads.clear();
  char untouched = '#';
  cfg.getMpptDiagnostics(&untouched, 0);
  cfg.getMpptDiagnostics(nullptr, 100);
  assert(untouched == '#');
  assert(bq.reads.empty());

  cfg.bqInitialized = false;
  assert(snapshot(cfg) == "BQ not init");
  assert(bq.reads.empty());
  puts("PASS: MR2 mpptdiag decode, masks, 95-character bound, all I2C failures, buffer guards");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX") or shutil.which("g++"))
    args = parser.parse_args()
    if not args.cxx:
        parser.error("g++ unavailable; pass --cxx with a C++17 compiler")

    root = Path(__file__).resolve().parents[2]
    source = (root / "variants/inhero_mr2/BoardConfigContainer.cpp").read_text(encoding="utf-8")
    signature = "void BoardConfigContainer::getMpptDiagnostics("
    start = source.index(signature)
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    method = source[start:end]

    cli = (root / "variants/inhero_mr2/helpers/CliCommands.cpp").read_text(encoding="utf-8")
    branch_start = cli.index('strcmp(cmd, "mpptdiag") == 0')
    branch_end = cli.index("} else", branch_start)
    branch = cli[branch_start:branch_end]
    assert "cfg.getMpptDiagnostics(diagBuffer, sizeof(diagBuffer));" in branch
    assert "char diagBuffer[100];" in branch
    assert 'snprintf(reply, maxlen, "%s", diagBuffer);' in branch

    with tempfile.TemporaryDirectory(prefix="mr2-mpptdiag-") as directory:
        tmp = Path(directory)
        cpp = tmp / "mpptdiag.cpp"
        exe = tmp / ("mpptdiag.exe" if os.name == "nt" else "mpptdiag")
        cpp.write_text(HARNESS.replace("@METHOD@", method), encoding="utf-8")
        subprocess.run([args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
