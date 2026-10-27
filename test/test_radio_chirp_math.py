"""Compile the portable production chirp math, independent of radio hardware."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class RadioChirpMathTest(unittest.TestCase):
    def test_floor_budgets_rounding_and_profile_order(self):
        compiler=shutil.which('g++') or shutil.which('clang++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as folder:
            for stm in (False, True):
                with self.subTest(stm32_defined=stm):
                    exe=Path(folder)/('chirps-stm.exe' if stm else 'chirps.exe')
                    checks=[] if os.name=='nt' else ['-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all','-fno-pie','-no-pie']
                    done=subprocess.run([compiler,'-std=c++11','-O1','-g','-Wall','-Wextra',
                        *checks,*(['-DSTM32_PLATFORM'] if stm else []),'-I',str(ROOT/'src'),
                        str(ROOT/'test/fixtures/radio_profiles/chirp_math.cpp'),'-o',str(exe)],capture_output=True,text=True)
                    self.assertEqual(done.returncode,0,done.stderr)
                    done=subprocess.run([str(exe)],capture_output=True,text=True)
                    self.assertEqual(done.returncode,0,done.stderr)

if __name__=='__main__':unittest.main()
