/* Browser-only SGP4/SDP4 viewer. No orbital records, propagation or network fetch on ESP32. */
'use strict';
const TrackerOrbits=(()=>{
 const tr=(key,params)=>TrackerI18n.t('orbits.'+key,params);
 const $=id=>document.getElementById(id),R=6378.137,rad=Math.PI/180,MAX=500,KEY='tracker-orbits-v1';
 const names=['GPS','GLONASS','Galileo','BeiDou','QZSS','SBAS','NavIC','Andere'];
 const colors=['#75d8ff','#e9a1ff','#9debc0','#ffe09b','#ff9a7d','#afbcff','#f5a9ca','#c5d4df'];
 let active=false,assets=null,land=[],catalog=null,cacheSaved=null,records=[],selected='',enabled=new Set(names),raw=null,position=null,received=0;
 let camera={lon:10*rad,lat:25*rad},zoom=1,mode='ground',offset=0,span=360,trails=true,grid=true,drag=null,points=[],pick=[],frame=0,lastFrame=0,pathCache=null,lastDetail='',db=null,busy=false,allPoints=[],receptionFilter='all',aboveOnly=false,highlight=true,horizonLine=true,horizon=[],optionsSignature='',receptionState=null,modelCache=null,mapPick=[],globeTrail=[],mapTrail=[],hover=null,hoverSignature='',frameTime=0,pausedFilter=null,mapGesture=null;
 function systemLabel(name){return name==='Andere'?tr('other_system'):name;}
 function note(s){$('orbit-data-note').textContent=s;}
 function system(name){const n=name.toUpperCase();return /GALILEO|GSAT0/.test(n)?'Galileo':/GLONASS|COSMOS.*\(.*R\d|^COSMOS \d+ \(7\d\dK?\)$/.test(n)?'GLONASS':/BEIDOU|BDS|COMPASS/.test(n)?'BeiDou':/QZS|QZSS|MICHIBIKI/.test(n)?'QZSS':/IRNSS|NAVIC/.test(n)?'NavIC':/GPS|NAVSTAR/.test(n)?'GPS':/EGNOS|WAAS|MSAS|GAGAN|SDCM|SOUTHPAN|ASTRA 5B|SES.?5/.test(n)?'SBAS':'Andere';}
 function epochMs(s){if(typeof s!=='string'||!/^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d+)?Z?$/.test(s))return NaN;return Date.parse(s.endsWith('Z')?s:s+'Z');}
 function tleChecksum(line){if(line.length!==69||!/^\d$/.test(line[68]))return false;let sum=0;for(const c of line.slice(0,68))sum+=c==='-'?1:/\d/.test(c)?Number(c):0;return sum%10===Number(line[68]);}
 function parse(text){
  if(typeof text!=='string'||!text.trim()||text.length>1000000)throw Error(tr('empty_large'));
  let entries=[],format;text=text.trim();
  if(text[0]==='['||text[0]==='{'){
   let source;try{source=JSON.parse(text);}catch(e){throw Error(tr('invalid_json'));}
   if(!Array.isArray(source))source=[source];if(!source.length||source.length>MAX)throw Error(tr('catalog_count_bounds',{max:MAX}));
   for(const row of source){
    if(!row||typeof row!=='object')throw Error(tr('invalid_omm'));
    const fields=['MEAN_MOTION','ECCENTRICITY','INCLINATION','RA_OF_ASC_NODE','ARG_OF_PERICENTER','MEAN_ANOMALY','BSTAR','MEAN_MOTION_DOT','MEAN_MOTION_DDOT'];
    const j={...row};for(const k of fields){if(j[k]===undefined&&['BSTAR','MEAN_MOTION_DOT','MEAN_MOTION_DDOT'].includes(k))j[k]=0;if(j[k]===null||j[k]===''||!Number.isFinite(Number(j[k])))throw Error(tr('invalid_omm_field',{field:k}));j[k]=Number(j[k]);}
    const epoch=epochMs(j.EPOCH),id=Number(j.NORAD_CAT_ID);
    if(!Number.isFinite(epoch)||!Number.isInteger(id)||id<1||id>999999999||j.MEAN_MOTION<=0||j.MEAN_MOTION>20||j.ECCENTRICITY<0||j.ECCENTRICITY>=1||j.INCLINATION<0||j.INCLINATION>180||['RA_OF_ASC_NODE','ARG_OF_PERICENTER','MEAN_ANOMALY'].some(k=>j[k]<0||j[k]>=360))throw Error(tr('invalid_elements'));
    if((j.REF_FRAME&&j.REF_FRAME!=='TEME')||(j.TIME_SYSTEM&&j.TIME_SYSTEM!=='UTC')||(j.MEAN_ELEMENT_THEORY&&j.MEAN_ELEMENT_THEORY!=='SGP4')||(j.CENTER_NAME&&j.CENTER_NAME!=='EARTH'))throw Error(tr('supported_elements'));
    j.NORAD_CAT_ID=id;j.EPOCH=new Date(epoch).toISOString();const name=String(j.OBJECT_NAME||'NORAD '+id).slice(0,100);
    entries.push({id:String(id),name,epoch,kind:'omm',data:j,system:system(name)});
   }format='OMM-JSON';
  }else{
   const lines=text.split(/\r?\n/).map(s=>s.trimEnd()).filter(s=>s.trim());let name='';
   for(let i=0;i<lines.length;i++){
    const a=lines[i];if(!a.startsWith('1 ')){if(a.startsWith('2 '))throw Error(tr('missing_tle_line'));name=a.replace(/^0 /,'').trim().slice(0,100);continue;}
    const b=lines[++i];if(!b||!b.startsWith('2 ')||!tleChecksum(a)||!tleChecksum(b)||a.slice(2,7)!==b.slice(2,7))throw Error(tr('invalid_tle_lines'));
    const id=String(Number(a.slice(2,7)));if(!Number.isInteger(Number(id))||Number(id)<1)throw Error(tr('unsupported_tle_id'));
    const rec=satellite.twoline2satrec(a,b),epoch=(rec.jdsatepoch-2440587.5)*86400000;
    if(!Number.isFinite(epoch)||rec.error||!Number.isFinite(rec.no)||rec.no<=0||rec.ecco<0||rec.ecco>=1||!Number.isFinite(rec.inclo))throw Error(tr('invalid_tle_orbit'));
    const label=name||'NORAD '+id;entries.push({id,name:label,epoch,kind:'tle',data:[a,b],system:system(label)});name='';if(entries.length>MAX)throw Error(tr('catalog_max',{max:MAX}));
   }if(!entries.length)throw Error(tr('no_tle_lines'));format='TLE';
  }
  const unique=new Map();for(const e of entries)if(!unique.has(e.id)||unique.get(e.id).epoch<e.epoch)unique.set(e.id,e);
  return {format,entries:[...unique.values()]};
 }
 function model(entry){const rec=entry.kind==='tle'?satellite.twoline2satrec(...entry.data):satellite.json2satrec(entry.data);if(rec.error||!Number.isFinite(rec.no)||rec.no<=0)throw Error(tr('orbit_not_computable',{name:entry.name}));return {...entry,rec};}
 function calculate(entry,t){
  try{const date=new Date(t),result=satellite.propagate(entry.rec,date);if(!result||!result.position||entry.rec.error)return null;
   const eci=result.position;if(![eci.x,eci.y,eci.z].every(Number.isFinite))return null;const gmst=satellite.gstime(date),ecf=satellite.eciToEcf(eci,gmst),geo=satellite.eciToGeodetic(eci,gmst);
   if(![geo.latitude,geo.longitude,geo.height,ecf.x,ecf.y,ecf.z].every(Number.isFinite))return null;
   const velocity=result.velocity,speed=velocity&&[velocity.x,velocity.y,velocity.z].every(Number.isFinite)?Math.hypot(velocity.x,velocity.y,velocity.z):null;
   return {id:entry.id,entry,t,eci,ecf,speed,lat:geo.latitude/rad,lon:geo.longitude/rad,height:geo.height};
  }catch(e){return null;}
 }
 function now(){const c=raw?.gnss_time,elapsed=performance.now()-received;return c?.confirmed&&c.recent&&elapsed>=0&&elapsed<12000&&Number.isFinite(c.utc_ms)?{t:c.utc_ms+elapsed,source:'GNSS'}:{t:Date.now(),source:tr('browser_time')};}
 function observer(){const p=position;if(!p?.gnss_fix||performance.now()-received>12000||!Number.isFinite(p.latitude)||!Number.isFinite(p.longitude))return null;return {latitude:p.latitude*rad,longitude:p.longitude*rad,height:Number.isFinite(p.altitude)?p.altitude/1000:0};}
 /* Identifiers are system-specific. Never infer a GLONASS slot from a COSMOS number,
    nor an SBAS PRN from the receiver's remapped 33..79 ID range. */
 function catalogIdentity(e){
  const n=e.name.toUpperCase(),fail=reason=>({key:null,label:tr('unmapped'),reason});
  const one=(values,system,label)=>{const ids=[...new Set(values)];return ids.length===1?{key:system+':'+ids[0],label:label(ids[0]),reason:''}:fail(ids.length?tr('multiple_catalog_ids'):tr('missing_catalog_id'));};
  if(e.system==='Galileo'){
   const gsats=[...n.matchAll(/\bGSAT\d{4}\b/g)].map(m=>m[0]);if(new Set(gsats).size!==1)return fail(tr('missing_gsat'));
   const table=typeof galileoCatalog==='undefined'?{}:galileoCatalog,ids=Object.entries(table).filter(([id,v])=>v[0]===gsats[0]).map(([id])=>Number(id));
   return one(ids,3,id=>'E'+String(id).padStart(2,'0')+' ↔ '+gsats[0]);
  }
  if(e.system==='GPS')return one([...n.matchAll(/\bPRN\s*0*(\d{1,3})\b/g)].map(m=>Number(m[1])).filter(id=>id>=1&&id<=32),1,id=>'GPS PRN '+id);
  if(e.system==='BeiDou')return one([...n.matchAll(/\b(?:C|PRN\s*)0*(\d{1,3})\b/g)].map(m=>Number(m[1])).filter(id=>id>=1&&id<=63),4,id=>'BeiDou C'+String(id).padStart(2,'0'));
  if(e.system==='GLONASS')return one([...n.matchAll(/\b(?:SLOT\s*|R)0*(\d{1,2})\b/g)].map(m=>Number(m[1])).filter(id=>id>=1&&id<=28),2,id=>'GLONASS R'+String(id).padStart(2,'0'));
  if(e.system==='QZSS')return one([...n.matchAll(/\bPRN\s*(\d{3})\b/g)].map(m=>Number(m[1])).filter(id=>id>=193&&id<=202),5,id=>'QZSS PRN '+id);
  if(e.system==='SBAS')return one([...n.matchAll(/\bPRN\s*(\d{3})\b/g)].map(m=>Number(m[1])).filter(id=>id>=120&&id<=158),6,id=>'SBAS PRN '+id);
  if(e.system==='NavIC')return one([...n.matchAll(/\b(?:PRN|SVID)\s*(\d{1,2})\b/g)].map(m=>Number(m[1])).filter(id=>id>=1&&id<=14),7,id=>'NavIC PRN '+id);
  return fail(tr('unsupported_system'));
 }
 function receiverIdentity(row){const [sys,id]=row;let value=id;
  if(!Number.isInteger(id))return null;
  if(sys===2&&id>=65&&id<=92)value=id-64;
  const valid=sys===1?id>=1&&id<=32:sys===2?value>=1&&value<=28:sys===3?id>=1&&id<=36:sys===4?id>=1&&id<=63:sys===5?id>=193&&id<=202:sys===6?id>=120&&id<=158:sys===7?id>=1&&id<=14:false;
  return valid?sys+':'+value:null;
 }
 function angularSeparation(a,b,c,d){const cosine=Math.sin(a*rad)*Math.sin(c*rad)+Math.cos(a*rad)*Math.cos(c*rad)*Math.cos((b-d)*rad);return Math.acos(Math.max(-1,Math.min(1,cosine)))/rad;}
 function receptionSnapshot(entries,rows,{fresh,fix,utc,observer:obs,t,models}){
  const keys=new Map(),byRow=new Map(),matches=new Map();let receivedCount=0,usedCount=0;
  for(const e of entries){const identity=catalogIdentity(e);matches.set(e.id,{identity,state:identity.key?'mapped':'uncertain',reason:identity.reason,received:false,used:false,cn0:null});if(identity.key){const ids=keys.get(identity.key)||[];ids.push(e.id);keys.set(identity.key,ids);}}
  for(const row of rows||[]){const k=receiverIdentity(row);if(k){const values=byRow.get(k)||[];values.push(row);byRow.set(k,values);}}
  for(const e of entries){const m=matches.get(e.id),k=m.identity.key;if(!k)continue;
   if(keys.get(k).length!==1){m.state='uncertain';m.reason=tr('duplicate_orbits');continue;}
   const candidates=byRow.get(k)||[];if(candidates.length>1){m.state='uncertain';m.reason=tr('duplicate_receiver');continue;}
   const row=candidates[0];if(!fresh)continue;if(!row){if((rows||[]).some(r=>r[0]===Number(k.split(':')[0])&&!receiverIdentity(r))){m.state='uncertain';m.reason=tr('unknown_receiver_range');}continue;}m.row=row;m.cn0=row[4];
   /* A strong direction conflict can reveal a reassigned PRN or stale catalog. */
   if(obs&&utc&&Number.isFinite(row[2])&&Number.isFinite(row[3])){
    const p=models?models.get(e.id):calculate(e,t);if(p){const look=satellite.ecfToLookAngles(obs,p.ecf),delta=angularSeparation(row[2],row[3],look.elevation/rad,look.azimuth/rad);m.direction_delta=delta;if(delta>15){m.state='uncertain';m.reason=tr('direction_mismatch',{angle:delta.toFixed(1)});continue;}}
   }
   m.received=!!(row[5]&1)&&Number.isFinite(row[4])&&row[4]>0;m.used=!!fix&&!!(row[5]&4)&&!!(row[5]&2);receivedCount+=m.received?1:0;usedCount+=m.used?1:0;
  }
  const unmatched=[];if(fresh)for(const row of rows||[]){const k=receiverIdentity(row),ids=k?keys.get(k):null,m=ids?.length===1?matches.get(ids[0]):null;if(!m||m.state==='uncertain')unmatched.push({system:names[row[0]-1]||tr('unknown'),id:row[1],reason:m?.reason||(!k?tr('unknown_nmea_range'):ids?.length>1?tr('multiple_orbits'):tr('no_matching_orbit'))});}
  return {matches,unmatched,receivedCount,usedCount,fresh};
 }
 function horizonSamples(o,height){if(!o||!Number.isFinite(height)||height<=0)return [];const a=Math.acos(R/(R+height)),lat=o.latitude,lon=o.longitude,rows=[];for(let i=0;i<=144;i++){const bearing=i/144*2*Math.PI,p=Math.asin(Math.sin(lat)*Math.cos(a)+Math.cos(lat)*Math.sin(a)*Math.cos(bearing)),l=lon+Math.atan2(Math.sin(bearing)*Math.sin(a)*Math.cos(lat),Math.cos(a)-Math.sin(lat)*Math.sin(p));rows.push({lat:p/rad,lon:((l/rad+540)%360)-180});}return rows;}
 function filterPoints(rows,filter,above){return rows.filter(p=>(!above||p.above===true)&&(filter==='received'?p.reception.received:filter==='used'?p.reception.used:filter==='uncertain'?p.reception.state==='uncertain':true));}
 function paintMarker(ctx,p,x,y,chosen,base){
  const m=p.reception,live=highlight&&offset===0&&receptionState?.fresh,r=chosen?6:live&&(m.received||m.used)?4.5:base;
  ctx.globalAlpha=(live&&!m.received&&!m.used) ? 0.35 : 1;ctx.beginPath();ctx.arc(x,y,r,0,2*Math.PI);ctx.fillStyle=colors[names.indexOf(p.entry.system)];ctx.fill();ctx.globalAlpha=1;
  const ring=(radius,color,dash=[])=>{ctx.beginPath();ctx.arc(x,y,radius,0,2*Math.PI);ctx.strokeStyle=color;ctx.lineWidth=1.5;ctx.setLineDash(dash);ctx.stroke();ctx.setLineDash([]);};
  if(live&&m.used)ring(r+3,'#ffffff');
  if(live&&m.state==='uncertain')ring(r+2,'#a0b4c6',[2,3]);
  if(p.above&&$('orbit-horizon-mark').checked)ring(r+(live&&m.used?6:4),'#ffc66e');
 }
 function liveOnlyFilter(value){return value==='received'||value==='used';}
 function effectiveFilter(value,shift){return shift&&liveOnlyFilter(value)?'all':value;}
 function shiftedTime(base,minutes){return base+minutes*60000;}
 function formatTime(t){return new Date(t).toLocaleString(TrackerI18n.locale(),{timeZone:'UTC',year:'numeric',month:'2-digit',day:'2-digit',hour:'2-digit',minute:'2-digit',second:'2-digit'})+' UTC';}
 function setOffset(value){
  const next=Math.max(-720,Math.min(720,Number(value)||0));
  if(next&&liveOnlyFilter(receptionFilter)){pausedFilter=receptionFilter;receptionFilter='all';}
  if(!next&&pausedFilter){receptionFilter=pausedFilter;pausedFilter=null;}
  offset=next;$('orbit-time').value=String(offset);$('orbit-reception-filter').value=receptionFilter;hideHover();schedule();
 }
 function renderTimes(t,clock){
  const simulation=offset!==0,label=(simulation?'Simulation':'Live')+' · '+formatTime(t);
  $('orbit-display-time').textContent=formatTime(t);$('orbit-time-state').textContent=simulation?'Simulation':'Live';$('orbit-time-state').title=tr('time_source')+clock.source;
  $('orbit-time-state').closest('.orbit-timebar').dataset.mode=simulation?'simulation':'live';$('orbit-offset').textContent=(offset>0?'+':'')+offset+' min · '+clock.source;
  for(const id of ['orbit-globe-time','orbit-map-time']){$(id).textContent=label;$(id).parentElement.dataset.mode=simulation?'simulation':'live';}
  $('orbit-now').disabled=!simulation;document.querySelectorAll('[data-orbit-live]').forEach(button=>{button.hidden=!simulation;});
 }
 function nearestHit(items,x,y,radius=12){let best=null;for(const p of items){const d=Math.hypot(p.x-x,p.y-y);if(d<=radius&&(!best||d<best.distance))best={...p,distance:d,kind:'satellite'};}return best;}
 function nearestTrail(segments,x,y,radius=7){let best=null;for(const path of segments)for(let i=1;i<path.length;i++){
  const a=path[i-1],b=path[i],dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy,k=length?Math.max(0,Math.min(1,((x-a.x)*dx+(y-a.y)*dy)/length)):0;
  const q={x:a.x+k*dx,y:a.y+k*dy},distance=Math.hypot(x-q.x,y-q.y);if(distance<=radius&&(!best||distance<best.distance))best={kind:'trail',id:a.id,t:a.t+k*(b.t-a.t),distance,...q};
 }return best;}
 function hitAt(surface,x,y){return nearestHit(surface==='map'?mapPick:pick,x,y)||nearestTrail(surface==='map'?mapTrail:globeTrail,x,y);}
 function chooseSatellite(id){if(!points.some(p=>p.id===id))return;selected=id;$('orbit-satellite').value=id;pathCache=null;schedule();}
 function hideHover(){hover=null;hoverSignature='';const box=$('orbit-tooltip');if(box)box.hidden=true;}
 function hoverEvent(surface,event,select=false){
  const c=$(surface==='map'?'orbit-map':'orbit-globe'),r=c.getBoundingClientRect(),x=event.clientX-r.left,y=event.clientY-r.top,hit=hitAt(surface,x,y);
  if(!hit){hideHover();return;}if(select&&hit.kind==='satellite')chooseSatellite(hit.id);
  hover={...hit,surface,clientX:event.clientX,clientY:event.clientY};renderHover();
 }
 function tooltipRows(p,trailHit){
  const values=[[tr('time'),formatTime(p.t)],[tr('position'),p.lat.toFixed(3)+'° / '+p.lon.toFixed(3)+'°'],[tr('altitude'),Math.round(p.height).toLocaleString(TrackerI18n.locale())+' km'],[tr('orbital_speed'),p.speed===null?'—':p.speed.toLocaleString(TrackerI18n.locale(),{minimumFractionDigits:2,maximumFractionDigits:2})+' km/s · '+Math.round(p.speed*3600).toLocaleString(TrackerI18n.locale())+' km/h']];
  const o=observer();if(o){const look=satellite.ecfToLookAngles(o,p.ecf);values.push([tr('distance'),Math.round(look.rangeSat).toLocaleString(TrackerI18n.locale())+' km'],[tr('azimuth'), tr('look_angles',{azimuth:(look.azimuth/rad).toFixed(1),elevation:(look.elevation/rad).toFixed(1)})],[tr('horizon'),look.elevation>=0?tr('above_computed'):tr('below_computed')]);}
  const m=p.reception;
  if(trailHit||offset)values.push([tr('reception_fix'),tr('historical_reception')]);
  else if(!receptionState?.fresh)values.push([tr('reception_fix'),tr('stale_tracker')]);
  else if(m?.state==='uncertain')values.push([tr('mapping'),tr('uncertain_prefix')+m.reason]);
  else if(m){values.push([tr('reception'),m.received?tr('reported_signal',{signal:m.cn0}):tr('no_reception_proof')],[tr('in_fix'),!position?.gnss_fix?tr('no_fix'):m.used?tr('yes'):m.row&&(m.row[5]&4)?tr('no'):tr('unknown')],[tr('mapping'),m.identity.label]);}
  return values;
 }
 function renderHover(){
  if(!hover)return;const e=records.find(e=>e.id===hover.id);if(!e){hideHover();return;}
  const p=hover.kind==='trail'?calculate(e,hover.t):points.find(p=>p.id===hover.id);if(!p){hideHover();return;}
  const values=tooltipRows(p,hover.kind==='trail'),signature=JSON.stringify([e.id,hover.kind,values]),box=$('orbit-tooltip');
  if(signature!==hoverSignature){hoverSignature=signature;box.replaceChildren();const title=document.createElement('strong');title.textContent=e.name+' · '+systemLabel(e.system)+' · NORAD '+e.id;box.append(title);
   if(hover.kind==='trail'){const kind=document.createElement('p');kind.textContent=tr('trail_time');kind.className='orbit-tip-time';box.append(kind);}
   for(const [label,value]of values){const line=document.createElement('p');line.textContent=label+': '+value;if(label===tr('time'))line.className='orbit-tip-time';if(label===tr('reception_fix')||label===tr('reception'))line.className+=' orbit-tip-reception';box.append(line);}}
  box.hidden=false;const r=box.getBoundingClientRect();box.style.left=Math.max(8,Math.min(window.innerWidth-r.width-8,hover.clientX+14))+'px';box.style.top=Math.max(8,Math.min(window.innerHeight-r.height-8,hover.clientY+14))+'px';
 }
 function receptionNotes(clock){
  const live=offset===0,fresh=receptionState.fresh,o=observer(),nAbove=allPoints.filter(p=>p.above).length;
  const selector=$('orbit-reception-filter');selector.disabled=false;for(const option of selector.options){if(liveOnlyFilter(option.value))option.disabled=!live;}
  $('orbit-filter-help').hidden=live;$('orbit-filter-help-text').textContent=tr('simulation_help');
  $('orbit-above-only').disabled=!o;
  $('orbit-live-note').textContent=!live?tr('shifted_help'):!fresh?tr('no_fresh_reception'):tr('reception_summary',{received:receptionState.receivedCount,used:receptionState.usedCount,unmatched:receptionState.unmatched.length});
  $('orbit-horizon-note').textContent=o?tr('horizon_summary',{above:nAbove,total:allPoints.length}):tr('horizon_needs_fix');
  const box=$('orbit-unmatched'),signature=JSON.stringify([fresh,receptionState.unmatched]);if(box.dataset.signature!==signature){box.dataset.signature=signature;box.replaceChildren();for(const u of receptionState.unmatched){const li=document.createElement('li');li.textContent=systemLabel(u.system)+' · NMEA '+u.id+': '+u.reason;box.append(li);}if(!receptionState.unmatched.length){const li=document.createElement('li');li.textContent=fresh?tr('all_mapped'):tr('no_ids_to_check');box.append(li);}}
 }
 function angleDiff(a,b){return Math.abs(a-b)>180;}
 function vec(lat,lon,r=1){lat*=rad;lon*=rad;return {x:r*Math.cos(lat)*Math.cos(lon),y:r*Math.cos(lat)*Math.sin(lon),z:r*Math.sin(lat)};}
 function rotate(v){const sl=Math.sin(camera.lon),cl=Math.cos(camera.lon),sp=Math.sin(camera.lat),cp=Math.cos(camera.lat),forward=cl*v.x+sl*v.y;return {x:-sl*v.x+cl*v.y,y:-sp*forward+cp*v.z,z:cp*forward+sp*v.z};}
 function unitEcf(ecf){return {x:ecf.x/R,y:ecf.y/R,z:ecf.z/R};}
 function hidden(v,ground){return ground?v.z<0:v.z<0&&v.x*v.x+v.y*v.y<1;}
 function canvas(id){const c=$(id),r=c.getBoundingClientRect(),dpr=Math.min(window.devicePixelRatio||1,2),w=Math.max(1,r.width),h=Math.max(1,r.height);if(c.width!==Math.round(w*dpr)||c.height!==Math.round(h*dpr)){c.width=Math.round(w*dpr);c.height=Math.round(h*dpr);}const ctx=c.getContext('2d');ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);return {c,ctx,w,h};}
 function spherePath(ctx,path,cx,cy,scale,ground=true,color='#40556b',dash=[]){ctx.beginPath();let live=false;for(const v of path){const q=rotate(v);if(hidden(q,ground)){live=false;continue;}const x=cx+q.x*scale,y=cy-q.y*scale;if(live)ctx.lineTo(x,y);else ctx.moveTo(x,y);live=true;}ctx.strokeStyle=color;ctx.lineWidth=1.3;ctx.setLineDash(dash);ctx.stroke();ctx.setLineDash([]);}
 function globe(t,trail){
  const {ctx,w,h}=canvas('orbit-globe'),cx=w/2,cy=h/2,scale=Math.min(w,h)*(mode==='ground'?.4:.057)*zoom;
  const halo=ctx.createRadialGradient(cx,cy,scale*.8,cx,cy,scale*1.12);halo.addColorStop(0,'#143754');halo.addColorStop(.9,'#17364e');halo.addColorStop(1,'#081522');ctx.fillStyle=halo;ctx.beginPath();ctx.arc(cx,cy,scale*1.12,0,2*Math.PI);ctx.fill();
  const sea=ctx.createRadialGradient(cx-scale*.35,cy-scale*.4,0,cx,cy,scale);sea.addColorStop(0,'#195276');sea.addColorStop(.8,'#10334d');sea.addColorStop(1,'#071e30');ctx.fillStyle=sea;ctx.beginPath();ctx.arc(cx,cy,scale,0,2*Math.PI);ctx.fill();
  ctx.save();ctx.beginPath();ctx.arc(cx,cy,scale,0,2*Math.PI);ctx.clip();
  for(const ring of land){const front=ring.map(([lon,lat])=>rotate(vec(lat,lon)));let vertices=[];for(let i=0;i<front.length;i++){const a=front[i],b=front[(i+1)%front.length];if(a.z>=0)vertices.push(a);if((a.z>=0)!==(b.z>=0)){const k=a.z/(a.z-b.z),v={x:a.x+k*(b.x-a.x),y:a.y+k*(b.y-a.y)};const d=Math.hypot(v.x,v.y);vertices.push({x:v.x/d,y:v.y/d,z:0});}}if(vertices.length<3)continue;ctx.beginPath();vertices.forEach((q,i)=>i?ctx.lineTo(cx+q.x*scale,cy-q.y*scale):ctx.moveTo(cx+q.x*scale,cy-q.y*scale));ctx.closePath();ctx.fillStyle='#387166';ctx.fill();ctx.strokeStyle='#6c9c88';ctx.lineWidth=.5;ctx.stroke();}
  if(grid){for(let lat=-60;lat<=60;lat+=30)spherePath(ctx,Array.from({length:121},(_,i)=>vec(lat,-180+i*3)),cx,cy,scale,true,'#42617a');for(let lon=-180;lon<180;lon+=30)spherePath(ctx,Array.from({length:61},(_,i)=>vec(-90+i*3,lon)),cx,cy,scale,true,'#42617a');}
  ctx.restore();
  if(trails&&trail.length){const parts=[trail.filter(p=>p.t<=t),trail.filter(p=>p.t>=t)];parts.forEach((part,i)=>{const vectors=part.map(p=>mode==='ground'?vec(p.lat,p.lon):unitEcf(satellite.eciToEcf(p.eci,satellite.gstime(new Date(t)))));spherePath(ctx,vectors,cx,cy,scale,mode==='ground',i?'#75d8ff':'#9debc0',i?[5,4]:[]);});}
  if(horizonLine&&horizon.length)spherePath(ctx,horizon.map(p=>vec(p.lat,p.lon,1.003)),cx,cy,scale,true,'#ffc66e',[4,4]);
  globeTrail=[];if(trails&&trail.length){let segment=[];for(const p of trail){const v=mode==='ground'?vec(p.lat,p.lon):unitEcf(satellite.eciToEcf(p.eci,satellite.gstime(new Date(t)))),q=rotate(v);if(hidden(q,mode==='ground')){if(segment.length)globeTrail.push(segment);segment=[];}else segment.push({id:p.id,t:p.t,x:cx+q.x*scale,y:cy-q.y*scale});}if(segment.length)globeTrail.push(segment);}
  pick=[];const ordered=points.map(p=>({p,q:rotate(mode==='ground'?vec(p.lat,p.lon,1.004):unitEcf(p.ecf))})).sort((a,b)=>a.q.z-b.q.z);
  for(const {p,q} of ordered){if(hidden(q,mode==='ground'))continue;const x=cx+q.x*scale,y=cy-q.y*scale;if(x<-10||y<-10||x>w+10||y>h+10)continue;const chosen=p.id===selected;paintMarker(ctx,p,x,y,chosen,3);if(chosen){ctx.fillStyle='#eef5fb';ctx.font='12px system-ui';ctx.fillText(p.entry.name.slice(0,27),Math.min(w-190,x+10),Math.max(15,y-9));}pick.push({id:p.id,x,y});}
  const o=observer();if(o){const q=rotate(vec(o.latitude/rad,o.longitude/rad,1.015));if(q.z>=0){const x=cx+q.x*scale,y=cy-q.y*scale;ctx.beginPath();ctx.moveTo(x,y-7);ctx.lineTo(x+6,y);ctx.lineTo(x,y+7);ctx.lineTo(x-6,y);ctx.closePath();ctx.fillStyle='#fff';ctx.fill();ctx.strokeStyle='#091522';ctx.stroke();}}
  if(!records.length){ctx.fillStyle='#b4cbdc';ctx.textAlign='center';ctx.font='14px system-ui';ctx.fillText(tr('load_import'),cx,h-24);ctx.textAlign='left';}
 }
 function splitMapPath(samples){const parts=[];let part=[];for(const p of samples){if(part.length&&angleDiff(part.at(-1).lon,p.lon)){if(part.length)parts.push(part);part=[];}part.push(p);}if(part.length)parts.push(part);return parts;}
 function map(t,trail){
  const {ctx,w,h}=canvas('orbit-map'),xy=(lat,lon)=>({x:(lon+180)/360*w,y:(90-lat)/180*h});ctx.fillStyle='#0b2135';ctx.fillRect(0,0,w,h);
  ctx.strokeStyle='#26435a';ctx.lineWidth=.6;for(let lon=-180;lon<=180;lon+=30){ctx.beginPath();ctx.moveTo(xy(90,lon).x,0);ctx.lineTo(xy(-90,lon).x,h);ctx.stroke();}for(let lat=-60;lat<=60;lat+=30){ctx.beginPath();ctx.moveTo(0,xy(lat,0).y);ctx.lineTo(w,xy(lat,0).y);ctx.stroke();}
  ctx.fillStyle='#31594f';ctx.strokeStyle='#59826f';for(const ring of land){if(ring.some((p,i)=>i&&angleDiff(p[0],ring[i-1][0])))continue;ctx.beginPath();ring.forEach(([lon,lat],i)=>{const q=xy(lat,lon);if(i)ctx.lineTo(q.x,q.y);else ctx.moveTo(q.x,q.y);});ctx.closePath();ctx.fill();ctx.stroke();}
  if(trails){for(const [i,part]of [trail.filter(p=>p.t<=t),trail.filter(p=>p.t>=t)].entries()){ctx.strokeStyle=i?'#75d8ff':'#9debc0';ctx.lineWidth=2;ctx.setLineDash(i?[5,4]:[]);for(const segment of splitMapPath(part)){ctx.beginPath();segment.forEach((p,k)=>{const q=xy(p.lat,p.lon);if(k)ctx.lineTo(q.x,q.y);else ctx.moveTo(q.x,q.y);});ctx.stroke();}}ctx.setLineDash([]);}
  if(horizonLine&&horizon.length){ctx.strokeStyle='#ffc66e';ctx.lineWidth=1.5;ctx.setLineDash([4,4]);for(const segment of splitMapPath(horizon)){ctx.beginPath();segment.forEach((p,i)=>{const q=xy(p.lat,p.lon);if(i)ctx.lineTo(q.x,q.y);else ctx.moveTo(q.x,q.y);});ctx.stroke();}ctx.setLineDash([]);}
  mapTrail=trails?splitMapPath(trail).map(segment=>segment.map(p=>({id:p.id,t:p.t,...xy(p.lat,p.lon)}))):[];
  mapPick=[];for(const p of points){const q=xy(p.lat,p.lon);paintMarker(ctx,p,q.x,q.y,p.id===selected,2.5);mapPick.push({id:p.id,x:q.x,y:q.y});}
  const o=observer();if(o){const q=xy(o.latitude/rad,o.longitude/rad);ctx.strokeStyle='#fff';ctx.lineWidth=2;ctx.strokeRect(q.x-4,q.y-4,8,8);}
  ctx.fillStyle='#b4cbdc';ctx.font='11px system-ui';ctx.fillText('180° W',5,h-6);ctx.fillText('0°',w/2,h-6);ctx.fillText('180° E',w-45,h-6);
 }
 function selectionTrail(t){if(!trails||!selected)return [];const minute=Math.floor(t/60000),key=selected+':'+minute+':'+span;if(pathCache?.key===key)return pathCache.rows;const e=records.find(e=>e.id===selected);if(!e)return [];const center=minute*60000,rows=[];for(let i=-90;i<=90;i++){const p=calculate(e,center+i*span*60000/90);if(p)rows.push(p);}pathCache={key,rows};return rows;}
 function renderDetail(t,clock){
  const entry=records.find(e=>e.id===selected),p=allPoints.find(p=>p.id===selected),o=observer();$('orbit-name').textContent=entry?entry.name:tr('select_satellite');$('orbit-clock').textContent=clock.source+' · '+new Date(t).toLocaleString(TrackerI18n.locale(),{timeZone:'UTC'})+' UTC'+(offset?' · '+(offset>0?'+':'')+offset+' min':' · live');$('orbit-home').disabled=!o;
  const values=[];if(entry){values.push([tr('catalog_number'),'NORAD '+entry.id],['System',systemLabel(entry.system)],[tr('orbit_epoch'),new Date(entry.epoch).toISOString().replace('T',' ').replace('Z','')],[tr('epoch_age'),tr('days',{count:(Math.abs(t-entry.epoch)/86400000).toFixed(1)})]);if(p){values.push([tr('latitude_longitude'),p.lat.toFixed(3)+'° / '+p.lon.toFixed(3)+'°'],[tr('altitude'),Math.round(p.height).toLocaleString(TrackerI18n.locale())+' km'],[tr('speed_geocentric'),p.speed===null?'—':p.speed.toLocaleString(TrackerI18n.locale(),{minimumFractionDigits:2,maximumFractionDigits:2})+' km/s · '+Math.round(p.speed*3600).toLocaleString(TrackerI18n.locale())+' km/h']);if(o){const look=satellite.ecfToLookAngles(o,p.ecf);values.push([tr('distance_tracker'),Math.round(look.rangeSat).toLocaleString(TrackerI18n.locale())+' km'],[tr('elevation'),(look.elevation/rad).toFixed(1)+'°'],[tr('direction_azimuth'),(look.azimuth/rad).toFixed(1)+'°'],[tr('geometric_horizon'),look.elevation>=0?tr('above'):tr('below')]);}else values.push([tr('distance_direction'),tr('missing_tracker_fix')]);}else values.push([tr('calculation'),tr('cannot_calculate')]);}
  const match=p?.reception;if(entry&&match){values.push([tr('mapping'),match.state==='uncertain'?tr('uncertain_prefix')+match.reason:match.identity.label+tr('catalog_identifier')]);values.push([tr('reception'),offset?tr('live_only'):!receptionState.fresh?tr('stale_reception'):match.state==='uncertain'?tr('uncertain_match'):match.received?tr('received_signal',{signal:match.cn0}):tr('no_current_reception')],[tr('used_for_fix'),offset?tr('live_only'):!receptionState.fresh||match.state==='uncertain'?tr('unknown'):!position?.gnss_fix?tr('no_fix'):match.used?tr('yes'):match.row&&(match.row[5]&4)?tr('no'):tr('no_gsa')]);}
  const signature=JSON.stringify(values);if(signature!==lastDetail){lastDetail=signature;const dl=$('orbit-detail');dl.replaceChildren();for(const [label,value]of values){const item=document.createElement('div'),dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=label;dd.textContent=value;item.append(dt,dd);dl.append(item);}}
  const epochs=records.map(e=>e.epoch),oldCount=epochs.filter(e=>Math.abs(t-e)>14*86400000).length,age=epochs.length?Math.max(...epochs.map(e=>Math.abs(t-e)))/86400000:0;const failed=records.filter(e=>enabled.has(e.system)).length-allPoints.length;
  $('orbit-status').textContent=records.length?tr('calculation_summary',{shown:points.length,calculated:allPoints.length})+(failed?tr('calculation_failed',{count:failed}):'')+(oldCount?tr('old_elements',{count:oldCount}):age>3?tr('check_orbits'):''):tr('no_catalog');
  $('orbit-view-note').textContent=mode==='ground'?tr('ground_view'):tr('space_view');
 }
 function render(){frame=0;if(!active||!assets||!land.length)return;
  const clock=now(),t=shiftedTime(clock.t,offset),o=observer(),fresh=offset===0&&position?.gnss_data_recent===true&&performance.now()-received<12000;
  const bucket=Math.floor(t/1000);if(!modelCache||modelCache.bucket!==bucket||modelCache.records!==records)modelCache={bucket,records,rows:records.map(e=>calculate(e,t)).filter(Boolean)};
  const models=new Map(modelCache.rows.map(p=>[p.id,p]));receptionState=receptionSnapshot(records,raw?.sky||[],{fresh,fix:position?.gnss_fix,utc:clock.source==='GNSS',observer:o,t,models});
  allPoints=modelCache.rows.filter(p=>enabled.has(p.entry.system)).map(p=>{const look=o?satellite.ecfToLookAngles(o,p.ecf):null;return {...p,above:look?look.elevation>=0:null,reception:receptionState.matches.get(p.id)};});
  const filter=effectiveFilter(receptionFilter,offset);points=filterPoints(allPoints,filter,aboveOnly&&!!o);syncOptions(points.map(p=>p.entry));
  const selectedPoint=allPoints.find(p=>p.id===selected);horizon=horizonSamples(o,selectedPoint?.height);const trail=selectionTrail(t);globe(t,trail);map(t,trail);renderDetail(t,clock);renderTimes(t,clock);receptionNotes(clock);renderHover();downloadState();lastFrame=performance.now();
 }
 function schedule(){if(active&&!frame)frame=requestAnimationFrame(render);}
 function syncOptions(visible){const select=$('orbit-satellite'),signature=JSON.stringify(visible.map(e=>[e.id,e.name]));if(!visible.some(e=>e.id===selected)){selected=visible[0]?.id||'';pathCache=null;}
  if(signature!==optionsSignature){optionsSignature=signature;select.replaceChildren();for(const e of visible){const option=document.createElement('option');option.value=e.id;option.textContent=systemLabel(e.system)+' · '+e.name+' · '+e.id;select.append(option);}if(!visible.length){const option=document.createElement('option');option.value='';option.textContent=records.length?tr('no_filtered_satellites'):tr('load_orbits');select.append(option);}}
  if(select.value!==selected)select.value=selected;
 }
 function options(){optionsSignature='';pathCache=null;schedule();}

 async function ensureAssets(){
  if(assets)return assets;
  assets=Promise.all([new Promise((resolve,reject)=>{if(typeof satellite!=='undefined'){resolve();return;}const tag=document.createElement('script');tag.src='/satellite.js';tag.onload=resolve;tag.onerror=()=>reject(Error(tr('library_load_failed')));document.head.append(tag);}),fetch('/earth-land.json',{signal:AbortSignal.timeout(15000)}).then(r=>{if(!r.ok)throw Error(tr('contours_load_failed'));return r.json();}).then(j=>{if(!Array.isArray(j))throw Error(tr('invalid_contours'));land=j;})]);
  try{await assets;render();}catch(e){assets=null;throw e;}return assets;
 }
 async function openDB(){if(db)return db;db=new Promise((resolve,reject)=>{if(!window.indexedDB){reject(Error(tr('indexeddb_unavailable')));return;}const request=indexedDB.open(KEY,1);request.onupgradeneeded=()=>request.result.createObjectStore('catalog');request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error);});return db;}
 async function cacheRead(){let fallback=null,value=null;try{fallback=JSON.parse(localStorage.getItem(KEY)||'null');}catch(e){}try{const d=await openDB();value=await new Promise((resolve,reject)=>{const q=d.transaction('catalog').objectStore('catalog').get('current');q.onsuccess=()=>resolve(q.result);q.onerror=()=>reject(q.error);});}catch(e){}return (fallback?.storedAt||0)>(value?.storedAt||0)?fallback:value||fallback;}

 async function cacheWrite(value){try{const d=await openDB();await new Promise((resolve,reject)=>{const tx=d.transaction('catalog','readwrite');tx.objectStore('catalog').put(value,'current');tx.oncomplete=resolve;tx.onerror=()=>reject(tx.error);tx.onabort=()=>reject(tx.error);});return 'IndexedDB';}catch(e){try{localStorage.setItem(KEY,JSON.stringify(value));return tr('browser_storage');}catch(e){return null;}}}
 async function useText(text,source,fetchedAt=0,save=true){
  await ensureAssets();if(typeof satellite==='undefined')throw Error(tr('missing_library'));const parsed=parse(text),next=parsed.entries.map(model);if(!next.length)throw Error(tr('no_computable_elements'));
  let saved=tr('browser_storage');const nextCatalog={text,source,fetchedAt,format:parsed.format,storedAt:Date.now()};if(save)saved=await cacheWrite(nextCatalog);
  records=next;catalog=nextCatalog;pathCache=null;lastDetail='';options();$('orbit-export').disabled=false;
  const epochs=records.map(r=>r.epoch),oldest=new Date(Math.min(...epochs)).toISOString().slice(0,19).replace('T',' '),newest=new Date(Math.max(...epochs)).toISOString().slice(0,19).replace('T',' ');
  note(tr('catalog_loaded',{count:records.length,format:parsed.format,source,oldest,newest}));cacheSaved=!!saved;$('orbit-cache-note').textContent=saved?tr('catalog_cached'):tr('catalog_not_cached');schedule();downloadState();
 }
 function lastFetch(){try{return Number(localStorage.getItem(KEY+'-fetch-at'))||0;}catch(e){return catalog?.fetchedAt||0;}}
 function downloadState(){const wait=Math.max(0,lastFetch()+7200000-Date.now());$('orbit-download').disabled=busy||wait>0;$('orbit-download').textContent=busy?tr('loading_orbits'):wait?tr('download_after',{time:new Date(Date.now()+wait).toLocaleTimeString(TrackerI18n.locale(),{hour:'2-digit',minute:'2-digit'})}):records.length?tr('update_gnss_orbits'):tr('load_gnss_orbits');}
 async function download(){if(busy||Date.now()-lastFetch()<7200000)return;busy=true;downloadState();try{await ensureAssets();const r=await fetch('https://celestrak.org/NORAD/elements/gp.php?GROUP=gnss&FORMAT=json',{mode:'cors',credentials:'omit',referrerPolicy:'no-referrer',signal:AbortSignal.timeout(20000)});if(!r.ok)throw Error(tr('http_error',{status:r.status}));const text=await r.text();await useText(text,'CelesTrak · GNSS',Date.now());try{localStorage.setItem(KEY+'-fetch-at',String(Date.now()));}catch(e){} }catch(e){note(tr('fetch_failed_prefix')+e.message+tr('fetch_failed_suffix'));}finally{busy=false;downloadState();}}
 function exportCatalog(){if(!catalog)return;const blob=new Blob([catalog.text],{type:'text/plain'}),url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=catalog.format==='TLE'?'tracker-orbits.tle':'tracker-orbits.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
 function changeZoom(value){zoom=Math.max(.5,Math.min(2.2,value));$('orbit-zoom').value=String(Math.round(zoom*100));schedule();}
 function saveDisplay(){try{localStorage.setItem(KEY+'-display',JSON.stringify({receptionFilter:pausedFilter||receptionFilter,aboveOnly,highlight,horizonLine,mark:$('orbit-horizon-mark').checked}));}catch(e){}}
 function init(){
  try{const c=JSON.parse(localStorage.getItem(KEY+'-display')||'{}');if(['all','received','used','uncertain'].includes(c.receptionFilter))receptionFilter=c.receptionFilter;aboveOnly=c.aboveOnly===true;if(typeof c.highlight==='boolean')highlight=c.highlight;if(typeof c.horizonLine==='boolean')horizonLine=c.horizonLine;$('orbit-horizon-mark').checked=c.mark!==false;}catch(e){}
  $('orbit-reception-filter').value=receptionFilter;$('orbit-above-only').checked=aboveOnly;$('orbit-highlight').checked=highlight;$('orbit-horizon-line').checked=horizonLine;
  for(const id of ['orbit-reception-filter','orbit-above-only','orbit-highlight','orbit-horizon-line','orbit-horizon-mark'])$(id).addEventListener('change',()=>{if(id==='orbit-reception-filter')pausedFilter=null;receptionFilter=$('orbit-reception-filter').value;aboveOnly=$('orbit-above-only').checked;highlight=$('orbit-highlight').checked;horizonLine=$('orbit-horizon-line').checked;saveDisplay();schedule();});
  const systemButtons=new Map(names.map(name=>[name,[]]));
  for(const id of ['orbit-systems','orbit-systems-map'])for(const [i,name]of names.entries()){
   const button=document.createElement('button');button.type='button';button.textContent='● '+systemLabel(name);button.style.color=colors[i];button.setAttribute('aria-pressed',String(enabled.has(name)));systemButtons.get(name).push(button);
   button.addEventListener('click',()=>{enabled.has(name)?enabled.delete(name):enabled.add(name);for(const sibling of systemButtons.get(name))sibling.setAttribute('aria-pressed',String(enabled.has(name)));hideHover();options();});$(id).append(button);
  }
  $('orbit-download').addEventListener('click',download);$('orbit-export').addEventListener('click',exportCatalog);
  $('orbit-file').addEventListener('change',async()=>{const file=$('orbit-file').files[0];if(!file)return;try{if(file.size>1000000)throw Error(tr('file_large'));await useText(await file.text(),tr('file_source')+file.name);}catch(e){note(tr('import_rejected_prefix')+e.message+tr('import_rejected_suffix'));}finally{$('orbit-file').value='';}});
  $('orbit-import').addEventListener('click',async()=>{try{await useText($('orbit-paste').value,tr('pasted_source'));}catch(e){note(tr('import_rejected_prefix')+e.message+tr('import_rejected_suffix'));}});
  $('orbit-satellite').addEventListener('change',()=>{hideHover();chooseSatellite($('orbit-satellite').value);});$('orbit-view').addEventListener('change',()=>{mode=$('orbit-view').value;schedule();});$('orbit-span').addEventListener('change',()=>{span=Number($('orbit-span').value);pathCache=null;schedule();});$('orbit-trail').addEventListener('change',()=>{trails=$('orbit-trail').checked;schedule();});$('orbit-grid').addEventListener('change',()=>{grid=$('orbit-grid').checked;schedule();});$('orbit-zoom').addEventListener('input',()=>changeZoom(Number($('orbit-zoom').value)/100));$('orbit-time').addEventListener('input',()=>setOffset($('orbit-time').value));$('orbit-now').addEventListener('click',()=>setOffset(0));
  $('orbit-time-back').addEventListener('click',()=>setOffset(offset-5));$('orbit-time-forward').addEventListener('click',()=>setOffset(offset+5));$('orbit-filter-now').addEventListener('click',()=>setOffset(0));document.querySelectorAll('[data-orbit-live]').forEach(button=>button.addEventListener('click',()=>setOffset(0)));
  $('orbit-reset').addEventListener('click',()=>{camera={lon:10*rad,lat:25*rad};changeZoom(1);});$('orbit-home').addEventListener('click',()=>{const o=observer();if(o){camera={lon:o.longitude,lat:o.latitude};schedule();}});
  $('orbit-fullscreen').addEventListener('click',async()=>{try{if(document.fullscreenElement)await document.exitFullscreen();else await $('orbit-workspace').requestFullscreen();}catch(e){note(tr('fullscreen_unavailable'));}schedule();});document.addEventListener('fullscreenchange',schedule);
  const c=$('orbit-globe');c.addEventListener('pointerdown',e=>{hideHover();drag={x:e.clientX,y:e.clientY,lon:camera.lon,lat:camera.lat,moved:false};c.setPointerCapture(e.pointerId);});
  c.addEventListener('pointermove',e=>{if(!drag){if(e.pointerType!=='touch')hoverEvent('globe',e);return;}const dx=e.clientX-drag.x,dy=e.clientY-drag.y;drag.moved=drag.moved||Math.hypot(dx,dy)>5;camera={lon:drag.lon-dx*.007,lat:Math.max(-1.5,Math.min(1.5,drag.lat+dy*.007))};schedule();});
  c.addEventListener('pointerup',e=>{if(drag&&!drag.moved)hoverEvent('globe',e,true);drag=null;schedule();});c.addEventListener('pointercancel',()=>{drag=null;hideHover();});c.addEventListener('pointerleave',e=>{if(e.pointerType!=='touch'&&!drag)hideHover();});
  c.addEventListener('wheel',e=>{e.preventDefault();hideHover();changeZoom(zoom*(e.deltaY<0?1.08:1/1.08));},{passive:false});
  c.addEventListener('keydown',e=>{if(e.key==='Escape'){hideHover();return;}if(!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','+','-'].includes(e.key))return;e.preventDefault();hideHover();if(e.key==='+')changeZoom(zoom*1.1);else if(e.key==='-')changeZoom(zoom/1.1);else{camera.lon+=(e.key==='ArrowRight'?.12:e.key==='ArrowLeft'?-.12:0);camera.lat=Math.max(-1.5,Math.min(1.5,camera.lat+(e.key==='ArrowUp'?.12:e.key==='ArrowDown'?-.12:0)));schedule();}});
  const m=$('orbit-map');m.addEventListener('pointermove',e=>{if(e.pointerType!=='touch')hoverEvent('map',e);});m.addEventListener('pointerleave',e=>{if(e.pointerType!=='touch')hideHover();});m.addEventListener('pointerdown',e=>{mapGesture={x:e.clientX,y:e.clientY};});m.addEventListener('pointerup',e=>{if(mapGesture&&Math.hypot(e.clientX-mapGesture.x,e.clientY-mapGesture.y)<=6)hoverEvent('map',e,true);mapGesture=null;});m.addEventListener('pointercancel',()=>{mapGesture=null;hideHover();});m.addEventListener('keydown',e=>{if(e.key==='Escape')hideHover();if(!['ArrowLeft','ArrowRight'].includes(e.key)||!points.length)return;e.preventDefault();const i=points.findIndex(p=>p.id===selected),next=points[(i+(e.key==='ArrowRight'?1:-1)+points.length)%points.length];hideHover();chooseSatellite(next.id);});
  document.addEventListener('pointerdown',e=>{if(e.target!==c&&e.target!==m)hideHover();});window.addEventListener('scroll',hideHover,{passive:true});

  window.addEventListener('resize',schedule);setInterval(()=>{if(active&&performance.now()-lastFrame>900)schedule();},1000);
 }
 let restored=false;
 async function showTab(name){active=name==='orbits';if(!active){hideHover();return;}try{await ensureAssets();}catch(e){note(e.message);return;}if(!restored){restored=true;const cached=await cacheRead();if(cached?.text)try{await useText(cached.text,cached.source,cached.fetchedAt,false);}catch(e){note(tr('invalid_cached_catalog'));}}schedule();}
 function update(r,p){raw=r;position=p;received=performance.now();if(active)schedule();}
 document.addEventListener('DOMContentLoaded',init);
 function languageChanged(){optionsSignature='';lastDetail='';hoverSignature='';for(const id of ['orbit-systems','orbit-systems-map'])for(const [i,name]of names.entries()){const button=$(id).children[i];if(button)button.textContent='● '+systemLabel(name);}if(cacheSaved!==null)$('orbit-cache-note').textContent=cacheSaved?tr('catalog_cached'):tr('catalog_not_cached');$('orbit-unmatched').dataset.signature='';if(catalog&&records.length&&!busy){const epochs=records.map(r=>r.epoch),oldest=new Date(Math.min(...epochs)).toISOString().slice(0,19).replace('T',' '),newest=new Date(Math.max(...epochs)).toISOString().slice(0,19).replace('T',' ');note(tr('catalog_loaded',{count:records.length,format:catalog.format,source:catalog.source,oldest,newest}));}downloadState();if(active)render();}
 return {update,showTab,languageChanged};
})();
