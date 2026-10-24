#!/usr/bin/env python3
"""Exercise actual MyMesh queue methods with native doubles, without Arduino.

Requires an installed C++ compiler and Google Test sources; never installs them.
The small harness substitutes unrelated radio/filesystem/UI dependencies, while
the enqueue/drain/SYNC method bodies come verbatim from the current MyMesh.cpp.
This complements the board build; it is not a replacement for hardware tests.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', help='g++, clang++, or cl.exe in a developer shell')
    parser.add_argument('--gtest', type=Path,
                        default=repo / '.pio/libdeps/native/googletest')
    parser.add_argument('--asan', action='store_true')
    args = parser.parse_args()
    compiler = args.compiler or shutil.which('g++') or shutil.which('clang++') or shutil.which('cl')
    if not compiler:
        parser.error('No compiler found; supply --compiler (no installation is attempted).')
    gtest = args.gtest.resolve()
    if not (gtest / 'src/gtest-all.cc').is_file():
        gtest /= 'googletest'
    if not (gtest / 'src/gtest-all.cc').is_file():
        parser.error('Google Test sources not found; supply --gtest.')

    mesh = (repo / 'examples/companion_radio/MyMesh.cpp').read_text(encoding='utf-8')
    template = Path(__file__).with_suffix('.cpp.in').read_text(encoding='utf-8')
    # Fail clearly when these source landmarks change instead of silently testing
    # an obsolete hand-copied implementation of the queue.
    queue = mesh[mesh.index('bool MyMesh::Frame::isChannelMsg('):
                 mesh.index('float MyMesh::getAirtimeBudgetFactor(')]
    sync = mesh.split('} else if (cmd_frame[0] == CMD_SYNC_NEXT_MESSAGE) {', 1)[1]
    sync = sync.split('} else if (cmd_frame[0] == CMD_SET_RADIO_PARAMS)', 1)[0]
    fixtures = repo / 'test/test_mcotxt_delivery/fixtures.h'
    source = template.replace('@FIXTURES@', fixtures.as_posix())
    source = source.replace('@QUEUE@', queue).replace('@SYNC@', sync)
    sources = [repo / 'src/helpers/Base91.cpp',
               *sorted((repo / 'src/helpers/mcotxt').glob('*.cpp')),
               repo / 'examples/companion_radio/MCOtxtDelivery.cpp',
               gtest / 'src/gtest-all.cc']
    with tempfile.TemporaryDirectory(prefix='mcotxt-queue-') as temporary:
        work = Path(temporary)
        generated = work / 'queue.cpp'
        generated.write_text(source, encoding='utf-8')
        msvc = Path(compiler).name.lower() in ('cl', 'cl.exe')
        executable = work / ('queue.exe' if msvc else 'queue')
        includes = [repo / 'src', gtest, gtest / 'include']
        if msvc:
            command = [compiler, '/nologo', '/std:c++17', '/EHsc', '/utf-8', '/Od', '/Zi']
            command += ['/I' + str(path) for path in includes]
            if args.asan:
                command += ['/fsanitize=address']
            command += [*map(str, sources), str(generated), '/Fe:' + str(executable)]
        else:
            command = [compiler, '-std=c++17', '-pthread', '-O1', '-g']
            command += ['-I' + str(path) for path in includes]
            if args.asan:
                command += ['-fsanitize=address', '-fno-omit-frame-pointer']
            command += [*map(str, sources), str(generated), '-o', str(executable)]
        subprocess.run(command, cwd=work, check=True)
        subprocess.run([str(executable), '--gtest_color=no'], cwd=work, check=True)


if __name__ == '__main__':
    main()
