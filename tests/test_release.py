"""Release helpers reject wrong images/layouts and export a complete public source tree."""
import importlib.util
import struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def load(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'scripts'/f'{name}.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
p=load('package-release');e=load('export-source')
b=bytearray(320);b[0]=0xe9;struct.pack_into('<H',b,12,9);struct.pack_into('<I',b,32,0xabcd5432);b[48:53]=b'1.2.3';b[80:80+len(p.PROJECT)]=p.PROJECT.encode();assert p.descriptor(b)=='1.2.3'
for offset in [0,12,32,80]:
    broken=b.copy();broken[offset]^=1
    try:p.descriptor(broken)
    except ValueError:pass
    else:raise AssertionError('invalid application accepted')
entries=[('nvs',0x9000,0x6000),('phy_init',0xf000,0x1000),('ota_0',0x10000,0x200000),('ota_1',0x210000,0x200000),('otadata',0x410000,0x2000),('sat_history',0x420000,0x80000),('track_import',0x4a0000,0x60000),('tracks',0x500000,0x300000)]
data=b''.join(struct.pack('<HBBII16sI',0x50aa,0,0,offset,size,name.encode(),0) for name,offset,size in entries);p.validate_partitions(data)
try:p.validate_partitions(data[:-32])
except ValueError:pass
else:raise AssertionError('missing partition accepted')
files=e.source_files();assert all((ROOT/name).is_file() for name in files)
for name in ['main/main.c','main/web/ttn-position-decoder.js','components/loramac/adr_history.c','components/loramac/region_policy.h','components/sx1262/sx1262_calibration.h','sdkconfig.defaults','tools/generate_i18n_bundle.py','LICENSE','NOTICE','docs/INSTALL.de.md','docs/INSTALL.en.md']:assert name in files,name
assert not any('credentials.h'==Path(name).name or '/build/' in name or name.endswith('.gltb') for name in files)
cmake=(ROOT/'main/CMakeLists.txt').read_text();assert '../../../ttn-position-decoder.js' not in cmake
readme=(ROOT/'README.md').read_text()
assert readme.startswith('> [!WARNING]')
assert 'Work in progress' in readme[:300] and 'Expect bugs' in readme[:350]
assert 'Have a Wireless Tracker V1.2?' in readme[:1200]
assert 'https://github.com/jbjardine/HTIT-Tracker-v1.2-forIDF' in readme[:800]
assert not any(Path(name).name in ['AGENTS.md','PUBLICATION.md'] for name in files)
assert not any(name.startswith('.githooks/') for name in files)
print('Release: S3/project validation, exact partition layout, self-contained public source inventory, private-file exclusion and candidate status PASS')
