/* Device track synchronization, browser chart history, GNSS clock and tile map. No third-party scripts. */
'use strict';
const TrackerDashboard=(()=>{
 const tr=(key,params)=>TrackerI18n.t('dashboard.'+key,params);
 const $=id=>document.getElementById(id),systems=['GPS','GLONASS','Galileo','BeiDou','QZSS','SBAS','NavIC'];
 const colors=['#75d8ff','#e9a1ff','#9debc0','#ffe09b','#ff9a7d','#afbcff','#f5a9ca'];
 let mqttLoaded=false;
 let selected="Galileo",currentUsed=[];
 let history=[],track=[],latest=null,rawLatest=null,zone='auto',clockReceived=0,clockUtc=null,clockConfirmed=false,clockFresh=false;
 let mapActive=false,center=null,zoom=16,tiles=new Map(),mapFrame=0,drag=null;
 const enabled=new Set(systems);
 const esc=value=>String(value).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[c]));
 function confirmAction(title,message,accept=tr("delete")){
  const dialog=$('confirm-dialog');if(dialog.open)return Promise.resolve(false);
  $('confirm-title').textContent=title;$('confirm-message').textContent=message;$('confirm-accept').textContent=accept;dialog.returnValue='cancel';
  return new Promise(resolve=>{dialog.addEventListener('close',()=>resolve(dialog.returnValue==='accept'),{once:true});dialog.showModal();});
 }
 function historyNotes(){TrackerRecords.renderNotes();storageStats(rawLatest?.track||{});storageStats(rawLatest?.sat_history||{},'history-');}
 function refresh(){history=TrackerRecords.history();track=TrackerRecords.track();historyNotes();renderCharts();if(mapActive)drawMap();}
 function duration(seconds){if(!Number.isFinite(seconds))return '—';if(seconds>=86400)return (seconds/86400).toFixed(1)+tr("days");if(seconds>=3600)return (seconds/3600).toFixed(1)+tr("hours");return Math.max(0,Math.round(seconds/60))+tr("minutes");}
 function storageStats(log,prefix=''){
  const capacity=log.capacity||0,count=log.count||0,percent=capacity?Math.min(100,count/capacity*100):0,label=prefix?tr("statistics_storage"):tr("track_storage"),id=part=>$(prefix+'storage-'+part);
  id('ring').style.background='conic-gradient(var(--cyan) '+percent+'%, var(--border) '+percent+'%)';id('ring').setAttribute('aria-label',tr("storage_percent_used",{percent:percent.toFixed(1),name:label}));id('percent').textContent=log.ready?percent.toFixed(1)+' %':'—';
  const bytes=log.bytes||0,size=bytes>=1048576?(bytes/1048576).toFixed(0)+' MiB':Math.round(bytes/1024)+' KiB';
  id('points').textContent=log.ready?tr("storage_counts",{name:label,count:count.toLocaleString(TrackerI18n.locale()),free:Math.max(0,capacity-count).toLocaleString(TrackerI18n.locale()),size}):label+tr("not_available_yet");
  const format=t=>new Intl.DateTimeFormat(TrackerI18n.locale(),{dateStyle:'short',timeStyle:'medium',timeZone:timezone()}).format(new Date(t));
  const oldest=log.oldest_utc_ms,newest=log.newest_utc_ms;
 id('range').textContent=count?tr("storage_date_range",{oldest:oldest?format(oldest)+(log.oldest_time_estimated?tr('derived_from_gnss'):''):tr('without_gnss_time'),newest:newest?format(newest):tr('without_gnss_time')}):tr("no_samples_stored_yet");
  id('span').textContent=count&&oldest&&newest?tr("stored_time_span")+duration((newest-oldest)/1000):'';
  const estimate=duration((log.slots_before_overwrite??Math.max(0,capacity-count))*log.interval_s);
  id('overwrite').textContent=!log.ready?'':(log.overwriting?tr("ring_buffer_active_the_oldest_data_is_overwritten_in"):(log.enabled?tr("time_until_the_first_overwrite_approximately")+estimate:tr("recording_paused_remaining_capacity_when_resumed")+estimate)+'.')+(prefix?tr("estimate_at_the_current_interval_including_samples_without_a"):tr("estimate_at_the_current_interval_with_a_valid_fix"));
 }
 function renderMqtt(raw){
  const m=raw.mqtt||{},labels={disabled:tr("off"),connected:tr("connected"),connecting:tr("connecting_retrying"),starting:tr("starting")};
  $('mqtt-state').textContent=labels[m.state]||tr("no_status_yet");$('mqtt-state').dataset.state=m.state==='connected'?'used':'unknown';
  $('mqtt-detail').textContent=tr("mqtt_counters",{sent:m.sent_messages||0,connections:m.connections||0,commands:m.commands_received||0})+(m.last_error?tr("last_connection_error")+m.last_error:'');
  $('mqtt-password-note').textContent=m.password_set?tr("an_mqtt_password_is_stored_leave_blank_to_keep"):tr("no_mqtt_password_stored");
  $('mqtt-topics').textContent=m.topic?m.topic+'/state · '+m.topic+'/position · '+m.topic+'/availability · '+m.topic+'/command · '+m.topic+'/command_result':'';
  $('mqtt-save').disabled=false;
  if(!mqttLoaded&&m.topic){mqttLoaded=true;for(const [id,key] of [['host','host'],['port','port'],['interval','interval_s'],['topic','topic'],['username','username']])$('mqtt-'+id).value=m[key]??'';for(const key of ['enabled','discovery','commands'])$('mqtt-'+key).checked=!!m[key];}
 }
 async function saveMqtt(event){
  event.preventDefault();const data={enabled:$('mqtt-enabled').checked,host:$('mqtt-host').value.trim(),port:Number($('mqtt-port').value),interval_s:Number($('mqtt-interval').value),topic:$('mqtt-topic').value.trim(),username:$('mqtt-username').value,discovery:$('mqtt-discovery').checked,commands:$('mqtt-commands').checked};
  if($('mqtt-clear-password').checked)data.password='';if($('mqtt-password').value)data.password=$('mqtt-password').value;
  $('mqtt-save').disabled=true;try{const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),10000);let response;try{response=await fetch('/api/mqtt',{method:'POST',signal:controller.signal,headers:{'Content-Type':'application/json','X-Tracker-Token':rawLatest?.request_token||''},body:JSON.stringify(data)});}finally{clearTimeout(timer);}if(!response.ok)throw new Error(tr("settings_rejected")+response.status+')');$('mqtt-password').value='';$('mqtt-clear-password').checked=false;$('mqtt-note').textContent=tr("saved_mqtt_applies_the_settings_in_the_background");}catch(error){$('mqtt-note').textContent=tr("could_not_save")+error.message;}finally{$('mqtt-save').disabled=false;}
 }
 function syncTrack(force=false){return TrackerRecords.syncTracks(force).then(refresh);}
 function chooseSystem(name){if(!systems.includes(name))return;selected=name;try{localStorage.setItem('tracker-featured-system',name);}catch(error){}renderSystem();if(typeof renderSatellites==='function')renderSatellites();}
 function renderSystem(){
  if(!latest||!rawLatest)return;const index=systems.indexOf(selected),visible=latest.satellites_visible_by_system[selected],used=currentUsed[index],reported=(rawLatest.satellite_packets||[]).length;
  const tracked=(rawLatest._satellites||[]).filter(p=>p.system===selected&&p.cn0_db_hz!==null&&p.cn0_db_hz>0).length;
  const state=latest.gnss_fix&&used>0?'used':visible>0?'visible':'unknown';$('galileo').dataset.state=state;$('galileo-state').dataset.state=state;$('system-title').textContent=selected;
  $('galileo-state').textContent=state==='used'?tr("listed_in_fix"):state==='visible'?tr("satellites_reported"):tr("reception_not_confirmed");$('galileo-visible').textContent=visible??'—';$('galileo-used').textContent=used??'—';
 $('galileo-note').textContent=state==='used'?tr("system_in_fix",{system:selected}):visible>0?tr("system_reported",{system:selected,visible,tracked}):tr("system_no_reception",{system:selected});
  const receiver=rawLatest.gnss_receiver||{},masks=[7,1<<8,(1<<12)|(1<<14),(1<<4)|(1<<6)|(1<<7),(1<<16)|(1<<18),1<<20,1<<24];
  let note=receiver.configuration_known?tr("receiver_configuration")+((receiver.mask&masks[index])?tr("enabled"):tr("not_enabled")):tr("receiver_configuration_not_confirmed_yet");
  if(selected==='QZSS')note+=tr("the_uc6580_supports_qzss_with_coverage_focused_on_japan");
  if(selected==='SBAS'){
   const config=receiver.configuration_known?(receiver.mask&(1<<20)?tr("set"):tr("not_set")):tr("unknown");
   note=tr("sbas_configuration_bit")+config+tr("confirmed_reception")+tracked+tr("signals_with_current_c_n")+(used>0?tr("gsa_reports")+used+tr("sbas_identifiers_in_its_list_of_satellites_used"):'')+tr("this_does_not_confirm_that_sbas_egnos_corrections_are");
   if(used>0&&receiver.configuration_known&&!(receiver.mask&(1<<20)))note+=tr("the_gsa_list_and_configuration_bit_disagree_check_the");
  }
  if(selected==='NavIC')note+=tr("navic_l5_requires_specific_gnss_firmware_its_use_has");
  $('receiver-note').textContent=note;
  for(const button of $('system-selector').children)button.setAttribute('aria-pressed',String(button.dataset.system===selected));
  const box=$('systems');box.replaceChildren();systems.forEach((name,i)=>{const card=document.createElement('button');card.type='button';card.className='system';card.dataset.selected=String(name===selected);card.setAttribute('aria-pressed',String(name===selected));const label=document.createElement('strong'),value=document.createElement('em'),caption=document.createElement('small');label.textContent=name;value.textContent=(currentUsed[i]??'—')+' / '+(latest.satellites_visible_by_system[name]??'—');caption.textContent=tr("listed_in_fix_reported");card.append(label,value,caption);card.addEventListener('click',()=>chooseSystem(name));box.append(card);});
 }
 function update(raw,p,satellites,used){
  latest=p;rawLatest=raw;rawLatest._satellites=satellites;currentUsed=used;renderSystem();
  const time=raw.gnss_time||{};clockUtc=time.recent&&Number.isFinite(time.utc_ms)?time.utc_ms:null;clockReceived=performance.now();clockFresh=!!time.recent;clockConfirmed=!!time.confirmed;
  TrackerRecords.update(raw,p,satellites,used);refresh();renderClock();renderNetwork(raw.wifi||{});renderMqtt(raw);accuracyCircleNote();
  renderAccuracy();
  if(mapActive&&p.gnss_fix&&$('follow-map').checked){center=project(p.latitude,p.longitude,zoom);drawMap();}
 }
 function renderAccuracy(){
  const accuracy=rawLatest?.accuracy||{};$('accuracy-value').textContent=accuracy.available&&Number.isFinite(accuracy.horizontal_rms_m)?accuracy.horizontal_rms_m.toFixed(1)+' m':tr("not_available");
  $('accuracy-note').textContent=accuracy.available?tr("gst_north")+accuracy.north_sigma_m.toFixed(1)+tr("m_east")+accuracy.east_sigma_m.toFixed(1)+' m'+(Number.isFinite(accuracy.vertical_sigma_m)?tr("altitude")+accuracy.vertical_sigma_m.toFixed(1)+' m':'')+tr("receiver_estimate_not_a_guaranteed_accuracy"):tr("no_recent_valid_gst_error_estimate_available_hdop_is");
 }
 /* Language changes only rebuild presentation from the existing snapshots.
  * In particular, do not call update()/sync(): device history may contain tens
  * of thousands of points and its incremental cursor must survive unchanged. */
 function languageChanged(){
  timezoneOptions();renderClock();renderSystem();refresh();renderAccuracy();accuracyCircleNote();
  $('load-map').textContent=mapActive?tr("centre_on_current_position"):tr("load_map");
  if(rawLatest){
   renderNetwork(rawLatest.wifi||{});
   const saveDisabled=$('mqtt-save').disabled;renderMqtt(rawLatest);$('mqtt-save').disabled=saveDisabled;
   renderLoraTiming(rawLatest);
  }
  if(radioLoaded){radioOptions(selectedRegion(),$('radio-sf').value);radioEstimate();}
  if($('csv-dialog').open){csvDescription();if(csvBusy)$('csv-state').textContent=tr("loading_the_complete_stored_history");else if(csvSnapshot)$('csv-state').textContent=tr("ready_to_export_the_selected_chart_range_is_relative");}
 }
 function timezone(){return zone==='auto'?Intl.DateTimeFormat().resolvedOptions().timeZone:zone;}
 function renderClock(){
  const fresh=clockUtc!==null&&clockFresh&&performance.now()-clockReceived<10000,confirmed=fresh&&clockConfirmed;
  const value=new Date(fresh?clockUtc+performance.now()-clockReceived:Date.now()),primary=new Date(confirmed?value.getTime():Date.now());
  $('clock-time').textContent=(confirmed?'GNSS':'Browser')+' · '+new Intl.DateTimeFormat(TrackerI18n.locale(),{hour:'2-digit',minute:'2-digit',second:'2-digit',hourCycle:'h23',timeZone:timezone()}).format(primary)+' ⚙';
  $('clock-date').textContent=new Intl.DateTimeFormat(TrackerI18n.locale(),{weekday:'short',day:'2-digit',month:'2-digit',year:'numeric',timeZone:timezone()}).format(primary);
  $('clock-button').dataset.source=confirmed?'gnss':'browser';$('clock-button').title=(confirmed?tr("confirmed_gnss_time"):tr("using_browser_time_no_current_confirmed_gnss_time"))+' · '+timezone();
  $('gnss-clock').textContent=fresh?value.toISOString().replace('T',' ').slice(0,19)+' UTC':tr("no_current_gnss_time");
  $('local-clock').textContent=fresh?new Intl.DateTimeFormat(TrackerI18n.locale(),{dateStyle:'short',timeStyle:'medium',timeZone:timezone()}).format(value):'—';
  $('clock-quality').textContent=(confirmed?tr("gnss_time_confirmed"):fresh?tr("gnss_time_unconfirmed_browser_time_is_shown_above"):tr("browser_time_is_shown_above_as_a_fallback"))+tr("time_zone")+timezone()+'.';
 }
 function setZone(value){try{new Intl.DateTimeFormat(TrackerI18n.locale(),{timeZone:value==='auto'?undefined:value});zone=value;try{localStorage.setItem('tracker-timezone',zone);}catch(error){}renderClock();refresh();}catch(error){$('timezone-note').textContent=tr("this_browser_does_not_support_that_time_zone");}}
 function timezoneOptions(){if(!TrackerTimezones.fill($('timezone'),zone)){zone='auto';$('timezone').value='auto';}}
 function renderNetwork(wifi){
  const auth=[tr("open"),'WEP','WPA','WPA2','WPA/WPA2','Enterprise','WPA3','WPA2/WPA3'];
  const values=[['Status',wifi.state==='connected'?tr("connected"):tr("no_wi_fi_ip_address")],['SSID',wifi.ssid||'—'],['Signal',wifi.rssi_dbm===undefined?'—':wifi.rssi_dbm+' dBm'],['Access Point / BSSID',wifi.bssid||'—'],[tr("channel"),wifi.channel??'—'],[tr("security"),auth[wifi.auth_mode]||'—'],[tr("ip_address"),wifi.ip||'—'],[tr("subnet_mask"),wifi.netmask||'—'],['Router',wifi.gateway||'—'],['DNS',(wifi.dns||[]).join(' / ')||'—'],[tr("tracker_mac"),wifi.mac||'—'],[tr("connected_for"),wifi.state==='connected'?Math.floor((wifi.connected_seconds||0)/60)+' min '+(wifi.connected_seconds||0)%60+' s':'—'],[tr("last_disconnection_reason"),wifi.last_reason||tr("none")]];
  const box=$('network-details');box.replaceChildren();for(const [name,value]of values){const item=document.createElement('div'),dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=name;dd.textContent=value;item.append(dt,dd);box.append(item);}
 }
 let hoverTime=null;
 function svgElement(name,attrs={},content){const node=document.createElementNS('http://www.w3.org/2000/svg',name);for(const [key,value]of Object.entries(attrs))node.setAttribute(key,String(value));if(content!==undefined)node.textContent=content;return node;}
 function commitChart(target,source){
  for(const name of source.getAttributeNames())if(target.getAttribute(name)!==source.getAttribute(name))target.setAttribute(name,source.getAttribute(name));
  const nodes=[...source.children];for(let i=0;i<nodes.length;i++){
   const next=nodes[i],old=target.children[i];if(!old){target.append(next);continue;}
   if(old.tagName!==next.tagName){target.replaceChild(next,old);continue;}
   for(const name of old.getAttributeNames())if(!next.hasAttribute(name))old.removeAttribute(name);
   for(const name of next.getAttributeNames())if(old.getAttribute(name)!==next.getAttribute(name))old.setAttribute(name,next.getAttribute(name));
   if(old.textContent!==next.textContent)old.textContent=next.textContent;
  }
  while(target.children.length>nodes.length)target.lastElementChild.remove();
 }
 function chart(id,field){
  const target=$(id),svg=svgElement('svg'),w=960,h=240,pad={l:45,r:20,t:20,b:32},end=TrackerRecords.timelineNow()||history.at(-1)?.t||0,duration=Number($('chart-window').value),start=end-duration,points=history.filter(p=>p.t>=start&&p.t<=end);svg.setAttribute('viewBox',`0 0 ${w} ${h}`);svg.setAttribute('preserveAspectRatio','none');
  const values=points.flatMap(p=>p[field].filter((v,i)=>enabled.has(systems[i])&&v!==null));const max=Math.max(5,Math.ceil(values.reduce((a,v)=>Math.max(a,v),0)/5)*5);
  const x=t=>pad.l+(t-start)/duration*(w-pad.l-pad.r),y=v=>h-pad.b-v/max*(h-pad.t-pad.b);
  for(let i=0;i<=4;i++){const v=max*i/4;svg.append(svgElement('line',{x1:pad.l,x2:w-pad.r,y1:y(v),y2:y(v),class:'chart-grid'}),svgElement('text',{x:pad.l-8,y:y(v)+4,fill:'#a0b4c6','text-anchor':'end','font-size':12},Math.round(v)));}
  for(let i=0;i<=4;i++){const t=start+duration*i/4;svg.append(svgElement('text',{x:x(t),y:h-7,fill:'#a0b4c6','text-anchor':i===0?'start':i===4?'end':'middle','font-size':12},new Intl.DateTimeFormat(TrackerI18n.locale(),{hour:'2-digit',minute:'2-digit',timeZone:timezone()}).format(new Date(t))));}
  systems.forEach((name,index)=>{if(!enabled.has(name))return;let path='',previous=null;
   for(const point of points){const value=point[field][index];if(value===null){previous=null;continue;}const connected=previous&&!point.break&&point.t>previous.t&&point.t-previous.t<=(point.interval_s||TrackerRecords.period('history'))*2500;path+=(connected?'L':'M')+x(point.t).toFixed(2)+' '+y(value).toFixed(2)+' ';previous=point;}
   if(path)svg.append(svgElement('path',{d:path,stroke:colors[index],fill:'none','stroke-width':2,'stroke-linejoin':'round'}));
   if(points.length===1&&points[0][field][index]!==null)svg.append(svgElement('circle',{cx:x(points[0].t),cy:y(points[0][field][index]),r:3,fill:colors[index]}));
  });
  if(hoverTime!==null&&hoverTime>=start&&hoverTime<=end){const point=inspectionPoint(hoverTime);const at=point?point.t:hoverTime;svg.append(svgElement('line',{x1:x(at),x2:x(at),y1:pad.t,y2:h-pad.b,class:'chart-cursor','stroke-width':1}));if(point)systems.forEach((name,i)=>{if(enabled.has(name)&&point[field][i]!==null)svg.append(svgElement('circle',{cx:x(point.t),cy:y(point[field][i]),r:4,fill:colors[i],stroke:'#09121c','stroke-width':1}));});}
  const load=TrackerRecords.historyLoad();if(load.loading||!points.length)svg.append(svgElement('text',{x:w/2,y:h/2,fill:load.loading?'#75d8ff':'#a0b4c6','text-anchor':'middle'},load.loading?load.message:load.error?tr("history_not_fully_loaded_see_the_notice_above"):tr("no_samples_stored_for_this_time_range")));commitChart(target,svg);
 }
 function inspectionPoint(time){
  let lo=0,hi=history.length;while(lo<hi){const mid=(lo+hi)>>1;if(history[mid].t<time)lo=mid+1;else hi=mid;}
  const options=[history[lo-1],history[lo]].filter(Boolean);let best=null;for(const point of options)if(!best||Math.abs(point.t-time)<Math.abs(best.t-time))best=point;
  return best&&Math.abs(best.t-time)<=Math.max(1500,(best.interval_s||TrackerRecords.period('history'))*600)?best:null;
 }
 function renderInspector(){
  const tip=$('chart-tooltip');tip.replaceChildren();tip.hidden=hoverTime===null;
  if(hoverTime===null)return;
  const point=inspectionPoint(hoverTime),stamp=new Intl.DateTimeFormat(TrackerI18n.locale(),{dateStyle:'medium',timeStyle:'medium',timeZone:timezone()}).format(new Date(point?point.t:hoverTime));
  for(const destination of [tip]){const title=document.createElement('strong');title.textContent=stamp;destination.append(title);if(!point){const note=document.createElement('p');note.textContent=tr("no_data_at_this_time_gaps_in_recordings_are");destination.append(note);continue;}
   const caption=document.createElement('div');caption.textContent=tr("reported_listed_in_fix_total_used_in_fix")+(point.total??'—')+(point.time_source==='browser_legacy'?tr("legacy_data_with_browser_time"):point.time_estimated?tr("time_determined_later_using_the_gnss_clock"):point.time_confirmed===false?tr("gnss_time_unconfirmed"):tr("gnss_time"));destination.append(caption);
   const row=document.createElement('div');row.className='inspector-values';systems.forEach((name,i)=>{if(!enabled.has(name))return;const entry=document.createElement('span');entry.style.color=colors[i];entry.textContent=name+': '+(point.v[i]??'—')+' / '+(point.u[i]??'—');row.append(entry);});destination.append(row);
  }
 }
 function renderCharts(){chart('visible-chart','v');chart('used-chart','u');renderInspector();}
 function cursor(event){
  const svg=event.currentTarget,rect=svg.getBoundingClientRect(),x=(event.clientX-rect.left)/rect.width*960;
  if(x<45||x>940)return;const duration=Number($('chart-window').value);hoverTime=(TrackerRecords.timelineNow()||history.at(-1)?.t||0)-duration+(x-45)/895*duration;
  const tip=$('chart-tooltip');tip.style.left=Math.max(8,Math.min(window.innerWidth-330,event.clientX+14))+'px';tip.style.top=Math.max(8,Math.min(window.innerHeight-230,event.clientY+14))+'px';renderCharts();
 }
 function mapScale(latitude,level,maxPixels=120){const metersPerPixel=1/accuracyRadiusPixels(latitude,1,level),target=metersPerPixel*maxPixels,power=10**Math.floor(Math.log10(target)),meters=[5,2,1].map(n=>n*power).find(n=>n<=target)||power/2;return {meters,pixels:meters/metersPerPixel,label:meters>=1000?(meters/1000).toLocaleString(TrackerI18n.locale())+' km':meters.toLocaleString(TrackerI18n.locale())+' m'};}
 function drawScale(ctx,width,height){const world=256*2**zoom,latitude=Math.atan(Math.sinh(Math.PI*(1-2*center.y/world)))*180/Math.PI,scale=mapScale(latitude,zoom,Math.min(120,width/3));ctx.save();ctx.font='12px system-ui';const labelWidth=ctx.measureText(scale.label).width,boxWidth=Math.max(scale.pixels,labelWidth)+24;ctx.fillStyle='#09121cdd';ctx.fillRect(10,height-58,boxWidth,46);ctx.strokeStyle='#eef5fb';ctx.fillStyle='#eef5fb';ctx.lineWidth=2;ctx.beginPath();ctx.moveTo(22,height-27);ctx.lineTo(22,height-19);ctx.lineTo(22+scale.pixels,height-19);ctx.lineTo(22+scale.pixels,height-27);ctx.stroke();ctx.fillText(scale.label,22,height-33);ctx.restore();}
 function accuracyRadiusPixels(latitude,meters,level){return meters/(40075016.68557849*Math.cos(Math.max(-85.05112878,Math.min(85.05112878,latitude))*Math.PI/180)/(256*2**level));}
 function accuracyCircleNote(){const a=rawLatest?.accuracy||{};$('accuracy-circle-note').textContent=!$('accuracy-circle').checked?tr("accuracy_circle_hidden_this_preference_is_saved_in_this"):a.available&&latest?.gnss_fix&&Number.isFinite(a.horizontal_rms_m)?tr("circle_radius")+a.horizontal_rms_m.toFixed(1)+tr("m_receiver_estimate_rms_not_a_guaranteed_error_radius"):tr("no_circle_no_current_valid_gst_uncertainty_estimate_available");}
 function project(lat,lon,z){const size=256*2**z,sin=Math.sin(Math.max(-85.05112878,Math.min(85.05112878,lat))*Math.PI/180);return {x:(lon+180)/360*size,y:(.5-Math.log((1+sin)/(1-sin))/(4*Math.PI))*size};}
 function scheduleMap(){if(mapFrame)return;mapFrame=requestAnimationFrame(()=>{mapFrame=0;if(mapActive)drawMap();});}
 function drawMap(){
  if(!mapActive||!center)return;const canvas=$('track-map'),box=$('position-map'),width=box.clientWidth,height=box.clientHeight;if(!width||!height)return;
  const dpr=Math.min(2,devicePixelRatio||1);if(canvas.width!==Math.round(width*dpr)||canvas.height!==Math.round(height*dpr)){canvas.width=Math.round(width*dpr);canvas.height=Math.round(height*dpr);}
  const ctx=canvas.getContext('2d');ctx.setTransform(dpr,0,0,dpr,0,0);ctx.fillStyle='#172b3c';ctx.fillRect(0,0,width,height);
  const left=center.x-width/2,top=center.y-height/2,count=2**zoom;let failures=false;
  for(let y=Math.floor(top/256);y<=Math.floor((top+height)/256);y++)for(let x=Math.floor(left/256);x<=Math.floor((left+width)/256);x++){
   if(y<0||y>=count)continue;const wrap=(x%count+count)%count,key=zoom+'/'+wrap+'/'+y;let tile=tiles.get(key);
   if(!tile){const image=new Image();tile={image,failed:false};tiles.set(key,tile);image.referrerPolicy='strict-origin-when-cross-origin';image.onload=scheduleMap;image.onerror=()=>{tile.failed=true;scheduleMap();};image.src='https://tile.openstreetmap.org/'+key+'.png';}
   if(tile.image.complete&&tile.image.naturalWidth)ctx.drawImage(tile.image,x*256-left,y*256-top,256,256);else if(tile.failed)failures=true;
  }
  while(tiles.size>100)tiles.delete(tiles.keys().next().value);
  const screen=point=>{const pos=project(point.lat,point.lon,zoom),world=256*count;let dx=pos.x-center.x;if(dx>world/2)dx-=world;if(dx< -world/2)dx+=world;return {x:width/2+dx,y:pos.y-top};};
  ctx.strokeStyle='#087ca0';ctx.lineWidth=5;ctx.lineJoin='round';ctx.beginPath();let prev=null;
  for(const point of track){const pos=screen(point);if(!prev||point.break||Math.abs(pos.x-prev.x)>width)ctx.moveTo(pos.x,pos.y);else ctx.lineTo(pos.x,pos.y);prev=pos;}ctx.stroke();ctx.strokeStyle='#75e4ff';ctx.lineWidth=2;ctx.stroke();
  if(latest?.gnss_fix){const pos=screen({lat:latest.latitude,lon:latest.longitude});ctx.beginPath();ctx.arc(pos.x,pos.y,7,0,Math.PI*2);ctx.fillStyle='#d53f58';ctx.fill();ctx.lineWidth=2;ctx.strokeStyle='white';ctx.stroke();}
  const accuracy=rawLatest?.accuracy||{};
  if($('accuracy-circle').checked&&latest?.gnss_fix&&accuracy.available&&Number.isFinite(accuracy.horizontal_rms_m)&&accuracy.horizontal_rms_m>=0){const pos=screen({lat:latest.latitude,lon:latest.longitude}),r=accuracyRadiusPixels(latest.latitude,accuracy.horizontal_rms_m,zoom);ctx.beginPath();ctx.arc(pos.x,pos.y,r,0,Math.PI*2);ctx.fillStyle='#75d8ff30';ctx.fill();ctx.strokeStyle='#75d8ff';ctx.lineWidth=1;ctx.stroke();}
  drawScale(ctx,width,height);
  $('map-note').textContent=(failures?tr("some_map_tiles_could_not_be_loaded_the_track"):'')+'Zoom '+zoom+' · '+track.length+tr("track_points_drag_to_pan_the_map_and_pause");
 }
 function loadMap(){const point=latest?.gnss_fix?{lat:latest.latitude,lon:latest.longitude}:track.at(-1);if(!point){$('map-note').textContent=tr("no_position_loaded_yet_the_tracker_track_will_be");return;}mapActive=true;center=project(point.lat,point.lon,zoom);$('position-map').hidden=false;$('hide-map').hidden=false;$('load-map').textContent=tr("centre_on_current_position");drawMap();}
 function changeZoom(delta){if(!center)return;const next=Math.max(2,Math.min(19,zoom+delta));const factor=2**(next-zoom);center={x:center.x*factor,y:center.y*factor};zoom=next;drawMap();}
 function download(name,type,content){const link=document.createElement('a'),url=URL.createObjectURL(new Blob([content],{type}));link.href=url;link.download=name;document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);}
 function gpx(){let body='',open=false;for(const p of track){if(!open||p.break){if(open)body+='</trkseg>';body+='<trkseg>';open=true;}body+='<trkpt lat="'+p.lat+'" lon="'+p.lon+'">'+(Number.isFinite(p.alt)?'<ele>'+p.alt+'</ele>':'')+'<time>'+new Date(p.t).toISOString()+'</time>'+(p.imported?'<desc>'+esc(tr("imported_browser_track_time_source")+(p.time_source==='gnss'?'GNSS':'Browser'))+'</desc>':'')+'</trkpt>';}if(open)body+='</trkseg>';return '<?xml version="1.0" encoding="UTF-8"?><gpx version="1.1" creator="Tracker V2" xmlns="http://www.topografix.com/GPX/1/1"><trk><name>'+esc('Tracker V2 · '+(TrackerRecords.mode('track')==='browser'?tr("browser_recording"):tr("tracker_recording")))+'</name>'+body+'</trk></gpx>';}
 let csvBusy=false,csvEnd=0,csvSource='',csvSnapshot=null;
 function csvRows(range='all',end=TrackerRecords.timelineNow()||history.at(-1)?.t||0,rows=TrackerRecords.historyRows(true)){return range==='visible'?rows.filter(p=>Number.isFinite(p.t)&&p.t>=end-Number($('chart-window').value)&&p.t<=end):rows;}
 function csv(range='all',filtered=false,end=csvEnd||TrackerRecords.timelineNow()||history.at(-1)?.t||0,rows=csvSnapshot||TrackerRecords.historyRows(true)){const indices=systems.map((n,i)=>i).filter(i=>!filtered||enabled.has(systems[i]));return ['Messzeit_UTC,Zeitquelle,Fix_gueltig,GGA_gesamt,Laufzeit_s,Startkennung,'+indices.map(i=>systems[i]+'_gemeldet,'+systems[i]+'_im_Fix').join(','),...csvRows(range,end,rows).map(p=>(Number.isFinite(p.t)?new Date(p.t).toISOString():'')+','+(!Number.isFinite(p.t)?'Zeit unbekannt':p.time_source==='browser_legacy'?'Browser-Altbestand':p.time_estimated?'GNSS nachtraeglich bestimmt':p.time_confirmed===false?'GNSS unbestaetigt':'GNSS')+','+(typeof p.fix==='boolean'?Number(p.fix):'')+','+(p.total??'')+','+(p.up??'')+','+(p.boot??'')+','+indices.map(i=>(p.v[i]??'')+','+(p.u[i]??'')).join(','))].join('\r\n');}
 function csvDescription(){
  const rows=csvRows($('csv-range').value,csvEnd,csvSnapshot||TrackerRecords.historyRows(true)),dated=rows.filter(p=>Number.isFinite(p.t)),format=t=>new Intl.DateTimeFormat(TrackerI18n.locale(),{dateStyle:'short',timeStyle:'medium',timeZone:timezone()}).format(new Date(t));
  $('csv-source').textContent=tr("data_source")+(TrackerRecords.historySource()==='browser'?tr("this_browser_s_chart_history"):tr("chart_history_on_the_tracker"))+'.';
  const unknown=rows.length-dated.length;$('csv-detail').textContent=tr("csv_sample_count",{count:rows.length})+($('csv-range').value==='visible'?tr("time_range")+format(csvEnd-Number($('chart-window').value))+tr("to")+format(csvEnd):dated.length?' · '+format(dated[0].t)+tr("to")+format(dated.at(-1).t):'')+(unknown?' · '+unknown+tr("without_an_identifiable_time"):'')+'. '+($('csv-filtered').checked?tr("systems")+[...enabled].join(', '):tr("all_satellite_systems_regardless_of_the_chart_filters"))+tr("utc_timestamps_in_the_file");
 }
 let csvTicket=0;
 async function prepareCsv(){
  const ticket=++csvTicket;csvSnapshot=null;csvBusy=true;$('csv-state').textContent=tr($('csv-range').value==='visible'?'loading_selected_chart_range':'loading_the_complete_stored_history');$('csv-download').disabled=true;
  try{if(csvSource==='tracker'){await TrackerRecords.syncHistory(false,$('csv-range').value==='visible');if(TrackerRecords.historyError())throw new Error(TrackerRecords.historyError());}
   if(ticket!==csvTicket)return;refresh();csvSnapshot=TrackerRecords.historyRows(true);csvDescription();$('csv-state').textContent=tr("ready_to_export_the_selected_chart_range_is_relative");$('csv-download').disabled=false;
  }catch(error){if(ticket===csvTicket)$('csv-state').textContent=tr("export_not_ready")+error.message+tr("close_and_reopen_the_dialog");}finally{if(ticket===csvTicket)csvBusy=false;}
 }
 async function openCsv(){if(csvBusy){if(!$('csv-dialog').open)$('csv-dialog').showModal();return;}csvEnd=TrackerRecords.timelineNow()||history.at(-1)?.t||0;csvSource=TrackerRecords.historySource();$('csv-dialog').showModal();csvDescription();await prepareCsv();}
 function exportCsv(){if(csvBusy)return;if(csvSource!==TrackerRecords.historySource()){$('csv-state').textContent=tr("the_data_source_has_changed_please_reopen_the_dialog");return;}const range=$('csv-range').value,rows=csvRows(range,csvEnd,csvSnapshot||TrackerRecords.historyRows(true));if(!rows.length){$('csv-state').textContent=tr("no_samples_in_this_selection");return;}if($('csv-filtered').checked&&!enabled.size){$('csv-state').textContent=tr("enable_at_least_one_satellite_system_or_export_all");return;}download('tracker-diagramm-'+csvSource+'-'+(range==='all'?'gesamt':'zeitraum')+'.csv','text/csv;charset=utf-8',csv(range,$('csv-filtered').checked,csvEnd));$('csv-state').textContent=tr("csv_export_count",{count:rows.length});}
 let lastMessageKey='';
 function messageType(message){if(!message)return '—';const b=message.bytes||[];return message.fport===0?tr("mac_control_data"):message.fport===10?tr("position_status"):message.fport===11?tr("satellite_list")+(b.length>=7?tr("packet")+b[3]+tr("of")+b[4]:''):'FPort '+message.fport;}
 function renderLastMessage(message){
  const key=TrackerI18n.language()+(message?JSON.stringify(message):'');if(key===lastMessageKey)return;lastMessageKey=key;
  const box=$('lora-payload');box.replaceChildren();$('lora-payload-hex').textContent='—';$('lora-payload-json').textContent='—';$('lora-message-summary').textContent='';
  if(!message){$('lora-message-title').textContent=tr("none_since_restart");$('lora-message-result').textContent=tr("waiting_for_transmission");$('lora-message-result').dataset.state='unknown';$('lora-payload-note').textContent=tr("after_the_first_transmission_its_actual_payload_is_shown");return;}
  $('lora-message-title').textContent=messageType(message);$('lora-message-result').textContent=message.success?tr("sent"):tr("transmission_error");$('lora-message-result').dataset.state=message.success?'used':'unknown';
  const bytes=message.bytes||[];$('lora-payload-hex').textContent=bytes.map(n=>n.toString(16).padStart(2,'0').toUpperCase()).join(' ');
  if(message.fport===0){$('lora-message-summary').textContent=tr("lorawan_control_data_without_a_position_packet");$('lora-payload-note').textContent=tr("the_radio_protocol_requires_this_message_the_position_remains");return;}
  const result=typeof decodeUplink==='function'?decodeUplink({fPort:message.fport,bytes}):{errors:[tr("decoder_not_loaded")]};
  $('lora-payload-note').textContent=(message.success?tr("payload_of_the_last_sent_message"):tr("payload_of_the_failed_transmission_attempt"))+' · FPort '+message.fport+' · '+message.length+tr("bytes_counter")+message.counter+tr("these_values_are_from_this_message_rather_than_the");
  if(result.errors){$('lora-message-summary').textContent=tr("the_payload_could_not_be_decoded");$('lora-payload-json').textContent=result.errors.join(', ');return;}
  const data=result.data;$('lora-payload-json').textContent=JSON.stringify(data,null,2);
  const datum=(name,value)=>{const row=document.createElement('div'),label=document.createElement('span'),strong=document.createElement('strong');row.className='lora-datum';label.textContent=name;strong.textContent=value;row.append(label,strong);box.append(row);};
  if(data.message_type==='position'){
   $('lora-message-summary').textContent=data.gnss_fix?tr("position_fix_count",{count:data.satellites_used??'—'}):tr("no_valid_position_fix_at_the_time_of_measurement");
   for(const [name,value]of [['Position',data.gnss_fix?data.latitude.toFixed(6)+' / '+data.longitude.toFixed(6):tr("no_valid_position")],[tr("altitude_f0c36"),data.altitude==null?'—':data.altitude+' m'],[tr("satellites_used_in_fix_reported"),(data.satellites_used??'—')+' / '+(data.satellites_visible??'—')],['HDOP',data.hdop??'—'],[tr("speed"),data.speed_kmh==null?'—':data.speed_kmh.toFixed(1)+' km/h'],[tr("gnss_measurement_time_utc"),data.measurement_utc||'—'],[tr("systems_used"),data.systems_used?(data.systems_used.join(', ')||tr("none_reported")):tr("not_included_in_packet")],[tr("position_sequence"),data.sequence??'—']])datum(name,String(value));
   if(data.compact)$('lora-message-summary').textContent+=tr("compact_packet_limited_space_at_the_current_data_rate");
   for(const row of data.system_counts||[])datum(row.system+tr("reported_listed_in_fix"),(row.satellites_visible??'—')+' / '+(row.satellites_used??'—'));
  }else if(data.message_type==='satellites'){
   $('lora-message-summary').textContent=tr("satellite_packet_counts",{count:data.satellites.length,total:data.satellites_total});
   datum(tr("associated_position_sequence"),String(data.position_sequence));
   const wrap=document.createElement('div'),table=document.createElement('table'),head=document.createElement('tr');wrap.className='table-wrap';for(const title of [tr("system_identifier"),'C/N₀ · dB-Hz',tr("reported"),tr("listed_in_fix")]){const th=document.createElement('th');th.textContent=title;head.append(th);}table.append(head);
   for(const satellite of data.satellites){const row=document.createElement('tr'),label=typeof satelliteLabel==='function'?satelliteLabel(satellite).id:'NMEA '+satellite.nmea_id;for(const value of [satellite.system+' · '+label,satellite.cn0_db_hz??'—',satellite.visible?tr("yes"):tr("no"),satellite.used_in_fix===null?tr("unknown_d0b00"):satellite.used_in_fix?tr("yes"):tr("no")]){const cell=document.createElement('td');cell.textContent=String(value);row.append(cell);}table.append(row);}wrap.append(table);box.append(wrap);
  }
 }
 let radioLoaded=false,radioRaw=null;
 function radioAirtime(sf,bytes,bw=125){
  const symbol=Math.pow(2,sf)/(bw*1000),de=bw===125&&sf>=11?1:0;
  const payload=8+Math.max(Math.ceil((8*(bytes+13)-4*sf+28+16)/(4*(sf-2*de)))*5,0);
  return (8+4.25+payload)*symbol;
 }
 const radioRegions=['EU868','US915','AU915','IN865','KR920','AS923-1','AS923-2','AS923-3','AS923-4','RU864'];
 function selectedRegion(){const index=radioRegions.indexOf($('radio-region').value);return index<0?0:index;}
 function radioHasDwell(r){return r===2||r>=5&&r<=8;}
 function radioHasFsb(r){return r===1||r===2;}
 function radioRate(r,dr){if(r===1){if(dr>=0&&dr<=3)return {sf:10-dr,bw:125};if(dr===4)return {sf:8,bw:500};}else if(dr>=0&&dr<=5)return {sf:12-dr,bw:125};else if(dr===6&&r!==4)return {sf:r===2?8:7,bw:r===2?500:250};if(radioHasFsb(r)&&dr>=8&&dr<=13)return {sf:20-dr,bw:500};return null;}
 function selectedRate(){const v=$('radio-sf').value,wide=v.endsWith('w')?1:0;return {sf:parseInt(v,10)||7,wide,bw:wide?500:125};}
 function radioOptions(region,selection){
  const sf=selection||$('radio-sf').value||'7',dwell=radioHasDwell(region)&&$('radio-dwell').checked;$('radio-sf').replaceChildren();
  const values=Array.from({length:(region===1||dwell?10:12)-6},(_,i)=>String(7+i));if(radioHasFsb(region))values.push('8w');
  for(const v of values){const option=document.createElement('option'),wide=v.endsWith('w')?1:0,value=parseInt(v,10),dr=region===1?(wide?4:10-value):wide?6:12-value;option.value=v;option.textContent='SF'+value+' / '+(wide?500:125)+' kHz · DR'+dr;$('radio-sf').append(option);}
  $('radio-sf').value=values.includes(sf)?sf:'7';$('radio-block-label').hidden=!radioHasFsb(region);$('radio-block-title').textContent=(radioRegions[region]||'')+tr("channel_group");$('radio-dwell-label').hidden=!radioHasDwell(region);$('radio-dwell-note').hidden=!radioHasDwell(region)&&region!==4;
  const experimental=region||radioRaw?.lora?.settings?.region||0;$('radio-us-warning').hidden=experimental===0;$('radio-us-warning').textContent=(radioRegions[experimental]||tr("this_region"))+tr("is_experimental_software_tests_passed_join_uplink_and_downlink");
 }
 function radioProjection(sf,interval,profile,region=0,bw=125,available,dwell=false){const wanted=Number(profile)===0?23:Number(profile)===2?38:24;let bytes=(region===1||radioHasDwell(region)&&dwell)&&sf===10&&bw===125?11:wanted;if(Number.isFinite(available)&&available>=9&&available<bytes)bytes=available>=11?11:9;const position=radioAirtime(sf,bytes,bw);return {bytes,wanted,position,total:position*(86400/interval)};}
 function radioBudget(id,total){
 $(id).textContent=total>30?tr("ttn_budget_over",{percent:(total/30*100).toFixed(0)}):tr("ttn_budget_estimate",{percent:(total/30*100).toFixed(0)});
  $(id).classList.toggle('warning',total>30);
 }
 function radioEstimate(){
  if(!$('radio-sf'))return;
  const rate=selectedRate(),sf=rate.sf,interval=Number($('radio-interval').value)||300,region=selectedRegion();
  const preview=radioProjection(sf,interval,Number($('radio-profile').value),region,rate.bw,undefined,radioHasDwell(region)&&$('radio-dwell').checked);
 $('radio-interval-summary').textContent=tr("position_interval_preview",{bytes:preview.bytes,compact:preview.bytes<preview.wanted?tr("compact_packet"):'',minutes:interval/60});
  $('radio-estimate').textContent=tr("radio_airtime_preview",{sf,bandwidth:rate.bw,milliseconds:Math.round(preview.position*1000),seconds:preview.total.toFixed(1)});
  radioBudget('radio-budget',preview.total);
  const lora=radioRaw?.lora||{},saved=lora.settings||{},currentSf=lora.sf,bw=lora.bandwidth_khz;
  $('radio-adr-warning').hidden=!($('radio-adr').checked||lora.adr===true);
  const valid=Number.isInteger(currentSf)&&currentSf>=7&&currentSf<=(saved.region===1?10:12)&&([125,250].includes(bw)||radioHasFsb(saved.region)&&bw===500);
  $('radio-current-budget').hidden=!valid;
  $('radio-current-mode').textContent=typeof lora.adr==='boolean'?(lora.adr?tr("adr_enabled_saved_initial_value_sf")+(saved.sf??'—')+tr("the_current_data_rate_may_change"):tr("adr_off_fixed_data_rate")):tr("adr_status_not_available_yet");
  if(!valid){$('radio-current-rate').textContent=tr("current_data_rate_not_available_yet");$('radio-current-estimate').textContent='';$('radio-current-note').textContent=tr("the_current_estimate_requires_valid_radio_status_for_the");return;}
  const currentInterval=saved.position_interval_s||300,nextDr=saved.next_packet_dr,projectedRate=Number.isInteger(nextDr)?radioRate(saved.region??0,nextDr)||{sf:currentSf,bw}:{sf:currentSf,bw},current=radioProjection(projectedRate.sf,currentInterval,saved.packet_profile??1,saved.region??0,projectedRate.bw,saved.next_packet_bytes,!!saved.uplink_dwell);
  $('radio-current-rate').textContent='SF'+currentSf+' / '+bw+' kHz'+(Number.isInteger(lora.dr)?' · DR'+lora.dr:'');
  $('radio-current-estimate').textContent=saved.next_packet_bytes===0?tr("mac_control_data_is_queued_first_position_follows_when"):Math.round(current.position*1000)+tr("ms_per_position_packet_approx")+current.total.toFixed(1)+tr("s_per_day");
  radioBudget('radio-current-budget',current.total);
  $('radio-current-note').textContent=tr("saved")+(saved.region_name||'EU868')+tr("position_every")+(currentInterval/60)+tr("minutes_61aa7")+([tr("basic"),'Standard',tr("system_details")][saved.packet_profile??1])+' · '+current.bytes+tr("bytes")+(current.bytes<current.wanted?tr("compact_packet"):'')+'. '+(projectedRate.sf!==currentSf||projectedRate.bw!==bw?tr("next_uplink_expected_at_sf")+projectedRate.sf+' / '+projectedRate.bw+' kHz. ':'')+(saved.apply_pending?tr("a_radio_parameter_change_is_still_pending"):'')+(lora.adr?tr("network_commands_and_automatic_fallback_events_are_shown_in"):'')+tr("snapshot_not_measured_daily_usage");
 }
 function radioUpdate(raw){
  radioRaw=raw;const c=raw.lora?.settings;if(!c)return;
  if(!radioLoaded){$('radio-region').value=radioRegions[c.region??0]||'EU868';$('radio-block').value=String(radioHasFsb(c.region)?c.channel_block:2);$('radio-dwell').checked=!!c.dwell;radioOptions(c.region??0,String(c.sf)+(c.wideband?'w':''));$('radio-interval').value=String(c.position_interval_s||300);$('radio-adr').checked=c.adr;$('radio-profile').value=String(c.packet_profile??1);radioLoaded=true;}
  if(c.restart_pending)$('radio-note').textContent=tr("network_change_saved_the_tracker_restarts_after_any_ongoing");
  else if(c.apply_failed)$('radio-note').textContent=tr("settings_saved_applying_them_will_be_retried");
  else if(c.apply_pending)$('radio-note').textContent=tr("saved_will_be_applied_after_the_ongoing_radio_transmission");
  else if(!$('radio-form').dataset.dirty)$('radio-note').textContent=tr("active")+(c.region_name||'EU868')+(radioHasFsb(c.region)?' · '+(c.channel_block?'FSB'+c.channel_block:tr("all_channels")):'')+' · '+(c.adr?tr("adr_initial_value_sf")+c.sf:'SF'+c.sf+tr("fixed"))+tr("position_every")+((c.position_interval_s||300)/60)+tr("minutes_61aa7")+([tr("basic"),'Standard',tr("system_details")][c.packet_profile??1]);
  const current=raw.lora?.current_power_dbm??raw.lora?.tx_power?.target_dbm,baseline=c.baseline_dbm??14;
  $('radio-power-current').textContent=Number.isFinite(current)?current+' dBm · '+Math.pow(10,current/10).toFixed(1)+' mW':tr("not_available_yet_abfc8");
  $('radio-power-note').textContent=tr("initial_value")+baseline+' dBm · '+(Number.isFinite(current)?(current<baseline?tr("power_reduced",{difference:baseline-current}):current===baseline?tr("no_reduction"):tr("difference")+(current-baseline)+' dB'):tr("difference_not_known_yet"))+' · '+(raw.lora?.adr?tr("adr_enabled"):tr("adr_off"))+tr("target_values_not_measured_rf_power");
  $('radio-reset').disabled=!raw.request_token||c.reset_pending||c.apply_pending||c.restart_pending;
  if(c.reset_pending)$('radio-reset-result').textContent=tr("reset_is_waiting_for_an_ongoing_radio_transmission_to");
  else if(c.reset_count)$('radio-reset-result').textContent=tr("radio_parameters_reset_with_adr_enabled_the_network_may");
  radioEstimate();
 }
 async function resetRadio(){
  if(!await confirmAction(tr("reset_radio_parameters"),tr("restore_the_saved_initial_sf_and_the_region_s"),tr("reset")))return;
  $('radio-reset').disabled=true;
  try{const response=await fetch('/api/lora/reset',{method:'POST',headers:{'X-Tracker-Token':radioRaw?.request_token||''},signal:AbortSignal.timeout(12000)});if(!response.ok)throw Error(TrackerI18n.serverMessage(await response.text()));$('radio-reset-result').textContent=tr("reset_requested_waiting_for_any_ongoing_radio_transmission_to");}
  catch(e){$('radio-reset-result').textContent=tr("reset_failed")+e.message;$('radio-reset').disabled=false;}
 }
 async function saveRadio(event){
  event.preventDefault();
  const rate=selectedRate(),region=selectedRegion(),block=radioHasFsb(region)?Number($('radio-block').value):0,saved=radioRaw?.lora?.settings||{};
  if((region!==(saved.region??0)||block!==(saved.channel_block??0)||(radioHasDwell(region)&&$('radio-dwell').checked)!==!!saved.dwell)&&!await confirmAction(tr("change_lorawan_network"),tr("change_the_region_channel_group_restart_the_tracker_and")+(region!==0?tr("this_region_is_experimental_and_has_not_been_tested"):''),tr("change")))return;
  $('radio-save').disabled=true;
  try{const response=await fetch('/api/lora',{method:'POST',headers:{'Content-Type':'application/json','X-Tracker-Token':radioRaw?.request_token||''},body:JSON.stringify({sf:rate.sf,region,channel_block:block,wideband:rate.wide,dwell:radioHasDwell(region)&&$('radio-dwell').checked,adr:$('radio-adr').checked,packet_profile:Number($('radio-profile').value),position_interval_s:Number($('radio-interval').value)}),signal:AbortSignal.timeout(12000)});if(!response.ok)throw new Error(TrackerI18n.serverMessage(await response.text()));const result=await response.json();$('radio-form').dataset.dirty='';$('radio-note').textContent=result.restarting?tr("saved_the_tracker_is_restarting_to_change_networks"):tr("saved_ongoing_radio_transmissions_will_finish_first");}
  catch(error){$('radio-note').textContent=tr("could_not_save")+error.message;}finally{$('radio-save').disabled=false;}
 }
 function renderLoraTiming(raw){
  radioUpdate(raw);
  const lora=raw.lora||{},timing=lora.timing||{},now=raw.diagnostics?.uptime_ms??raw.measurement_time?.uptime_ms;
  const age=s=>s<60?s+' s':s<3600?Math.floor(s/60)+' min '+s%60+' s':Math.floor(s/3600)+' h '+Math.floor(s%3600/60)+' min';
 const stamp=t=>{if(!Number.isFinite(now)||!Number.isFinite(t))return tr("no_transmission_since_restart");const seconds=Math.max(0,Math.round((now-t)/1000)),clock=raw.gnss_time;if(clock?.confirmed&&clock.recent&&Number.isFinite(clock.utc_ms)){const date=new Date(clock.utc_ms-(now-t));return new Intl.DateTimeFormat(TrackerI18n.locale(),{hour:'2-digit',minute:'2-digit',second:'2-digit',timeZone:timezone()}).format(date)+' · '+tr("relative_age",{age:age(seconds)});}return tr("relative_age_standalone",{age:age(seconds)});};
  renderLastMessage(lora.last_message);$('lora-last').textContent=stamp(lora.last_message?.completed_uptime_ms??timing.last_completed_uptime_ms);
  if(lora.credentials&&!lora.credentials.ready){$('lora-next-kind').textContent=lora.credentials.restart_pending?tr("restart_for_new_credentials"):tr("set_up_lorawan");$('lora-next').textContent=lora.credentials.restart_pending?tr("waiting_for_an_ongoing_radio_transmission_to_finish"):tr("enter_deveui_joineui_and_appkey_above");$('lora-queued').textContent='';return;}
  if(lora.settings?.restart_pending){$('lora-next-kind').textContent=tr("network_change");$('lora-next').textContent=tr("waiting_for_restart");return;}
  const phase=lora.radio_phase||'unknown';$('lora-phase').textContent=({deferred:tr("transmission_pause"),channel_check:tr("channel_check"),transaction:tr("transmission_reception"),preparing:tr("preparing_transmission"),ready:tr("ready_to_transmit"),unknown:tr("waiting_for_radio_status")})[phase];
  const nextPort=timing.next_fport,what=lora.joined?(nextPort===11?tr("satellite_list"):tr("position_status")):tr("network_join"),queuedNext=!!lora.pending_message&&phase!=='transaction';$('lora-next-kind').textContent=queuedNext?messageType(lora.pending_message):what;
  $('lora-queued').textContent=lora.pending_message?(queuedNext?tr("queued_for_transmission"):tr("current_transmission")+messageType(lora.pending_message)+' · ')+stamp(lora.pending_message.queued_uptime_ms):'';
  const due=lora.joined?timing.next_planned_uptime_ms:timing.next_join_uptime_ms;
  if(!Number.isFinite(due)||!Number.isFinite(now)){$('lora-next').textContent=tr("not_scheduled_yet");return;}
  const seconds=queuedNext?0:Math.max(0,Math.ceil((due-now)/1000));
  $('lora-next').textContent=!lora.joined&&lora.join_pending?tr("joining_the_network"):seconds?tr("scheduled_in")+age(seconds):phase==='deferred'?tr("waiting_for_the_transmission_pause_to_end"):phase==='transaction'?tr("waiting_for_the_current_transmission_to_finish"):phase==='channel_check'?tr("waiting_for_a_clear_radio_channel"):phase==='preparing'?tr("preparing_transmission"):tr("queued_for_transmission_c0f3d");
 }
 function showTab(name){if(name==='track'&&mapActive)scheduleMap();if(name==='live')renderCharts();}
 function init(){
  try{const selected=localStorage.getItem('tracker-timezone')||'auto';new Intl.DateTimeFormat(TrackerI18n.locale(),{timeZone:selected==='auto'?undefined:selected});zone=selected;}catch(error){}
  try{const name=localStorage.getItem('tracker-featured-system');if(systems.includes(name))selected=name;}catch(error){}
  try{$('accuracy-circle').checked=localStorage.getItem('tracker-accuracy-circle')==='true';}catch(error){}
  $('accuracy-circle').addEventListener('change',()=>{try{localStorage.setItem('tracker-accuracy-circle',String($('accuracy-circle').checked));}catch(error){}accuracyCircleNote();if(mapActive)drawMap();});
  $('radio-region').addEventListener('change',()=>{$('radio-dwell').checked=radioHasDwell(selectedRegion());radioOptions(selectedRegion());$('radio-form').dataset.dirty='true';radioEstimate();});
  $('radio-dwell').addEventListener('change',()=>{radioOptions(selectedRegion());$('radio-form').dataset.dirty='true';radioEstimate();});
  $('radio-form').addEventListener('submit',saveRadio);$('radio-reset').addEventListener('click',resetRadio);
  for(const id of ['radio-sf','radio-adr','radio-profile','radio-interval','radio-block'])$(id).addEventListener('change',()=>{$('radio-form').dataset.dirty='true';radioEstimate();});
  $('mqtt-form').addEventListener('submit',saveMqtt);
  timezoneOptions();$('timezone').addEventListener('change',()=>setZone($('timezone').value));$('clock-button').addEventListener('click',()=>{$('time-dialog').showModal();});
  systems.forEach(name=>{const button=document.createElement('button');button.type='button';button.dataset.system=name;button.textContent=name;button.setAttribute('aria-pressed',String(name===selected));button.addEventListener('click',()=>chooseSystem(name));$('system-selector').append(button);});
  $('load-map').addEventListener('click',loadMap);$('hide-map').addEventListener('click',()=>{mapActive=false;tiles.clear();$('position-map').hidden=true;$('hide-map').hidden=true;$('load-map').textContent=tr("load_map");$('map-note').textContent=tr("map_hidden_tracker_recording_continues_independently");});
  $('zoom-in').addEventListener('click',()=>changeZoom(1));$('zoom-out').addEventListener('click',()=>changeZoom(-1));
  const canvas=$('track-map');canvas.addEventListener('pointerdown',e=>{if(!center)return;drag={x:e.clientX,y:e.clientY,center:{...center}};canvas.setPointerCapture(e.pointerId);});
  canvas.addEventListener('pointermove',e=>{if(!drag)return;center={x:drag.center.x-(e.clientX-drag.x),y:drag.center.y-(e.clientY-drag.y)};$('follow-map').checked=false;drawMap();});
  canvas.addEventListener('pointerup',()=>{drag=null;});canvas.addEventListener('pointercancel',()=>{drag=null;});canvas.addEventListener('wheel',e=>{e.preventDefault();changeZoom(e.deltaY<0?1:-1);},{passive:false});window.addEventListener('resize',scheduleMap);
  systems.forEach((name,i)=>{const button=document.createElement('button');button.type='button';button.textContent='● '+name;button.style.color=colors[i];button.setAttribute('aria-pressed','true');button.addEventListener('click',()=>{enabled.has(name)?enabled.delete(name):enabled.add(name);button.setAttribute('aria-pressed',String(enabled.has(name)));renderCharts();});$('chart-legend').append(button);});
  try{const saved=localStorage.getItem('tracker-chart-window');if(['900000','3600000','14400000','86400000','172800000'].includes(saved))$('chart-window').value=saved;}catch(error){}
  $('chart-window').addEventListener('change',()=>{try{localStorage.setItem('tracker-chart-window',$('chart-window').value);}catch(error){}hoverTime=null;renderCharts();TrackerRecords.renderNotes();});
  for(const id of ['visible-chart','used-chart']){const svg=$(id);svg.setAttribute('tabindex','0');svg.addEventListener('pointermove',cursor);svg.addEventListener('pointerdown',event=>{if(event.pointerType==='touch')svg.setPointerCapture(event.pointerId);cursor(event);});svg.addEventListener('pointerleave',event=>{if(event.pointerType!=='touch'){hoverTime=null;renderCharts();}});svg.addEventListener('keydown',event=>{if(event.key==='Escape'){hoverTime=null;renderCharts();return;}if(!['ArrowLeft','ArrowRight'].includes(event.key))return;event.preventDefault();const visible=history.filter(p=>p.t>=(TrackerRecords.timelineNow()||history.at(-1)?.t||0)-Number($('chart-window').value));if(!visible.length)return;let index=visible.findIndex(p=>p.t>=(hoverTime??visible.at(-1).t));index=Math.max(0,Math.min(visible.length-1,index+(event.key==='ArrowRight'?1:-1)));hoverTime=visible[index].t;const rect=svg.getBoundingClientRect();$('chart-tooltip').style.left=Math.max(8,Math.min(window.innerWidth-330,rect.right-330))+'px';$('chart-tooltip').style.top=Math.max(8,Math.min(window.innerHeight-230,rect.top+30))+'px';renderCharts();});}
  $('export-gpx').addEventListener('click',async()=>{await syncTrack();if(TrackerRecords.trackError()){$('track-note').textContent=tr("export_cancelled")+TrackerRecords.trackError();return;}if(!track.length){$('track-note').textContent=tr("no_track_points_to_export_yet");return;}download('tracker-track.gpx','application/gpx+xml',gpx());});
  $('export-csv').addEventListener('click',openCsv);$('csv-range').addEventListener('change',()=>{csvDescription();prepareCsv();});$('csv-filtered').addEventListener('change',csvDescription);$('csv-download').addEventListener('click',exportCsv);$('csv-close').addEventListener('click',()=>$('csv-dialog').close());
  setInterval(renderClock,1000);historyNotes();renderCharts();
 }
 document.addEventListener('DOMContentLoaded',init);
 return {update,refresh,showTab,renderLoraTiming,confirm:confirmAction,selectedSystem:()=>selected,languageChanged};
})();
