"""Compile the real alert-copy method in an isolated buffer-boundary harness."""
import argparse
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--source-ref', help='Optional Git revision for a regression check')
args = parser.parse_args()

def source(name):
  if args.source_ref:
    return subprocess.check_output(['git', 'show', args.source_ref+':'+name], cwd=root).decode()
  return (root/name).read_text()

def method(text, signature):
  start = text.index(signature)
  first = text.index('{', start)
  depth = 1
  end = first+1
  while depth:
    depth += (text[end] == '{') - (text[end] == '}')
    end += 1
  return text[start:end]

header = source('examples/companion_radio/ui-new/UITask.h')
alert = re.search(r'char _alert\[\d+\];', header).group()
copy = method(source('src/helpers/TxtDataHelpers.cpp'), 'void StrHelper::strzcpy(')
show = method(source('examples/companion_radio/ui-new/UITask.cpp'), 'void UITask::showAlert(')
program = '''#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
uint32_t millis() { return 100; }
class StrHelper { public: static void strzcpy(char*, const char*, size_t); };
class UITask { public:
'''+alert+'''
unsigned long _alert_expiry = 0;
unsigned char after[4096];
void showAlert(const char*, int);
};
'''+copy+'\n'+show+'''
int main() {
  for (size_t length : {size_t(0),size_t(1),size_t(79),size_t(80),size_t(1024)}) {
    UITask task;
    memset(task.after, 0xA5, sizeof(task.after));
    std::string input(length, 'x');
    task.showAlert(input.c_str(), 1200);
    assert(strlen(task._alert) == (length < sizeof(task._alert) ? length : sizeof(task._alert)-1));
    for (auto value : task.after) assert(value == 0xA5);
    assert(task._alert_expiry == 1300);
  }
}
'''
output = root/'.pio/ui-new-alert-check'
output.mkdir(parents=True, exist_ok=True)
(output/'check.cpp').write_text(program)
binary = output/('check.exe' if os.name == 'nt' else 'check')
flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', *flags,
                str(output/'check.cpp'), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
print('PASS: actual showAlert/strzcpy preserve the 80-byte boundary and adjacent state')
