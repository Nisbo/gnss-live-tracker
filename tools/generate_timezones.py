"""Build flash-only IANA offset timelines; no downloads or runtime timezone service.
Source: host tzdata. Re-run when updating timezone rules. Coverage 2020..2100.
"""
from pathlib import Path
from datetime import datetime,timezone,timedelta
from zoneinfo import ZoneInfo,available_timezones
import json
root=Path(__file__).resolve().parents[1]
start=int(datetime(2020,1,1,tzinfo=timezone.utc).timestamp());end=int(datetime(2101,1,1,tzinfo=timezone.utc).timestamp())
names=sorted(n for n in available_timezones() if '/' in n or n=='UTC')
sequences={};entries=[];records=[]
for name in names:
 z=ZoneInfo(name)
 def offset(t):return int(datetime.fromtimestamp(t,timezone.utc).astimezone(z).utcoffset().total_seconds()/60)
 old=offset(start);rows=[(0,old)];t=start
 while t<end:
  nxt=min(end,t+43200);value=offset(nxt)
  if value!=old:
   lo=t;hi=nxt
   while hi-lo>60:
    mid=((lo+hi)//120)*60
    if offset(mid)==old:lo=mid
    else:hi=mid
   rows.append(((hi-start)//60,value));old=value
  t=nxt
 key=tuple(rows)
 if key not in sequences:sequences[key]=(len(records),len(rows));records.extend(rows)
 entries.append((name,*sequences[key]))
version=Path('/usr/share/zoneinfo/+VERSION').read_text().strip()
lines=[f'/* Generated from IANA tzdata {version}; UTC offsets 2020..2100. See tools/generate_timezones.py. */', 'static const tz_transition_t transitions[]={']
lines+=['{%d,%d},'%r for r in records];lines+=['};','static const tz_zone_t zones[]={'];lines += ['{%s,%d,%d},'%(json.dumps(n),i,c) for n,i,c in entries];lines+=['};']
(root/'main/timezone_data.inc').write_text('\n'.join(lines)+'\n')
template=(root/'tools/timezones.template.js').read_text()
(root/'main/web/timezones.js').write_text(template.replace('TZ_NAMES',json.dumps(names,separators=(',',':'))))
print(json.dumps({'tzdata':version,'zones':len(names),'unique_timelines':len(sequences),'transitions':len(records),'base':start,'end':end}))
