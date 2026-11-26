"""Verify switching code in the final firmware ELF. Run with PlatformIO's Python."""

import sys

from elftools.elf.elffile import ELFFile


FUNCTIONS = {
    "startRecv": "_ZN15RadioLibWrapper9startRecvEv",
    "beginReconfigure": "_ZN15RadioLibWrapper16beginReconfigureEv",
    "tuneProfile": "_ZN15RadioLibWrapper11tuneProfileEh",
}


def check(path):
    with open(path, "rb") as stream:
        elf = ELFFile(stream)
        arm = elf["e_machine"] == "EM_ARM"
        expected = ".data" if arm else ".iram0.text"
        symtab = elf.get_section_by_name(".symtab")
        if symtab is None:
            raise ValueError("missing symbol table")
        loads = [s for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]
        found = set()
        for symbol in symtab.iter_symbols():
            if symbol["st_info"]["type"] != "STT_FUNC" or not symbol["st_size"]:
                continue
            # LTO may emit specialized copies with a suffix.
            name = symbol.name.partition(".")[0]
            if name not in FUNCTIONS.values():
                continue
            section = elf.get_section(symbol["st_shndx"]).name
            start = symbol["st_value"] & ~1 if arm else symbol["st_value"]
            end = start + symbol["st_size"]
            if section != expected:
                raise ValueError(f"{symbol.name} at {start:#x} in {section}, expected {expected}")
            # ARM startup copies flash-backed .data to SRAM. ESP boot images
            # contain a load segment for IRAM separate from mapped flash text.
            if not any(s["p_vaddr"] <= start and end <= s["p_vaddr"] + s["p_filesz"]
                       and (not arm or (0x20000000 <= start < 0x21000000
                                        and s["p_paddr"] < 0x20000000))
                       for s in loads):
                raise ValueError(f"{symbol.name} at {start:#x} has no initialized RAM segment")
            found.add(name)
        missing = [label for label, name in FUNCTIONS.items() if name not in found]
        if missing:
            raise ValueError(f"missing functions: {', '.join(missing)}")
        print(f"{path}: {', '.join(FUNCTIONS)} verified in {expected}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: check_radio_retune_ram.py FIRMWARE.elf [...]")
    for path in sys.argv[1:]:
        try:
            check(path)
        except (OSError, ValueError) as error:
            sys.exit(f"{path}: {error}")
