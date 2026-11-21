"""Native production-method harnesses for radio profile tests."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

def method(text, signature):
    """Extract a complete C++ method for execution in a small native fixture."""
    start = text.index(signature)
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]

class NativeTest(unittest.TestCase):
    def compile_run(self, source):
        with tempfile.TemporaryDirectory() as folder:
            cpp, exe = Path(folder) / "test.cpp", Path(folder) / "test.exe"
            cpp.write_text(source)
            flags = [] if os.name == "nt" else ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
            built = subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11",
                                    "-Wall", "-Wextra", *flags, str(cpp), "-o", str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stderr)
