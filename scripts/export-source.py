#!/usr/bin/env python3
"""Export current public source, including uncommitted V2 additions, without local secrets/builds."""
import argparse
import hashlib
import json
import subprocess
import zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def source_files():
    if (ROOT/'.git').exists():
        names=subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=ROOT).decode().split('\0')
    else:
        manifest=json.loads((ROOT/'SOURCE-MANIFEST.json').read_text())
        names=list(manifest['files'])
        for name,checksum in manifest['files'].items():
            if Path(name).is_absolute() or '..' in Path(name).parts or hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=checksum:
                raise ValueError('Invalid source snapshot: '+name)
    result=[]
    for name in sorted(set(filter(None,names))):
        p=ROOT/name
        if not p.is_file():continue
        if p.is_symlink() or name.endswith(('.gltb','.bin','.log')) or p.name in ['credentials.h','web_credentials.h','app_config.h','sdkconfig','sdkconfig.old','AGENTS.md','PUBLICATION.md'] or any(part in ['.git','build','dist','.test-output','__pycache__'] for part in p.relative_to(ROOT).parts):
            raise ValueError('Unexpected private/generated file in source export: '+name)
        result.append(name)
    if 'main/web/ttn-position-decoder.js' not in result:raise ValueError('Decoder missing')
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('output',type=Path);a=parser.parse_args()
    a.output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(a.output,'x',zipfile.ZIP_DEFLATED) as z:
        names=source_files()
        for name in names:z.write(ROOT/name,'gnss-live-tracker/'+name)
        manifest={'format':1,'files':{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in names}}
        z.writestr('gnss-live-tracker/SOURCE-MANIFEST.json',json.dumps(manifest,indent=2)+'\n')
    print(str(a.output))
