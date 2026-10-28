"""Exercise the actual ui-new timing expressions with a 32-bit clock."""
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
  end, depth = first+1, 1
  while depth:
    depth += (text[end] == '{') - (text[end] == '}')
    end += 1
  return text[start:end]

text = source('examples/companion_radio/ui-new/UITask.cpp')
loop = method(text, 'void UITask::loop()')
refresh = re.search(r'if \((.*_next_refresh.*) && curr\) \{', loop).group(1)
alert = re.search(r'if \((.*)\) \{  // render alert popup', loop).group(1)
off = re.search(r'if \((.*_auto_off.*)\) \{', loop).group(1)
shutdown = re.search(r'while \((.*)\)\s*buzzer.loop\(\)', method(text, 'void UITask::shutdown(')).group(1)
show = method(text, 'void UITask::showAlert(')
copy = method(source('src/helpers/TxtDataHelpers.cpp'), 'void StrHelper::strzcpy(')
forced = re.findall(r'_next_refresh = (\d+);[^\n]*trigger refresh', text)
forced += re.findall(r'_next_refresh = (\d+);', method(text, 'void UITask::setCurrScreen('))
schedule = re.search(r'_next_refresh = millis\(\) \+ delay_millis;[^}]*', loop).group()
program = '''#include <cassert>
#include <cstdint>
#include <cstring>
uint32_t now = 0;
uint32_t millis() { return now; }
class StrHelper { public: static void strzcpy(char*, const char*, size_t); };
class UITask { public: char _alert[80]; uint32_t _alert_expiry=0; void showAlert(const char*,int); };
'''+copy+'\n'+show+'''
struct Buzzer { bool isPlaying() { return true; } } buzzer;
bool refresh(uint32_t t,uint32_t _next_refresh) { now=t; return '''+refresh+'''; }
bool alert(uint32_t t,uint32_t _alert_expiry) { now=t; return '''+alert+'''; }
bool off(uint32_t t,uint32_t _auto_off) { now=t; return '''+off+'''; }
bool waiting(uint32_t t,uint32_t buzzer_timer) { now=t; return '''+shutdown+'''; }
uint32_t schedule(uint32_t t,uint32_t delay_millis) { now=t; uint32_t _next_refresh;
'''+schedule+'''
return _next_refresh; }
int main() {
  assert(waiting(100,100));
  assert(waiting(2499,0));
  assert(!waiting(2500,0));
  assert(waiting(0x20u,0xFFFFFFF0u));
  assert(!waiting(uint32_t(0xFFFFFFF0u+2500u),0xFFFFFFF0u));
  for(uint32_t deadline : {32u,0x80000020u,0xFFFFFFF0u}) {
    assert(!refresh(uint32_t(deadline-1),deadline));
    assert(refresh(deadline,deadline));
    assert(refresh(uint32_t(deadline+1),deadline));
    assert(!off(uint32_t(deadline-1),deadline));
    assert(off(deadline,deadline));
    assert(off(uint32_t(deadline+1),deadline));
    assert(alert(uint32_t(deadline-1),deadline));
    assert(!alert(deadline,deadline));
    assert(!alert(uint32_t(deadline+1),deadline));
  }
  assert(!refresh(0xFFFFFFF0u,0x20u));
  assert(!off(0xFFFFFFF0u,0x20u));
  assert(alert(0xFFFFFFF0u,0x20u));
  for(uint32_t t : {0u,100u,0x80000080u,0xFFFFFFF0u}) {
    for(uint32_t forced : {'''+','.join(v+'u' for v in forced)+'''}) assert(refresh(t,forced));
    assert(!alert(t,0));
  }
  UITask task;
  now=0xFFFFFF9Cu; task.showAlert("test",100);
  assert(task._alert_expiry != 0);
  assert(alert(now,task._alert_expiry));
  assert(alert(0,task._alert_expiry));
  assert(!alert(1,task._alert_expiry));
  assert(schedule(0xFFFFFF9Cu,100)==1);
}
'''
# initializer_list is needed by the harness's case tables, not by the firmware.
program = '#include <initializer_list>\n'+program
output = root/'.pio/ui-new-timing-check'
output.mkdir(parents=True, exist_ok=True)
(output/'check.cpp').write_text(program)
binary = output/('check.exe' if os.name == 'nt' else 'check')
flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
subprocess.run([os.environ.get('CXX','g++'), '-std=c++17', '-Wall', '-Wextra', *flags,
                str(output/'check.cpp'), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
print('PASS: actual render, alert, auto-off and shutdown conditions across 32-bit wrap')
