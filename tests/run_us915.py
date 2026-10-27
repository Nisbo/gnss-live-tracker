from test_support import ROOT, CJSON, CC, tmp
"""Reproducible host-only US915 checks; never connects to a tracker or radio."""
from pathlib import Path
import json, os, shutil, subprocess, sys, re
root=ROOT
os.chdir(root)
node=shutil.which('node')
if not node: raise SystemExit('Node.js is required for browser/decoder tests')
if not CJSON or not Path(CJSON,'cJSON.c').is_file(): raise SystemExit('Set IDF_PATH to ESP-IDF v5.4 before running host tests')
cc=CC;tests='tests';main='main';mac='components/loramac';cjson=CJSON
version=re.search(r'set\(PROJECT_VER "([^"]+)"',Path('CMakeLists.txt').read_text()).group(1)
reports=[]
def run(name,args):
 p=subprocess.run(args,text=True,capture_output=True);output=(p.stdout+p.stderr).strip();reports.append({'test':name,'passed':p.returncode==0,'output':output})
 print(name+': '+('PASS' if not p.returncode else 'FAIL'),flush=True)
 if p.returncode:print(output);raise RuntimeError(name)
def compile_test(name,sources,flags=()):
 binary=tmp('tracker-')+name+'-test'
 run(name+' build',[cc,'-std=c11','-I'+main,'-I'+mac,'-I'+mac+'/loramac','-I'+cjson,*flags,*sources,'-lm','-o',binary]);return binary
try:
 region_flags=['-DREGION_US915','-include',tests+'/region_stubs/esp_attr.h','-I'+tests+'/region_stubs','-I'+mac+'/loramac/region','-Icomponents/sx1262']
 b=compile_test('us915',[tests+'/test_us915.c',mac+'/loramac/region/RegionUS915.c',mac+'/loramac/region/RegionCommon.c'],region_flags);run('production US915 MAC',[b]);run('MAC budget/validation',[sys.executable,tests+'/test_mac_budget.py'])
 run('all regional modules',[sys.executable,tests+'/run_regions.py']);run('production regional session persistence',[sys.executable,tests+'/test_regional_session.py'])
 for name in ['gnss','compact','profiles','small','lora_messages']:
  sources=[tests+'/test_'+name+'.c']+([] if name=='lora_messages' else [main+'/gnss_nmea.c']);b=compile_test(name,sources)
  if name=='lora_messages':run(name,[b])
 stub_flags=['-I'+tests+'/backup_stubs','-I'+tests+'/region_stubs']
 b=compile_test('radio-server',[tests+'/test_radio_server.c',cjson+'/cJSON.c'],stub_flags);run('radio HTTP/NVS',[b])
 b=compile_test('backup-server',[tests+'/test_backup_server.c',main+'/track_ring.c',main+'/sat_ring.c',main+'/tracker_timezones.c',cjson+'/cJSON.c'],stub_flags);run('backup validation/restore in simulated flash',[b])
 for name in ['small','profiles','decoder','radio','adr','dashboard','web_release','backup','credentials','firmware']:
  run('browser/decoder '+name,[node,tests+'/test_'+name+'.cjs'])
 for name in ['dashboard','adr']:
  run('JS syntax '+name,[node,'--check',main+'/web/'+name+'.js'])
finally:
 (root/'.test-output').mkdir(exist_ok=True)
 path=root/('.test-output/v'+version.replace('.','').lstrip('0')+'-software-test.json');path.write_text(json.dumps({'version':version,'hardware_rf_used':False,'tests':reports},indent=2)+'\n')
