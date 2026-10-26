#!/usr/bin/env python3
"""Package a V2 build for distribution. Never accesses USB, a tracker, or a network."""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = 'tracker_v2_lorawan_test'  # Kept for compatibility with existing OTA installations.
FLASH = 8 * 1024 * 1024

def descriptor(image):
    if len(image) < 288 or image[0] != 0xE9 or struct.unpack_from('<H', image, 12)[0] != 9 or struct.unpack_from('<I', image, 32)[0] != 0xABCD5432:
        raise ValueError('Not an ESP32-S3 application image')
    value = lambda start: image[start:start+32].split(b'\0')[0].decode('ascii')
    if value(80) != PROJECT:
        raise ValueError('Wrong application project')
    return value(48)

def validate_partitions(data):
    expected = {'nvs': (0x9000,0x6000), 'phy_init': (0xF000,0x1000), 'ota_0': (0x10000,0x200000), 'ota_1': (0x210000,0x200000), 'otadata': (0x410000,0x2000), 'sat_history': (0x420000,0x80000), 'track_import': (0x4A0000,0x60000), 'tracks': (0x500000,0x300000)}
    actual = {}
    for offset in range(0,len(data)-31,32):
        magic,kind,sub,address,size,name,flags = struct.unpack_from('<HBBII16sI',data,offset)
        if magic != 0x50AA:
            break
        actual[name.split(b'\0')[0].decode()] = (address,size)
    if actual != expected:
        raise ValueError('Unexpected V2 flash partition layout')

def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument('--build-dir', type=Path, default=ROOT/'build')
    args.add_argument('--output', type=Path, default=ROOT/'dist')
    a = args.parse_args(); build = a.build_dir.resolve()
    flash = json.loads((build/'flasher_args.json').read_text())
    if flash['extra_esptool_args']['chip'] != 'esp32s3' or flash['flash_settings'] != {'flash_mode':'dio','flash_size':'8MB','flash_freq':'80m'}:
        raise ValueError('Expected ESP32-S3 DIO / 80 MHz / 8 MiB build')
    app = (build/flash['app']['file']).read_bytes(); version = descriptor(app)
    configured = re.search(r'set\(PROJECT_VER "([^"]+)"', (ROOT/'CMakeLists.txt').read_text()).group(1)
    if version != configured or len(app) > 0x200000:
        raise ValueError('Stale build or application larger than the OTA slot')
    validate_partitions((build/flash['partition-table']['file']).read_bytes())
    if {int(k,0) for k in flash['flash_files']} != {0,0x8000,0x10000,0x410000}:
        raise ValueError('Unexpected flash offsets')
    label = 'gnss-live-tracker-v'+version
    dest = a.output.resolve()/label
    dest.mkdir(parents=True,exist_ok=False)
    ota = dest/(label+'.ota.bin'); ota.write_bytes(app)
    factory = dest/(label+'.factory.bin')
    command = [sys.executable,'-m','esptool','--chip','esp32s3','merge_bin','--output',str(factory),'--flash_mode','dio','--flash_freq','80m','--flash_size','8MB','--fill-flash-size','8MB']
    for offset,path in sorted(flash['flash_files'].items(),key=lambda item:int(item[0],0)):
        command.extend([offset,str(build/path)])
    subprocess.run(command,check=True)
    merged = factory.read_bytes()
    if len(merged) != FLASH or merged[0x10000:0x10000+len(app)] != app:
        raise ValueError('Factory application does not match the OTA application')
    for name in ['partition-table','otadata']:
        part = flash[name]; data = (build/part['file']).read_bytes(); offset = int(part['offset'],0)
        if merged[offset:offset+len(data)] != data:
            raise ValueError('Factory component mismatch: '+name)
    for start,end in [(0x9000,0xF000),(0x210000,0x410000),(0x420000,FLASH)]:
        if merged[start:end] != b'\xff'*(end-start):
            raise ValueError('Factory image includes nonempty user storage')
    files = {'main/web/ttn-position-decoder.js':'ttn-decoder.js', 'README.md':'README.md', 'LICENSE':'LICENSE', 'NOTICE':'NOTICE', 'docs/INSTALL.en.md':'INSTALL.en.md', 'docs/INSTALL.de.md':'INSTALL.de.md', 'docs/SECURITY.md':'SECURITY.md', 'docs/UPSTREAM.md':'UPSTREAM.md', 'docs/RELEASE-NOTES.md':'RELEASE-NOTES.md', 'docs/REGIONS.md':'docs/REGIONS.md', 'components/loramac/LICENSE.TXT':'licenses/LoRaMac-node.LICENSE.txt', 'main/web/satellite.LICENSE.txt':'licenses/satellite-js.LICENSE.txt'}
    for source,target in files.items():
        out = dest/('docs/'+Path(target).name if source.startswith('docs/') else target); out.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(ROOT/source,out)
    for name,source in [('AES','aes.c'),('CMAC','cmac.c')]:
        text=(ROOT/'components/loramac/loramac'/source).read_text()
        notice=re.match(r'\s*(/\*.*?\*/)',text,re.S)
        if not notice or 'Copyright' not in notice.group(1):
            raise ValueError('Crypto source notice missing')
        (dest/'licenses'/(name+'.NOTICE.txt')).write_text(notice.group(1)+'\n')
    for doc in ['BUILD.md','QUICKSTART.md','TESTS.md','TROUBLESHOOTING.md']:
        shutil.copyfile(ROOT/'docs'/doc,dest/'docs'/doc)
    description = json.loads((build/'project_description.json').read_text())
    idf = Path(description['idf_path']).resolve()
    if not (idf/'LICENSE').is_file():
        raise ValueError('ESP-IDF license missing')
    license_files = {idf/'LICENSE'}
    for component in description['build_component_paths']:
        component = Path(component).resolve()
        if not component.is_relative_to(idf):
            continue
        for candidate in component.rglob('*'):
            if candidate.is_file() and re.match(r'^(LICENSE|COPYING|NOTICE)([._-].*)?$', candidate.name, re.I):
                license_files.add(candidate)
    for source in sorted(license_files):
        out = dest/'licenses/esp-idf'/source.relative_to(idf)
        out.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,out)
    (dest/'START-HERE.txt').write_text('GNSS Live Tracker '+version+'\n\nFirst USB installation: read docs/INSTALL.en.md or docs/INSTALL.de.md, then select the .factory.bin on https://web.esphome.io/. This replaces ALL device settings and recordings.\nExisting installation: back up first; select ONLY .ota.bin in the tracker Firmware tab.\nThe shared ttn-decoder.js handles all packet profiles.\n\nEXPERIMENTAL PREVIEW: testers welcome. Bugs and incomplete features are expected. See docs/RELEASE-NOTES.md for known limitations.\n')
    manifest = {'name':'GNSS Live Tracker','version':version,'target':'Heltec Wireless Tracker V2 / ESP32-S3 / 8 MiB','validation':'release-candidate; clean USB install and full data restore pending','factory':{'file':factory.name,'offset':0,'replaces_all_user_data':True},'ota':{'file':ota.name,'preserves_user_data':True},'decoder':'ttn-decoder.js','radio':{'field_tested':['EU868'],'experimental':['US915','AU915','IN865','KR920','AS923-1','AS923-2','AS923-3','AS923-4','RU864']},'files':{}}
    for p in sorted(dest.rglob('*')):
        if p.is_file():manifest['files'][p.relative_to(dest).as_posix()]={'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
    (dest/'release.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (dest/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(dest).as_posix()+'\n' for p in sorted(dest.rglob('*')) if p.is_file()))
    archive = dest.parent/(dest.name+'.zip')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(dest.rglob('*')):
            if p.is_file():z.write(p,label+'/'+p.relative_to(dest).as_posix())
    print(json.dumps({'version':version,'directory':str(dest),'archive':str(archive),'factory_bytes':len(merged),'ota_bytes':len(app),'factory_and_ota_application_identical':True,'user_storage_blank':True},indent=2))

if __name__ == '__main__':
    main()
