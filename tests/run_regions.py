from test_support import ROOT, CJSON, CC, tmp
"""Host-only multi-region suite. No tracker connection or transmission."""
from pathlib import Path
import subprocess,json
root=ROOT
mac='components/loramac';tests='tests';regions=['EU868','US915','AU915','IN865','KR920','AS923','RU864']
flags=[CC,'-std=c11',*['-DREGION_'+r for r in regions],'-include',tests+'/region_stubs/esp_attr.h','-I'+tests+'/region_stubs','-I'+tests+'/backup_stubs','-I'+mac,'-I'+mac+'/loramac/region','-Icomponents/sx1262','-Imain','-I'+CJSON]
sources=[tests+'/test_regions.c',mac+'/loramac/region/Region.c',mac+'/loramac/region/RegionCommon.c',*[mac+'/loramac/region/Region'+r+'.c' for r in regions]]
subprocess.run(flags+sources+['-lm','-o',tmp('tracker-regions-test')],cwd=root,check=True)
r=subprocess.run([tmp('tracker-regions-test')],cwd=root,text=True,capture_output=True);print(r.stdout+r.stderr);r.check_returncode()
