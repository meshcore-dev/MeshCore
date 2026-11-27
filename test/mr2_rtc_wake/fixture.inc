#include <array>
#include <cstdio>
#include <cstdlib>
#include <Wire.h>
#include "helpers/Rv3028Wake.h"

FakeWire Wire;
unsigned cases = 0;

void check(bool condition, const char* name, unsigned index = 0) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s [%u]\n", name, index);
    std::exit(1);
  }
}

void reset() {
  Wire = {};
  // Previously armed/expired timer makes ignored stop, interrupt-disable and
  // flag-clear writes observable, even if the final armed values would match.
  Wire.registers[0x0F] = 0x87;
  Wire.registers[0x10] = 0x10;
  Wire.registers[0x0E] = 0x08;
  Wire.registers[0x0A] = 0x33;
  Wire.registers[0x0B] = 0x03;
  Wire.registers[0x00] = 0x42; // Time/calendar must not be changed.
  Wire.registers[0x35] = 0x48; // CLKOUT and backup configuration stay untouched.
  Wire.registers[0x37] = 0x93;
}

void expectFailure(const char* name, unsigned index = 0) {
  ++cases;
  check(!inhero::configurePeriodicWake(60), name, index);
}

int main() {
  const std::array<std::array<uint16_t, 2>, 7> durations{{
      {{0, 1}}, {{1, 1}}, {{60, 60}}, {{256, 256}},
      {{4095, 4095}}, {{4096, 4095}}, {{65535, 4095}}
  }};
  for (const auto& duration : durations) {
    reset();
    const auto before = Wire.registers;
    ++cases;
    check(inhero::configurePeriodicWake(duration[0]), "valid duration", duration[0]);
    check(Wire.registers[0x0A] == (duration[1] & 0xFF) &&
          Wire.registers[0x0B] == (duration[1] >> 8), "timer preset", duration[0]);
    check(Wire.registers[0x0F] == 0x07 && Wire.registers[0x10] == 0x10 &&
          (Wire.registers[0x0E] & 0x08) == 0 && Wire.starts == 1,
          "fresh single-shot timer armed", duration[0]);
    for (unsigned reg = 0; reg < before.size(); ++reg) {
      if (reg == 0x0A || reg == 0x0B || reg == 0x0E || reg == 0x0F || reg == 0x10) continue;
      check(Wire.registers[reg] == before[reg], "unrelated register changed", reg);
    }
  }

  reset();
  check(inhero::configurePeriodicWake(60), "baseline");
  const unsigned transactions = Wire.transfers;
  const unsigned bytes = Wire.queuedBytes;
  const unsigned writes = Wire.dataWrites;
  const auto readRegisters = Wire.readRegisters;
  check(transactions > writes && !readRegisters.empty(), "verification reads present");

  for (unsigned i = 1; i <= transactions; ++i) {
    reset();
    Wire.nackAt = i;
    expectFailure("NACK on write or read selection", i);
  }
  for (unsigned i = 1; i <= bytes; ++i) {
    reset();
    Wire.queueFailureAt = i;
    expectFailure("queue failure on any address/data byte", i);
  }
  for (unsigned i = 1; i <= writes; ++i) {
    reset();
    Wire.dropWriteAt = i;
    expectFailure("ACK but write ignored", i);
  }
  for (unsigned i = 1; i <= readRegisters.size(); ++i) {
    for (const auto& counts : std::array<std::array<uint8_t, 2>, 4>{{
             {{0, 0}}, {{1, 0}}, {{0, 1}}, {{2, 2}}}}) {
      reset();
      Wire.badReadAt = i;
      Wire.readReported = counts[0];
      Wire.readDelivered = counts[1];
      expectFailure("short or inconsistent verification read", i);
      check(Wire.available() == 0, "failed read buffer drained", i);
    }
    reset();
    Wire.corruptReadAt = i;
    expectFailure("incorrect register readback / TF still set", i);
  }

  reset();
  Wire.absent = true;
  expectFailure("RTC absent");
  reset();
  Wire.reservedReadBits = true;
  ++cases;
  check(inhero::configurePeriodicWake(60), "reserved bits do not reject valid timer");

  // Two successful calls must start two fresh countdowns, including when the
  // first timer is still active. This also guards stop-before-rearm behavior.
  ++cases;
  check(inhero::configurePeriodicWake(60) && Wire.starts == 2, "rearm running timer");
  std::printf("PASS: %u RTC wake cases (durations, all %u transfers, %u queued bytes, "
              "%u writes, %zu verification reads, absent RTC and rearm)\n",
              cases, transactions, bytes, writes, readRegisters.size());
  return 0;
}
