Import('env')
import os

# Bosch has a goof in their PlatformIO packaging making linking fail.
# The BSEC library's extra_script.py selects cortex-m4/libalgobsec.a (soft-float ABI).
# nRF52840 compiles with -mfloat-abi=hard, requiring the fpv4-sp-d16-hard blob.
# Workaround to prepend the hard-float path so the linker finds it before the 
# soft-float one.
bsec_hard = os.path.join(
    env.subst('$PROJECT_DIR'),
    '.pio', 'libdeps', env.subst('$PIOENV'),
    'BSEC Software Library', 'src',
    'cortex-m4', 'fpv4-sp-d16-hard'
)
env.Prepend(LIBPATH=[bsec_hard])