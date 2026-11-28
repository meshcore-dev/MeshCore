# PlatformIO builder for lolren's nrf54-arduino-core, using the same tools as its platform.txt.
import hashlib
import json
import os
import shutil
import ssl
import sys
import tarfile
import urllib.request

from SCons.Script import AlwaysBuild, Builder, Default, DefaultEnvironment

env = DefaultEnvironment()
board = env.BoardConfig()

CORE_VERSION = "1.0.20"
CORE_URL = (
    "https://github.com/lolren/nrf54-arduino-core/releases/download/v1.0.20/"
    "nrf54l15clean-1.0.20-f9d133328f2a.tar.bz2"
)
CORE_SHA256 = "f9d133328f2ad89a77bef7bf5262d6faf1a59b3e8befbf335348fc6d106ebaf5"

# andyshinn's nRF54_Bootloader (BLE and serial DFU), plus the s145 SoftDevice it needs for BLE DFU.
# This must stay a single-bank build: DFU then only overwrites the app, not InternalFS above it.
BOOTLOADER_VERSION = "0.5.0"
BOOTLOADER_FILES = {
    "bootloader.hex": (
        "https://github.com/andyshinn/nRF54_Bootloader/releases/download/v0.5.0/"
        "xiao_nrf54lm20a_bootloader.hex",
        "2926e8c5d2a8d50c65410192b47bb446f97863a02f9a55ba5006aaeb9b98befa",
    ),
    "softdevice.hex": (
        "https://raw.githubusercontent.com/andyshinn/nRF54_Bootloader/v0.5.0/"
        "lib/softdevice/s145_nrf54lm20_10.0.1/s145_nrf54lm20_10.0.1_softdevice.hex",
        "d1d244958494b2717b7d3315526a7cb7783e0b0fd92f697cbfd186fd82feffd1",
    ),
}
# Bootloader settings page. Leaving it erased tells the bootloader the app was flashed over SWD,
# so it doesn't check the CRC saved by the last DFU.
SETTINGS_PAGE = 0x1D1000


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def download(url, path):
    """Downloads url to path, using certifi's CA bundle when it's there (as PlatformIO does)."""
    try:
        import certifi
        ctx = ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        ctx = None
    with urllib.request.urlopen(url, timeout=60, context=ctx) as r, open(path + ".tmp", "wb") as f:
        shutil.copyfileobj(r, f)
    os.replace(path + ".tmp", path)


def core_dir():
    """Download and unpack the core, checking its SHA-256. PlatformIO can't install a .tar.bz2
    without a manifest."""
    root = os.path.join(env.subst("$PROJECT_PACKAGES_DIR"), "framework-arduino-nrf54l")
    manifest = os.path.join(root, ".meshcore.json")
    try:
        with open(manifest) as f:
            if json.load(f).get("sha256") == CORE_SHA256:
                return root
    except (OSError, ValueError):
        pass
    print("Fetching nrf54-arduino-core %s" % CORE_VERSION)
    archive = root + ".tar.bz2"
    staging = root + ".tmp"
    try:
        download(CORE_URL, archive)
        if sha256(archive) != CORE_SHA256:
            sys.exit("nrf54-arduino-core %s: SHA-256 mismatch, refusing it" % CORE_VERSION)
        shutil.rmtree(staging, ignore_errors=True)
        with tarfile.open(archive) as t:
            if hasattr(tarfile, "data_filter"):
                t.extractall(staging, filter="data")
            else:
                t.extractall(staging)
        shutil.rmtree(root, ignore_errors=True)
        os.rename(os.path.join(staging, "nrf54l15clean-" + CORE_VERSION), root)
    finally:
        shutil.rmtree(staging, ignore_errors=True)
        if os.path.isfile(archive):
            os.remove(archive)
    with open(manifest + ".tmp", "w") as f:
        json.dump({"version": CORE_VERSION, "sha256": CORE_SHA256}, f)
    os.replace(manifest + ".tmp", manifest)
    # PlatformIO lists what's in the packages dir, so give it a manifest too if the core has none
    package = os.path.join(root, "package.json")
    if not os.path.isfile(package):
        with open(package, "w") as f:
            json.dump({"name": "framework-arduino-nrf54l", "version": CORE_VERSION}, f)
    return root


env["NRF54_CORE_DIR"] = core_dir()


def bootloader_file(name):
    root = os.path.join(env.subst("$PROJECT_PACKAGES_DIR"), "nrf54-bootloader-" + BOOTLOADER_VERSION)
    path = os.path.join(root, name)
    url, expected = BOOTLOADER_FILES[name]
    if os.path.isfile(path) and sha256(path) == expected:
        return path
    print("Fetching %s" % url)
    os.makedirs(root, exist_ok=True)
    download(url, path)
    if sha256(path) != expected:
        os.remove(path)
        sys.exit("%s: SHA-256 mismatch, refusing it" % url)
    return path


def read_hex(path):
    """Intel HEX data records as a list of (address, bytes)."""
    records, base = [], 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rec = bytes.fromhex(line[1:])
            kind, addr, data = rec[3], int.from_bytes(rec[1:3], "big"), rec[4:4 + rec[0]]
            if kind == 0:
                records.append((base + addr, data))
            elif kind in (2, 4):
                base = int.from_bytes(data, "big") << (4 if kind == 2 else 16)
    return records


def write_hex(path, records):
    def line(addr, kind, data):
        rec = bytes([len(data)]) + addr.to_bytes(2, "big") + bytes([kind]) + data
        return ":%s%02X\n" % (rec.hex().upper(), -sum(rec) & 0xFF)

    out, upper, end = [], None, 0
    for addr, data in sorted(records, key=lambda r: r[0]):
        if addr < end:
            sys.exit("%s: images overlap at 0x%X" % (path, addr))
        end = addr + len(data)
        while data:
            n = min(len(data), 0x10000 - (addr & 0xFFFF))  # a record can't cross a 64K boundary
            if addr >> 16 != upper:
                upper = addr >> 16
                out.append(line(0, 4, upper.to_bytes(2, "big")))
            out.append(line(addr & 0xFFFF, 0, data[:n]))
            addr, data = addr + n, data[n:]
    with open(path, "w") as f:
        f.writelines(out + [line(0, 1, b"")])


def merge_hex(target, source, env):
    """Merges the source images with an erased (0xFF) bootloader settings page."""
    records = [(SETTINGS_PAGE + i, b"\xff" * 32) for i in range(0, 0x1000, 32)]
    for s in source:
        records += read_hex(str(s))
    write_hex(str(target[0]), records)


env.Replace(
    AR="arm-none-eabi-ar",
    AS="arm-none-eabi-as",
    CC="arm-none-eabi-gcc",
    CXX="arm-none-eabi-g++",
    GDB="arm-none-eabi-gdb",
    OBJCOPY="arm-none-eabi-objcopy",
    RANLIB="arm-none-eabi-ranlib",
    SIZETOOL="arm-none-eabi-size",
    ARFLAGS=["rc"],
    SIZEPROGREGEXP=r"^(?:\.isr_vector|\.text|\.rodata|\.init_array|\.fini_array|\.ARM|\.data)\s+([0-9]+).*",
    SIZEDATAREGEXP=r"^(?:\.data|\.bss|\.noinit)\s+([0-9]+).*",
    SIZECHECKCMD="$SIZETOOL -A -d $SOURCES",
    SIZEPRINTCMD="$SIZETOOL -B -d $SOURCES",
    PROGNAME="firmware",
    PROGSUFFIX=".elf",
)
env.Append(
    BUILDERS=dict(
        ElfToHex=Builder(
            action=env.VerboseAction("$OBJCOPY -O ihex $SOURCES $TARGET", "Building $TARGET"),
            suffix=".hex",
        )
    )
)

target_elf = env.BuildProgram()
target_hex = env.ElfToHex(os.path.join("$BUILD_DIR", "${PROGNAME}"), target_elf)
env.Depends(target_hex, "checkprogsize")

# Build outputs:
#   firmware.zip        - DFU package (BLE OTA with the nRF DFU app, or serial)
#   firmware-merged.hex - bootloader + SoftDevice + app, for a new board over SWD
#   upload.hex          - app only, used by "pio run -t upload"
# --dev-type and --sd-req must match the bootloader's DFU_DEVICE_TYPE and the s145 10.0.1 FWID.
nrfutil = os.path.join(env.PioPlatform().get_package_dir("tool-adafruit-nrfutil"), "adafruit-nrfutil.py")
target_zip = env.Command(
    os.path.join("$BUILD_DIR", "${PROGNAME}.zip"), target_hex,
    env.VerboseAction('"$PYTHONEXE" "%s" dfu genpkg --dev-type 0x0054 --sd-req 0x310D '
                      '--application $SOURCE $TARGET' % nrfutil, "Building $TARGET"))
target_merged = env.Command(
    os.path.join("$BUILD_DIR", "${PROGNAME}-merged.hex"),
    [bootloader_file("softdevice.hex"), bootloader_file("bootloader.hex"), target_hex],
    env.VerboseAction(merge_hex, "Building $TARGET"))
target_upload = env.Command(
    os.path.join("$BUILD_DIR", "upload.hex"), target_hex, env.VerboseAction(merge_hex, "Building $TARGET"))


def nrf_ocd(erase, unlock):
    """Upload command for the core's bundled nrf_ocd (x86-64 Linux and Windows only)."""
    cmd = ['"%s"' % os.path.join(env["NRF54_CORE_DIR"], "tools", "nrf_ocd"),
           "-t", board.get("upload.target"), "-e", erase, "-R"]
    if unlock:
        cmd.append("--auto-unlock")  # only mass-erases the chip if it is locked
    if env.subst("$UPLOAD_PORT"):
        cmd += ["-p", '"$UPLOAD_PORT"']
    return " ".join(cmd + ["load", "$SOURCE"])


# "upload" doesn't erase (RRAM doesn't need it), so the bootloader and InternalFS are kept.
# "pio run -t bootloader" erases the chip and flashes bootloader + SoftDevice + app.
env.Replace(UPLOADCMD=nrf_ocd("none", False))
AlwaysBuild(env.Alias("upload", target_upload, env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")))
AlwaysBuild(env.Alias("bootloader", target_merged,
                      env.VerboseAction(nrf_ocd("chip", True),
                                        "Erasing and flashing bootloader, SoftDevice and $SOURCE")))

AlwaysBuild(env.Alias("size", target_elf, env.VerboseAction("$SIZEPRINTCMD", "Calculating size $SOURCE")))
Default([env.Alias("buildprog", [target_hex, target_zip, target_merged], target_hex)])
