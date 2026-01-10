#!/usr/bin/python3

# Adds PlatformIO post-processing to convert hex files to uf2 files

import os

Import("env")

firmware_hex = "${BUILD_DIR}/${PROGNAME}.hex"
uf2_file = os.environ.get("UF2_FILE_PATH", "${BUILD_DIR}/${PROGNAME}.uf2")

def create_uf2_action(source, target, env):
    uf2_cmd = " ".join(
        [
            '"$PYTHONEXE"',
            '"$PROJECT_DIR/bin/uf2conv/uf2conv.py"',
            '-f', '0xADA52840',
            '-c', firmware_hex,
            '-o', uf2_file,
        ]
    )
    env.Execute(uf2_cmd)

env.AddCustomTarget(
    name="create_uf2",
    dependencies=firmware_hex,
    actions=create_uf2_action,
    title="Create UF2 file",
    description="Use uf2conv to convert hex binary into uf2",
    always_build=True,
)