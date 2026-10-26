#!/usr/bin/env python3
"""Run supported host-only V2 checks. No device/network/USB commands are used."""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from test_support import CJSON, TMP, tmp

os.chdir(ROOT)
node=shutil.which('node')
cc=os.environ.get('CC','cc')
if not node or not shutil.which(cc):raise SystemExit('Install Node.js and a C compiler; see docs/BUILD.md')
if not CJSON or not Path(CJSON,'cJSON.c').is_file():raise SystemExit('Set IDF_PATH to ESP-IDF v5.4 (including its cJSON submodule)')
checks=[]
def run(name,command,output=None):
    result=subprocess.run(command,text=True,capture_output=True)
    checks.append({'name':name,'passed':result.returncode==0})
    if output and result.returncode==0:Path(output).write_text(result.stdout)
    print(name+': '+('PASS' if result.returncode==0 else 'FAIL'),flush=True)
    if result.returncode:
        print(result.stdout+result.stderr)
        raise RuntimeError(name)

def compile_run(name,sources,flags=(),output=None):
    includes=['main','components/loramac','components/loramac/loramac','components/st7735','components/sx1262',CJSON]
    binary=tmp('tracker-'+name+'-test')
    run(name+' build',[cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations',*['-I'+p for p in includes],*flags,*sources,'-lm','-o',binary])
    run(name,[binary],output)

try:
    run('regional / MAC / backup / decoder suite',[sys.executable,'tests/run_us915.py'])
    main='main/';tests='tests/';cj=str(Path(CJSON,'cJSON.c'))
    for name in ['gnss_quality','gnss_time']:
        compile_run(name,[tests+'test_'+name+'.c',main+'gnss_nmea.c'])
    for name in ['track_ring','track_merge']:
        compile_run(name,[tests+'test_'+name+'.c',main+'track_ring.c'])
    for name in ['sat_ring','sat_merge','sat_recent']:
        compile_run(name,[tests+'test_'+name+'.c',main+'sat_ring.c'])
    compile_run('sat_import',[tests+'test_sat_import.c',main+'sat_import.c',cj])
    for name in ['credentials_policy','radio_policy','wifi_policy','mqtt_policy','backup_policy','display_policy']:
        compile_run(name,[tests+'test_'+name+'.c',main+'tracker_timezones.c',cj])
    compile_run('adr_history',[tests+'test_adr_history.c','components/loramac/adr_history.c'],['-I'+tests+'backup_stubs'])
    compile_run('status_cache',[tests+'test_status_cache.c',main+'status_cache.c',main+'gnss_nmea.c'],['-pthread'],tmp('tracker-v100-status.json'))
    compile_run('display_server',[tests+'test_display_server.c',main+'gnss_nmea.c',main+'tracker_timezones.c',cj],['-Wno-sign-compare','-I'+tests+'display_stubs','-I'+tests+'backup_stubs','-I'+tests+'region_stubs'])
    for path in sorted((ROOT/tests).glob('test_*.cjs')):
        run('browser '+path.stem,[node,str(path)])
    for name in ['test_history_http.py','test_i18n_bundle.py','test_timezones.py']:
        run(name,[sys.executable,tests+name])
    run('release packaging policy',[sys.executable,tests+'test_release.py'])
finally:
    out=ROOT/'.test-output';out.mkdir(exist_ok=True)
    (out/'host-tests.json').write_text(json.dumps({'hardware_rf_used':False,'tests':checks},indent=2)+'\n')
print(str(len(checks))+' host checks passed; device untouched. Temporary test output: '+str(TMP))
