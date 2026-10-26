/* Browser-only sky trails/signal history and device diagnostics. */
'use strict';
const TrackerExtras=(()=>{
 const tr=(key,params)=>TrackerI18n.t('extras.'+key,params);
 const $=id=>document.getElementById(id),names=['GPS','GLONASS','Galileo','BeiDou','QZSS','SBAS','NavIC'],colors=['#75d8ff','#e9a1ff','#9debc0','#ffe09b','#ff9a7d','#afbcff','#f5a9ca'];
 let raw=null,position=null,received=0,tab='live',selectedKey='',lastMemory=-Infinity,memory=[],lastUptime=0;
 let lastSkySample=-Infinity,skyHistory=new Map(),windowMs=300000,trails=true,fullscreenFallback=false,savedOverflow='';
 const enabled=new Set(names);let signalsOnly=false;
 function svg(tag,attrs={},text){const n=document.createElementNS('http://www.w3.org/2000/svg',tag);for(const [k,v]of Object.entries(attrs))n.setAttribute(k,String(v));if(text!==undefined)n.textContent=text;return n;}
 function kib(n){return Number.isFinite(n)?(n/1024).toFixed(1)+' KiB':'—';}
 function seconds(ms){return Number.isFinite(ms)?(ms/1000).toFixed(1)+' s':'—';}
 function save(){try{localStorage.setItem('tracker-sky-view',JSON.stringify({systems:[...enabled],signalsOnly,trails,windowMs,selectedKey}));}catch(error){}}
 function skyPoint(elevation,azimuth){const r=(90-elevation)/90*240,a=azimuth*Math.PI/180;return {x:300+r*Math.sin(a),y:300-r*Math.cos(a)};}
 function validRow(r){return Array.isArray(r)&&r.length===6&&Number.isInteger(r[0])&&r[0]>=1&&r[0]<=7&&Number.isInteger(r[1])&&r[1]>0&&Number.isInteger(r[5])&&r[5]>=0&&r[5]<=7&&(r[2]===null||Number.isFinite(r[2])&&r[2]>=0&&r[2]<=90)&&(r[3]===null||Number.isFinite(r[3])&&r[3]>=0&&r[3]<360)&&(r[4]===null||Number.isFinite(r[4])&&r[4]>=0&&r[4]<=99);}
 function fresh(){return raw&&Number.isFinite(raw.diagnostics?.gnss_age_ms)&&raw.diagnostics.gnss_age_ms+performance.now()-received<15000;}
 function label(row){const system=names[row[0]-1],l=typeof satelliteLabel==='function'?satelliteLabel({system,nmea_id:row[1]}):{id:system+' '+row[1],name:''};return {system,...l};}
 const keyOf=row=>row[0]+':'+row[1];
 function collectSky(at){
  if(at<lastSkySample){skyHistory.clear();lastSkySample=-Infinity;}
  if(!fresh()||at-lastSkySample<5000)return;lastSkySample=at;
  const rows=(raw.sky||[]).filter(validRow),active=new Set(rows.map(keyOf));
  for(const row of rows){const key=keyOf(row);let item=skyHistory.get(key);if(!item){item={row,samples:[]};skyHistory.set(key,item);}item.row=row;item.samples.push({at,el:row[2],az:row[3],cn:row[4],flags:row[5]});}
  for(const [key,item]of skyHistory){item.samples=item.samples.filter(p=>at-p.at<=1800000).slice(-361);if(!item.samples.length)skyHistory.delete(key);}
  while(skyHistory.size>96){const candidates=[...skyHistory].filter(([key])=>!active.has(key)).sort((a,b)=>a[1].samples.at(-1).at-b[1].samples.at(-1).at);if(!candidates.length)break;skyHistory.delete(candidates[0][0]);}
 }
 function details(row,live=true){const l=label(row);$('sky-detail').textContent=l.system+' · '+l.id+(l.name?' · '+l.name:'')+(live?tr('angles',{elevation:row[2]??'—',azimuth:row[3]??'—',signal:row[4]===null?tr('signal_missing'):row[4]+' dB-Hz'})+' · '+(!(row[5]&4)?tr('usage_unknown'):row[5]&2?tr('used_gsa'):tr('not_used')):tr('stale_detail'));}
 function selectSatellite(row){selectedKey=keyOf(row);save();renderSky();}
 function trailPath(samples,end){let path='',prev=null;for(const p of samples){if(p.at<end-windowMs)continue;if(!Number.isFinite(p.el)||!Number.isFinite(p.az)){prev=null;continue;}const q=skyPoint(p.el,p.az);path+=(prev&&p.at-prev.at<=15000?'L':'M')+q.x.toFixed(2)+' '+q.y.toFixed(2)+' ';prev=p;}return path;}
 function labelLayout(nodes){
  const placed=[],points=nodes.map(n=>({left:n.point.x-n.r-3,right:n.point.x+n.r+3,top:n.point.y-n.r-3,bottom:n.point.y+n.r+3,key:keyOf(n.row)})),result=new Map();
  const intersects=(a,b)=>a.left<b.right&&a.right>b.left&&a.top<b.bottom&&a.bottom>b.top;
  const ordered=nodes.slice().sort((a,b)=>(keyOf(b.row)===selectedKey)-(keyOf(a.row)===selectedKey)||(b.row[5]&2)-(a.row[5]&2)||(b.row[4]||0)-(a.row[4]||0)||keyOf(a.row).localeCompare(keyOf(b.row)));
  for(const n of ordered){const {point,l}=n,key=keyOf(n.row),w=l.id.length*7+6,h=15;let box=null;
   for(const [dx,dy]of [[13,-7],[-w-13,-7],[-w/2,-25],[-w/2,16],[16,-26],[-w-16,16],[28,-7],[-w-28,-7]]){const candidate={left:point.x+dx,right:point.x+dx+w,top:point.y+dy,bottom:point.y+dy+h};if(candidate.left<12||candidate.right>588||candidate.top<15||candidate.bottom>582||placed.some(b=>intersects(candidate,b))||points.some(b=>b.key!==key&&intersects(candidate,b)))continue;box=candidate;break;}
   if(!box&&key===selectedKey){const x=Math.max(12,Math.min(582-w,point.x+14)),y=Math.max(15,Math.min(567,point.y-24));box={left:x,right:x+w,top:y,bottom:y+h};}
   if(box){placed.push(box);result.set(key,box);}
  }return result;
 }
 function renderSignal(){
  const plot=$('sky-signal');plot.replaceChildren();plot.setAttribute('viewBox','0 0 640 210');
  const item=skyHistory.get(selectedKey),end=lastUptime*1000,start=end-windowMs,samples=(item?.samples||[]).filter(p=>p.at>=start),max=Math.max(60,Math.ceil(samples.reduce((a,p)=>Math.max(a,p.cn??0),0)/10)*10),x=t=>42+(t-start)/windowMs*580,y=v=>177-v/max*155;
  for(let i=0;i<=3;i++){const v=max*i/3;plot.append(svg('line',{x1:42,x2:622,y1:y(v),y2:y(v),class:'chart-grid'}),svg('text',{x:34,y:y(v)+4,fill:'#a0b4c6','text-anchor':'end','font-size':12},Math.round(v)));}
  for(let i=0;i<=2;i++)plot.append(svg('text',{x:42+290*i,y:202,fill:'#a0b4c6','font-size':12,'text-anchor':i===0?'start':i===2?'end':'middle'},i===2?tr('now'):'−'+(windowMs/60000*(1-i/2))+' min'));
  let path='',prev=null,values=0;
  for(let at=0;at<samples.length;at++){const p=samples[at];if(p.cn===null){prev=null;continue;}path+=(prev&&p.at-prev.at<=15000?'L':'M')+x(p.at).toFixed(2)+' '+y(p.cn).toFixed(2)+' ';prev=p;values++;if((at===0||samples[at-1].cn===null||p.at-samples[at-1].at>15000)&&(at===samples.length-1||samples[at+1].cn===null||samples[at+1].at-p.at>15000))plot.append(svg('circle',{cx:x(p.at),cy:y(p.cn),r:3,fill:colors[(item?.row[0]||1)-1]}));}
  if(path)plot.append(svg('path',{d:path,stroke:colors[item.row[0]-1],fill:'none','stroke-width':2}));
  if(!values)plot.append(svg('text',{x:332,y:105,'text-anchor':'middle',fill:'#a0b4c6','font-size':14},item?tr('no_strength'):tr('select_for_history')));
  const live=fresh()&&(raw.sky||[]).find(r=>validRow(r)&&keyOf(r)===selectedKey);
  $('sky-signal-state').textContent=item?label(item.row).system+' · '+label(item.row).id+' · '+tr('signal_points',{count:values})+(live&&live[4]!==null?tr('signal_current',{value:live[4]}):tr('signal_absent')):tr('no_selection');
 }
 function renderSky(){
  const plot=$('sky-plot'),focusKey=document.activeElement?.getAttribute?.('data-sat-key');plot.replaceChildren();
  for(const elevation of [0,30,60]){const r=(90-elevation)/90*240;plot.append(svg('circle',{cx:300,cy:300,r,fill:elevation===0?'#0b1723':'none',stroke:'#34495b','stroke-width':elevation===0?2:1}),svg('text',{x:306,y:300-r+15,fill:'#a0b4c6','font-size':12},elevation+'°'));}
  plot.append(svg('path',{d:'M60 300H540 M300 60V540',stroke:'#263849','stroke-dasharray':'4 6'}),svg('text',{x:300,y:304,fill:'#a0b4c6','font-size':11,'text-anchor':'middle'},'90°'));
  for(const [x,y,text]of [[300,35,'N'],[566,305,tr('east')],[300,575,'S'],[34,305,'W']])plot.append(svg('text',{x,y,fill:'#eef5fb','font-size':20,'text-anchor':'middle','font-weight':650},text));
  const rows=fresh()?(raw.sky||[]).filter(validRow).filter(r=>enabled.has(names[r[0]-1])&&(!signalsOnly||r[4]>0)):[],known=rows.filter(r=>Number.isFinite(r[2])&&Number.isFinite(r[3])),unknown=rows.length-known.length;
  if(!selectedKey&&rows.length){const candidates=rows.slice().sort((a,b)=>(b[0]===3)-(a[0]===3)||(b[4]||0)-(a[4]||0));selectedKey=keyOf(candidates[0]);}
  const select=$('sky-satellite-select'),choices=rows.slice();if(selectedKey&&!choices.some(r=>keyOf(r)===selectedKey)&&skyHistory.has(selectedKey))choices.push(skyHistory.get(selectedKey).row);
  const signature=TrackerI18n.language()+'|'+choices.map(r=>keyOf(r)+':'+label(r).id).sort().join('|');if(select.dataset.signature!==signature){select.replaceChildren();select.append(Object.assign(document.createElement('option'),{value:'',textContent:tr('select_satellite')}));for(const row of choices.slice().sort((a,b)=>a[0]-b[0]||a[1]-b[1])){const l=label(row),option=document.createElement('option');option.value=keyOf(row);option.textContent=l.system+' · '+l.id;select.append(option);}select.dataset.signature=signature;}select.value=selectedKey;
  $('sky-state').textContent=!fresh()?tr('no_fresh_gnss'):tr('angle_counts',{known:known.length,unknown})+(position?.gnss_fix?tr('fix_valid'):tr('no_fix'));
  $('sky-missing').textContent=unknown?tr('missing_angles',{ids:rows.filter(r=>r[2]===null||r[3]===null).map(r=>label(r).id).join(', ')}):'';
  if(!known.length)plot.append(svg('text',{x:300,y:330,fill:'#a0b4c6','text-anchor':'middle','font-size':15},fresh()?tr('no_angles'):tr('waiting_gsv')));
  if(trails)for(const row of known){const key=keyOf(row),samples=skyHistory.get(key)?.samples||[],path=trailPath(samples,lastUptime*1000);if(path)plot.append(svg('path',{d:path,stroke:colors[row[0]-1],fill:'none','stroke-width':key===selectedKey?2.5:1.2,opacity:key===selectedKey?.9:.35,'pointer-events':'none','stroke-linejoin':'round'}));}
  const nodes=known.map(row=>({row,l:label(row),point:skyPoint(row[2],row[3]),r:row[4]>0?Math.min(9,4+row[4]/10):5})),labels=labelLayout(nodes);let focusNode=null;
  for(const n of nodes){const {row,point,l,r}=n,key=keyOf(row),hasSignal=row[4]>0,selected=key===selectedKey,group=svg('g',{tabindex:0,role:'button','aria-label':tr('sat_aria',{system:l.system,id:l.id,elevation:row[2],azimuth:row[3]}),'aria-pressed':selected,'data-sat-key':key,class:'sky-satellite'});
   if(selected)group.append(svg('circle',{cx:point.x,cy:point.y,r:r+8,fill:'none',stroke:'#eef5fb','stroke-width':1,'stroke-dasharray':'2 3'}));
   if(row[5]&2)group.append(svg('circle',{cx:point.x,cy:point.y,r:r+4,fill:'none',stroke:colors[row[0]-1],'stroke-width':2}));
   group.append(svg('circle',{cx:point.x,cy:point.y,r,fill:hasSignal?colors[row[0]-1]:'#0b1723',stroke:colors[row[0]-1],'stroke-width':1.5,opacity:hasSignal?1:.7}));
   const box=labels.get(key);if(box){const lx=box.left+3,ly=box.top+12;group.append(svg('line',{x1:point.x,y1:point.y,x2:Math.max(box.left,Math.min(box.right,point.x)),y2:Math.max(box.top,Math.min(box.bottom,point.y)),stroke:colors[row[0]-1],opacity:.4,'stroke-width':.8}),svg('text',{x:lx,y:ly,fill:colors[row[0]-1],'font-size':12,'font-weight':selected?700:400,stroke:'#0b1723','stroke-width':3,'paint-order':'stroke','stroke-linejoin':'round'},l.id));}
   const choose=()=>selectSatellite(row);group.addEventListener('click',choose);group.addEventListener('focus',()=>{selectedKey=key;save();$('sky-satellite-select').value=key;details(row);renderSignal();});group.addEventListener('pointerenter',()=>{if(!selectedKey){selectedKey=key;details(row);renderSignal();}});group.addEventListener('keydown',e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();choose();}});plot.append(group);if(key===focusKey)focusNode=group;
  }
  if(focusNode?.focus)focusNode.focus({preventScroll:true});
  $('sky-label-note').textContent=known.length>labels.size?tr('labels_hidden',{count:known.length-labels.size}):tr('label_note');
  const current=fresh()?(raw.sky||[]).find(r=>validRow(r)&&keyOf(r)===selectedKey):null,past=skyHistory.get(selectedKey)?.row;if(current)details(current);else if(past)details(past,false);else $('sky-detail').textContent=tr('select_details');
  const oldest=[...skyHistory.values()].reduce((a,item)=>Math.min(a,item.samples[0]?.at??Infinity),Infinity),span=Number.isFinite(oldest)?Math.min(windowMs,Math.max(0,lastUptime*1000-oldest)):0;
  $('sky-history-note').textContent=tr('history_note',{minutes:Math.floor(span/60000),seconds:Math.floor(span/1000)%60});renderSignal();
 }
 function fullscreenState(){const on=fullscreenFallback||document.fullscreenElement===$('sky-card');$('sky-fullscreen').textContent=on?tr('exit_fullscreen'):tr('fullscreen');$('sky-fullscreen').setAttribute('aria-pressed',String(on));}
 function fallbackFullscreen(on){const card=$('sky-card');if(on&&!fullscreenFallback){savedOverflow=document.body.style.overflow;document.body.style.overflow='hidden';}if(!on&&fullscreenFallback)document.body.style.overflow=savedOverflow;fullscreenFallback=on;card.classList.toggle('sky-expanded',on);fullscreenState();}
 async function toggleFullscreen(){const card=$('sky-card');if(fullscreenFallback){fallbackFullscreen(false);return;}if(document.fullscreenElement===card){await document.exitFullscreen();return;}try{if(card.requestFullscreen){await card.requestFullscreen();fullscreenState();return;}}catch(error){}fallbackFullscreen(true);}
 function grid(id,values){const box=$(id);box.replaceChildren();for(const [name,value]of values){const item=document.createElement('div'),dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=name;dd.textContent=value;item.append(dt,dd);box.append(item);}}
 function renderMemory(){
  const plot=$('ram-chart');plot.replaceChildren();const w=900,h=170,left=50,right=15,bottom=25,top=12,max=Math.max(64,...memory.map(p=>p.free/1024),...memory.map(p=>p.largest/1024)),end=lastUptime,span=Math.min(1800,Math.max(60,end-(memory[0]?.up??end))),start=end-span;
  plot.setAttribute('viewBox','0 0 '+w+' '+h);
  const x=t=>left+(t-start)/span*(w-left-right),y=v=>h-bottom-v/1024/max*(h-top-bottom);
  for(let i=0;i<=3;i++){const value=max*i/3;plot.append(svg('line',{x1:left,x2:w-right,y1:y(value*1024),y2:y(value*1024),class:'chart-grid'}),svg('text',{x:left-7,y:y(value*1024)+4,fill:'#a0b4c6','text-anchor':'end','font-size':11},Math.round(value)));}
  for(const [field,color]of [['free','#75d8ff'],['largest','#9debc0'],['minimum','#ffe09b']]){let path='',prev=null;for(const p of memory){path+=(prev&&p.up-prev.up<20?'L':'M')+x(p.up).toFixed(1)+' '+y(p[field]).toFixed(1)+' ';prev=p;}if(path)plot.append(svg('path',{d:path,stroke:color,fill:'none','stroke-width':2}));if(memory.length)plot.append(svg('circle',{cx:x(memory.at(-1).up),cy:y(memory.at(-1)[field]),r:3,fill:color}));}
  plot.append(svg('text',{x:left,y:h-4,fill:'#a0b4c6','font-size':11},'−'+Math.round(span/60)+' min'),svg('text',{x:w-right,y:h-4,fill:'#a0b4c6','text-anchor':'end','font-size':11},tr('now')));
 }
 function logState(state){return ({recording:tr('recording'),waiting_fix:tr('waiting_fix'),waiting_time:tr('waiting_time'),disabled:tr('disabled'),storage_error:tr('storage_error'),busy:tr('busy'),starting:tr('starting')})[state]||state||'—';}
 function renderDiagnostics(){
  if(!raw)return;const d=raw.diagnostics||{},f=raw.firmware||{},m=raw.mqtt||{},wifi=raw.wifi||{},reasons=[tr('unknown'),tr('power_on'),tr('external_reset'),tr('software_restart'),tr('panic'),'Interrupt-Watchdog','Task-Watchdog','Watchdog',tr('deep_sleep'),tr('brownout'),'SDIO-Reset','USB-Reset','JTAG-Reset',tr('efuse_error'),tr('power_glitch'),'CPU-Lockup'];
  $('diag-free').textContent=kib(f.free_heap_bytes);$('diag-minimum').textContent=kib(f.min_free_heap_bytes);$('diag-largest').textContent=kib(f.largest_free_block);
  grid('diag-device',[['Firmware',f.version||'—'],[tr('active_slot'),f.running_slot||'—'],[tr('uptime'),Math.floor((d.uptime_ms||0)/3600000)+' h '+Math.floor((d.uptime_ms||0)/60000)%60+' min'],[tr('restart_reason'),reasons[d.reset_reason]||'Code '+d.reset_reason],[tr('ota_validation'),f.pending_validation?tr('validating'):tr('complete')],[tr('main_stack'),kib(d.main_stack_min_free_bytes)],[tr('http_stack'),kib(d.http_stack_min_free_bytes)],[tr('status_size'),(d.status_published_bytes||0)+' Bytes'],[tr('status_failures'),d.status_build_failures??'—']]);
  grid('diag-services',[[tr('gnss_age'),seconds(Number.isFinite(d.gnss_age_ms)?d.gnss_age_ms+performance.now()-received:NaN)],[tr('gnss_sentences'),raw.gnss_sentences??'—'],[tr('checksum_errors'),raw.checksum_errors??'—'],[tr('sat_overflow'),d.satellite_overflow?tr('yes'):tr('no')],[tr('wifi'),wifi.state==='connected'?tr('connected'):tr('no_ip')],[tr('wifi_drops'),wifi.disconnects??'—'],[tr('wifi_last_reason'),wifi.last_reason||tr('none')],['MQTT',({disabled:tr('off'),connected:tr('connected'),connecting:tr('connecting'),starting:tr('starting')})[m.state]||'—'],[tr('mqtt_connections'),m.connections??0],[tr('mqtt_error'),m.last_error||tr('none')],['LoRaWAN',raw.lora?.joined?tr('session_active'):raw.lora?.join_pending?tr('joining'):tr('no_session')],[tr('track_storage'),logState(raw.track?.state)],[tr('stats_storage'),logState(raw.sat_history?.state)]]);
  const rx=raw.lora?.rx_windows||{},delay=value=>Number.isFinite(value)?value/1000+' s':'—';
  grid('diag-rx',[[tr('rx1_delay'),delay(rx.rx1_delay_ms)],[tr('rx2_delay'),delay(rx.rx2_delay_ms)],[tr('rx1_offset'),rx.rx1_dr_offset??'—'],[tr('rx2_frequency'),Number.isFinite(rx.rx2_frequency_hz)?rx.rx2_frequency_hz/1000000+' MHz':'—'],[tr('rx2_rate'),Number.isFinite(rx.rx2_dr)?'DR'+rx.rx2_dr:'—']]);renderMemory();
 }
 function update(value,p){raw=value;position=p;received=performance.now();const f=raw.firmware||{},up=(raw.diagnostics?.uptime_ms??p.uptime_s*1000)/1000;if(up<lastUptime){memory=[];lastMemory=-Infinity;skyHistory.clear();lastSkySample=-Infinity;}lastUptime=up;if(Number.isFinite(f.free_heap_bytes)&&up-lastMemory>=5){memory.push({up,free:f.free_heap_bytes,minimum:f.min_free_heap_bytes,largest:f.largest_free_block});memory=memory.filter(p=>up-p.up<=1800).slice(-361);lastMemory=up;}collectSky(up*1000);if(tab==='sky')renderSky();if(tab==='diagnostics')renderDiagnostics();}
 function showTab(name){if(name!=='sky'&&fullscreenFallback)fallbackFullscreen(false);tab=name;if(tab==='sky')renderSky();if(tab==='diagnostics')renderDiagnostics();}
 function init(){
  try{const p=JSON.parse(localStorage.getItem('tracker-sky-view')||'null');if(p){if(Array.isArray(p.systems)){enabled.clear();for(const n of p.systems)if(names.includes(n))enabled.add(n);}signalsOnly=!!p.signalsOnly;if(typeof p.trails==='boolean')trails=p.trails;if([300000,900000,1800000].includes(p.windowMs))windowMs=p.windowMs;if(typeof p.selectedKey==='string'&&/^[1-7]:[0-9]{1,5}$/.test(p.selectedKey))selectedKey=p.selectedKey;}}catch(error){}
  for(let i=0;i<names.length;i++){const name=names[i],button=document.createElement('button');button.type='button';button.textContent='● '+name;button.style.color=colors[i];button.setAttribute('aria-pressed',String(enabled.has(name)));button.addEventListener('click',()=>{enabled.has(name)?enabled.delete(name):enabled.add(name);button.setAttribute('aria-pressed',String(enabled.has(name)));save();renderSky();});$('sky-systems').append(button);}
  $('sky-signals-only').checked=signalsOnly;$('sky-signals-only').addEventListener('change',()=>{signalsOnly=$('sky-signals-only').checked;save();renderSky();});
  $('sky-trails').checked=trails;$('sky-trails').addEventListener('change',()=>{trails=$('sky-trails').checked;save();renderSky();});$('sky-window').value=String(windowMs);$('sky-window').addEventListener('change',()=>{const n=Number($('sky-window').value);if([300000,900000,1800000].includes(n)){windowMs=n;save();renderSky();}});
  $('sky-satellite-select').addEventListener('change',()=>{selectedKey=$('sky-satellite-select').value;save();renderSky();});$('sky-fullscreen').addEventListener('click',()=>toggleFullscreen());document.addEventListener('fullscreenchange',fullscreenState);document.addEventListener('keydown',e=>{if(e.key==='Escape'&&fullscreenFallback)fallbackFullscreen(false);});
  setInterval(()=>{if(tab==='sky'&&!fresh())renderSky();if(tab==='diagnostics')renderDiagnostics();},2000);
 }
 document.addEventListener('DOMContentLoaded',init);
 function languageChanged(){fullscreenState();if(raw){if(tab==='sky')renderSky();if(tab==='diagnostics')renderDiagnostics();}}
 return {update,showTab,languageChanged};
})();
