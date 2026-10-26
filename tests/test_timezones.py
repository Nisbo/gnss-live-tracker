from test_support import ROOT, CJSON, tmp
"""Compare compiled flash lookups against source IANA data, including boundaries."""
import ctypes,subprocess,json,calendar
from pathlib import Path
from datetime import datetime,timezone
from zoneinfo import ZoneInfo
root=Path(__file__).resolve().parents[1];lib=tmp('tracker-timezones.so')
subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror',str(root/'main/tracker_timezones.c'),'-o',lib],check=True)
c=ctypes.CDLL(lib);c.tracker_timezone_offset.argtypes=[ctypes.c_char_p,ctypes.c_int64,ctypes.POINTER(ctypes.c_int)];c.tracker_timezone_offset.restype=ctypes.c_bool
# Catalog extracted from generated public JS; it is exactly the firmware catalog.
s=(root/'main/web/timezones.js').read_text();names=json.loads(s.split('const names=',1)[1].split(';',1)[0]);count=0
for name in names:
 z=ZoneInfo(name)
 for year in range(2020,2101):
  for month in [1,4,7,10]:
   t=int(datetime(year,month,15,12,tzinfo=timezone.utc).timestamp());out=ctypes.c_int();assert c.tracker_timezone_offset(name.encode(),t,ctypes.byref(out));expected=int(datetime.fromtimestamp(t,timezone.utc).astimezone(z).utcoffset().total_seconds()/60);assert out.value==expected,(name,t,out.value,expected);count+=1
# European, US, southern hemisphere, half-hour DST and Ramadan boundaries.
for name in ['Europe/Berlin','America/New_York','Australia/Lord_Howe','Pacific/Chatham','Africa/Casablanca']:
 z=ZoneInfo(name);previous=None
 for t in range(1767225600,1798761600,3600):
  expected=int(datetime.fromtimestamp(t,timezone.utc).astimezone(z).utcoffset().total_seconds()/60)
  if expected!=previous:
   for at in [t-1,t,t+1]:
    out=ctypes.c_int();assert c.tracker_timezone_offset(name.encode(),at,ctypes.byref(out));assert out.value==int(datetime.fromtimestamp(at,timezone.utc).astimezone(z).utcoffset().total_seconds()/60);count+=1
  previous=expected
out=ctypes.c_int();assert not c.tracker_timezone_offset(b'Invalid/Zone',1792890000,ctypes.byref(out));assert not c.tracker_timezone_offset(b'UTC',0,ctypes.byref(out));assert not c.tracker_timezone_offset(b'UTC',4133980800,ctypes.byref(out))
print(json.dumps({'zones':len(names),'offset_comparisons':count,'boundaries_and_invalid_input':'PASS'}))
