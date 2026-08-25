Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

import os
import sys

sys.path.insert(0, os.path.join(env["PROJECT_DIR"], "tools", "mota"))  # noqa: F821
import gen_firmware_identity as gen

project_dir = env["PROJECT_DIR"]  # noqa: F821
out_path = os.path.join(project_dir, "src", "helpers", "FirmwareIdentity.generated.cpp")
env_override = any(
    os.environ.get(key)
    for key in (
        "ENVYOS_FIRMWARE_VERSION",
        "ENVYOS_FIRMWARE_BUILD_DATE",
        "ENVYOS_MOTA_TARGET_ID",
    )
)

if not env_override and os.path.isfile(out_path):
    pass
else:
    version, build_date, target_id = gen.resolve_identity(project_dir, env["PIOENV"])  # noqa: F821
    gen.write_firmware_identity(out_path, version, build_date, target_id)
