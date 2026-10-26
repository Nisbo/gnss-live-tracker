#!/usr/bin/env python3
"""Build, host-test and package a clean source export. Never flashes hardware."""
import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--idf-path',type=Path,default=os.environ.get('IDF_PATH'))
    parser.add_argument('--idf-tools-path',type=Path,default=os.environ.get('IDF_TOOLS_PATH'))
    parser.add_argument('--node',type=Path,default=shutil.which('node'))
    parser.add_argument('--work-dir',type=Path)
    a=parser.parse_args()
    if not a.idf_path or not (a.idf_path/'export.sh').is_file() or not a.node:raise SystemExit('Provide ESP-IDF v5.4 and Node.js; see docs/BUILD.md')
    work=a.work_dir.resolve() if a.work_dir else Path(tempfile.mkdtemp(prefix='gnss-source-'))
    if a.work_dir:work.mkdir(parents=True,exist_ok=False)
    archive=work/'source.zip'
    subprocess.run([sys.executable,str(ROOT/'scripts/export-source.py'),str(archive)],check=True)
    with zipfile.ZipFile(archive) as z:z.extractall(work)
    source=work/'gnss-live-tracker';env=os.environ.copy()
    env['PATH']=str(Path(sys.executable).parent)+os.pathsep+str(a.node.resolve().parent)+os.pathsep+env['PATH']
    if a.idf_tools_path:env['IDF_TOOLS_PATH']=str(a.idf_tools_path.resolve())
    actions=['idf.py build','python scripts/test-v2.py','python scripts/package-release.py']
    script='set -euo pipefail\n. '+shlex.quote(str(a.idf_path.resolve()/'export.sh'))+'\n'+'\n'.join(actions)
    subprocess.run(['bash','-c',script],cwd=source,env=env,check=True)
    report={'source':str(source),'clean_source_build':True,'host_tests_passed':True,'distribution':str(source/'dist'),'device_accessed':False}
    (work/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
