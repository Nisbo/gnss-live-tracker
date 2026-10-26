"""Portable host-test paths; ESP-IDF is a separately installed build dependency."""
import os
import tempfile
from pathlib import Path

CC = os.environ.get('CC','cc')
ROOT = Path(__file__).resolve().parents[1]
TMP = Path(os.environ.get('TRACKER_TEST_TMP') or tempfile.mkdtemp(prefix='gnss-tracker-tests-'))
os.environ['TRACKER_TEST_TMP'] = str(TMP)
TMP.mkdir(parents=True, exist_ok=True)
_idf = os.environ.get('IDF_PATH')
CJSON = str(Path(_idf) / 'components/json/cJSON') if _idf else ''

def tmp(name):
    return str(TMP / name)
