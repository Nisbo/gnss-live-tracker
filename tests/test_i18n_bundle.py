"""Validate the exact offline catalog shipped in the application, without device access."""
from pathlib import Path
import gzip
import importlib.util
import subprocess

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('bundle',root/'tools/generate_i18n_bundle.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
web=root/'main/web';packed=module.bundle(web)
assert packed==module.bundle(web)
expected=b'\n'.join((web/(name+'.js')).read_bytes() for name in module.CATALOGS)
assert gzip.decompress(packed)==expected
assert len(packed)<len(expected)*0.5
# A generated source dictionary must agree with its editable JSON catalog.
ui=(web/'i18n-ui.js').read_bytes()
subprocess.run(['python3',str(root/'tools/generate_i18n_ui.py')],check=True)
assert ui==(web/'i18n-ui.js').read_bytes()
print(f'Offline i18n bundle: deterministic gzip, byte-exact decompression, generated UI catalog current; {len(expected)} -> {len(packed)} bytes: PASS')
