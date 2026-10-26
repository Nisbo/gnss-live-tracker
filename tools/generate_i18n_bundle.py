"""Build a reproducible gzip catalog bundle; compression runs on the build host only."""
import argparse
import gzip
from pathlib import Path

CATALOGS = ('i18n', 'i18n-ui', 'i18n-inline', 'i18n-dashboard', 'i18n-sky', 'i18n-settings', 'i18n-server')

def bundle(web: Path) -> bytes:
    raw = b'\n'.join((web / (name + '.js')).read_bytes() for name in CATALOGS)
    return gzip.compress(raw, compresslevel=9, mtime=0)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--web', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = bundle(args.web)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
