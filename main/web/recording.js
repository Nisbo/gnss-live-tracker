/* Independent recording destinations. Device history is never stored as browser history. */
'use strict';
const TrackerRecords=(()=>{
 const tr=(key,params)=>TrackerI18n.t('recording.'+key,params);
 const $=id=>document.getElementById(id),systems=['GPS','GLONASS','Galileo','BeiDou','QZSS','SBAS','NavIC'];
 const MAX_BROWSER_TRACK=6000,MAX_HISTORY=1440;
 let raw=null,storageMetadata={},key='',prefs={},browserHistory=[],browserTrack=[],storageFailed=false,lastHistory=0,lastTrack=0,breakTrack=true,lastUptime=null,lastSeen=0;
 const caches={track:{points:[],epoch:null,cursor:0,promise:null,error:'',at:0,loaded:false},history:{points:[],epoch:null,cursor:0,promise:null,error:'',at:0,loaded:false}};
 let changing=false,importing=false,importingHistory=false,utcAnchor=null,utcReceived=0;
 function notify(){if(typeof TrackerDashboard!=='undefined'&&TrackerDashboard.refresh)TrackerDashboard.refresh();}
 function save(){try{localStorage.setItem(key,JSON.stringify({history:browserHistory,track:browserTrack}));localStorage.setItem(key+':preferences',JSON.stringify(prefs));storageFailed=false;}catch(error){storageFailed=true;}}
 const validPosition=p=>Number.isFinite(p.t)&&Number.isFinite(p.lat)&&Math.abs(p.lat)<=90&&Number.isFinite(p.lon)&&Math.abs(p.lon)<=180;
 const validHistory=p=>Array.isArray(p.v)&&Array.isArray(p.u)&&p.v.length===7&&p.u.length===7&&[...p.v,...p.u].every(v=>v===null||Number.isInteger(v)&&v>=0&&v<=96);
 function initStorage(value){
  const next='tracker-observations-v1:'+(value.wifi?.mac||'local');if(next===key)return;
  key=next;storageMetadata={};browserHistory=[];browserTrack=[];prefs={};lastHistory=lastTrack=0;lastUptime=null;breakTrack=true;
  for(const c of Object.values(caches)){c.generation=(c.generation||0)+1;c.hydrated=false;c.complete=false;c.promise=null;c.bySeq=new Map();c.points=[];c.epoch=null;c.cursor=0;c.error='';c.at=0;c.loaded=false;}
  try{const text=localStorage.getItem(key);if(text&&text.length<2500000){const old=JSON.parse(text);browserHistory=(Array.isArray(old.history)?old.history:[]).filter(p=>(p.t===null||Number.isFinite(p.t))&&validHistory(p)).map(p=>({...p,time_source:p.time_source||'browser_legacy'})).slice(-MAX_HISTORY);browserTrack=(Array.isArray(old.track)?old.track:[]).filter(validPosition).slice(-MAX_BROWSER_TRACK);}
   prefs=JSON.parse(localStorage.getItem(key+':preferences')||'{}');if(!prefs||typeof prefs!=='object')prefs={};
  }catch(error){storageFailed=true;}
 }
 function meta(type){return raw?.[type==='history'?'sat_history':'track']||{};}
 function mode(type){const m=meta(type);return m.enabled?'tracker':prefs[type+'Mode']==='browser'?'browser':'none';}
 function displaySource(type){return mode(type)==='browser'?'browser':mode(type)==='tracker'?'tracker':prefs[type+'Source']==='browser'?'browser':'tracker';}
 function period(type){return mode(type)==='browser'?Number(prefs[type+'Interval'])||10:meta(type).interval_s||10;}
 async function request(path,data){
  const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),15000);
  try{const response=await fetch(path,{method:data===undefined?'GET':'POST',signal:controller.signal,cache:'no-store',headers:{'Content-Type':'application/json','X-Tracker-Token':raw?.request_token||''},body:data===undefined?undefined:JSON.stringify(data)});if(!response.ok)throw new Error(tr("request_rejected")+response.status+')');return await response.json();}finally{clearTimeout(timer);}
 }
 function syncLegacy(type,force=false){
  const c=caches[type],m=meta(type);if(c.promise)return c.promise;if(!m.ready)return Promise.resolve();
  if(force||c.epoch!==m.epoch||c.retention!==m.retention_revision){c.retention=m.retention_revision;c.points=[];c.loaded=false;c.epoch=m.epoch;c.cursor=Math.max(0,m.oldest_seq-1);}
  c.points=c.points.filter(p=>p.seq>=m.oldest_seq);if(c.cursor>=m.next_seq-1){c.loaded=true;c.at=performance.now();return Promise.resolve();}
  const endpoint=type==='history'?'/api/history':'/api/track',limit=type==='history'?128:256;
  c.promise=(async()=>{let target=m.next_seq-1;
   for(let batch=0;c.cursor<target&&batch<256;batch++){
    const page=await request(endpoint+'?after='+c.cursor+'&limit='+limit);
    if(!Array.isArray(page.points)||!Number.isFinite(page.next_seq)||!Number.isFinite(page.oldest_seq))throw new Error(tr("invalid_stored_data"));
    if(page.epoch!==c.epoch){c.points=[];c.loaded=false;c.epoch=page.epoch;c.cursor=Math.max(0,page.oldest_seq-1);target=page.next_seq-1;continue;}
    const points=page.points.filter(p=>Number.isFinite(p.seq)&&p.seq>c.cursor&&(type==='history'?validHistory(p)&&(p.t===null||Number.isFinite(p.t)):validPosition(p)));
    for(const p of points){if(p.seq>c.cursor){c.points.push(p);c.cursor=p.seq;}}
    c.points=c.points.filter(p=>p.seq>=page.oldest_seq);renderNotes();if(!points.length){if(c.cursor<page.next_seq-1)throw new Error(tr("incomplete_stored_data"));break;}
   }c.error='';c.loaded=c.cursor>=target;
  })().catch(error=>{c.error=error.name==='AbortError'?tr("timed_out_will_retry"):error.message;}).finally(()=>{c.promise=null;c.at=performance.now();renderNotes();notify();});return c.promise;
 }
 // Recent-first paging keeps a forward sequence frontier for live additions and imports.
 // The descending cursor independently fills the older snapshot without holes or duplicates.
 const UINT32_MAX=4294967295;
 const diskCache=()=>typeof TrackerHistoryCache==='undefined'?null:TrackerHistoryCache;
 function historySince(){const value=Number($('chart-window').value),span=[900000,3600000,14400000,86400000,172800000].includes(value)?value:14400000;const end=timelineNow()||meta('history').newest_utc_ms||0;return Math.max(0,Math.floor((end-span)/1000));}
 function recentReady(since=historySince()){const c=caches.history;return !!c.complete||Number.isFinite(c.coveredSince)&&c.coveredSince<=since;}
 function resetRecent(c,m){
  c.points=[];c.bySeq=new Map();c.epoch=m.epoch;c.retention=m.retention_revision;c.cursor=Math.max(0,m.next_seq-1);c.beforeS=UINT32_MAX;c.beforeSeq=UINT32_MAX;c.since=historySince();c.phase=c.since?'window':'all';c.coveredSince=UINT32_MAX;c.complete=false;c.loaded=false;c.error='';c.saveAt=0;c.metaNext=m.next_seq;c.oldest=m.oldest_seq;c.count=m.count;
 }
 function validCachedPoint(p){return p&&Number.isInteger(p.seq)&&p.seq>0&&p.seq<UINT32_MAX&&validHistory(p)&&(p.t===null||Number.isInteger(p.t)&&p.t%1000===0&&p.t>=315532800000&&p.t<=4102444800000)&&Number.isInteger(p.boot)&&p.boot>0&&p.boot<=UINT32_MAX&&Number.isFinite(p.up)&&p.up>=0&&p.up<=UINT32_MAX&&(p.total===null||Number.isInteger(p.total)&&p.total>=0&&p.total<=96)&&typeof p.fix==='boolean';}
 // A ring-buffer rollover retires only the oldest records. It must not clear
 // the visible window or restart the descending background cursor.
 function reconcileRecent(c,m){
  if(c.epoch!==m.epoch){resetRecent(c,m);return true;}
  if(m.next_seq<c.metaNext&&m.retention_revision<=c.retention||m.next_seq===c.metaNext&&m.retention_revision<c.retention){
   Object.assign(m,{next_seq:c.metaNext,oldest_seq:c.oldest,count:c.count,retention_revision:c.retention});return;
  }
  if(c.retention!==m.retention_revision){
   const retired=m.retention_revision-c.retention;
   if(retired<0||m.next_seq<c.metaNext||m.oldest_seq-c.oldest!==retired){resetRecent(c,m);return true;}
   c.points=c.points.filter(p=>p.seq>=m.oldest_seq);c.bySeq=new Map(c.points.map(p=>[p.seq,p]));c.cursor=Math.max(c.cursor,m.oldest_seq-1);c.retention=m.retention_revision;
  }
  c.metaNext=m.next_seq;c.oldest=m.oldest_seq;c.count=m.count;
 }
 function cacheCompatible(saved,m){
  const oldest=saved?.oldest??saved?.retention;
  return saved&&saved.version===1&&Number.isInteger(saved.snapshotCount)&&saved.snapshotCount>=0&&saved.snapshotCount<=(m.capacity||16000)&&Number.isInteger(saved.snapshotNext)&&saved.cursor<saved.snapshotNext&&saved.snapshotNext-saved.snapshotCount===saved.retention&&saved.snapshotNext<=m.next_seq&&(!saved.complete||saved.snapshotCount===saved.points?.length&&saved.cursor===saved.snapshotNext-1)&&saved.epoch===m.epoch&&m.retention_revision>=saved.retention&&m.oldest_seq-oldest===m.retention_revision-saved.retention&&Number.isInteger(saved.cursor)&&saved.cursor>=oldest-1&&['window','all'].includes(saved.phase)&&['beforeS','beforeSeq','since','coveredSince'].every(k=>Number.isInteger(saved[k])&&saved[k]>=0&&saved[k]<=UINT32_MAX)&&typeof saved.complete==='boolean'&&Array.isArray(saved.points)&&saved.points.length<=Math.min(m.capacity||16000,16000)&&saved.points.every(validCachedPoint)&&saved.points.every(p=>p.seq>=oldest&&p.seq<saved.snapshotNext)&&new Set(saved.points.map(p=>p.seq)).size===saved.points.length;
 }
 async function hydrateHistory(c,device,generation){
  if(c.hydrated)return;c.hydrated=true;const saved=await diskCache()?.get(device+':tracker-history');
  if(device!==key||generation!==(c.generation||0))return;
  const m=meta('history');resetRecent(c,m);
  if(cacheCompatible(saved,m)){
   for(const field of ['cursor','beforeS','beforeSeq','since','phase','coveredSince','complete'])c[field]=saved[field];
   c.points=saved.points.filter(p=>p.seq>=m.oldest_seq);c.bySeq=new Map(c.points.map(p=>[p.seq,p]));c.cursor=Math.max(c.cursor,m.oldest_seq-1);c.loaded=c.complete;renderNotes();notify();
  }else if(saved)diskCache()?.remove(device+':tracker-history');
 }
 function persistHistory(c,force=false){
  if(!meta('history').ready||!c.hydrated||!diskCache()||(!force&&performance.now()-(c.saveAt||0)<10000))return;
  c.saveAt=performance.now();const saved={version:1,epoch:c.epoch,retention:c.retention,cursor:c.cursor,beforeS:c.beforeS,beforeSeq:c.beforeSeq,since:c.since,phase:c.phase,coveredSince:c.coveredSince,oldest:c.oldest,snapshotCount:c.count,snapshotNext:c.metaNext,complete:!!c.complete&&c.cursor===meta('history').next_seq-1&&c.points.length===meta('history').count,points:c.points};
  diskCache().put(key+':tracker-history',saved);
 }
 function settleVisible(c,finish=false){if(!c.waiters)return;const pending=[];for(const waiter of c.waiters){if(finish||recentReady(waiter.since))waiter.resolve();else pending.push(waiter);}c.waiters=pending;}
 function mergeHistoryPage(c,points){for(const p of points)c.bySeq.set(p.seq,p);c.points=[...c.bySeq.values()];renderNotes();settleVisible(c);notify();}
 function pageMetadata(page){
  if(!Array.isArray(page.points)||!Number.isInteger(page.next_seq)||!Number.isInteger(page.oldest_seq)||!Number.isInteger(page.total)||!Number.isInteger(page.epoch)||!Number.isInteger(page.retention_revision))throw new Error(tr('invalid_stored_data'));
  return {ready:true,epoch:page.epoch,retention_revision:page.retention_revision,next_seq:page.next_seq,oldest_seq:page.oldest_seq,count:page.total};
 }
 function syncRecent(force=false){
  const c=caches.history;if(c.promise){if(force)c.restart=true;return c.promise;}if(!meta('history').ready)return Promise.resolve();
  const device=key,generation=c.generation||0;
  const work=(async()=>{
   await hydrateHistory(c,device,generation);
   if(device!==key||generation!==(c.generation||0))return;
   if(force)resetRecent(c,meta('history'));else reconcileRecent(c,meta('history'));
   while(device===key&&generation===(c.generation||0)){
    const m=meta('history');if(!m.ready)break;if(c.restart){c.restart=false;resetRecent(c,m);}else reconcileRecent(c,m);
    if(m.next_seq-1-c.cursor>128){const retained=c.points;resetRecent(c,m);c.points=retained;c.bySeq=new Map(retained.map(p=>[p.seq,p]));}
    const forward=c.cursor<m.next_seq-1;
    if(!forward&&c.complete){if(c.points.length!==m.count)throw new Error(tr('incomplete_stored_data'));break;}
    const path=forward?'/api/history?after='+c.cursor+'&limit=128':'/api/history?order=recent&before_s='+c.beforeS+'&before_seq='+c.beforeSeq+'&since_s='+(c.phase==='window'?c.since:0)+'&limit=128';
    const wasReady=recentReady();
    const page=await request(path);
    if(device!==key||generation!==(c.generation||0))return;
    const observed=pageMetadata(page);
    if(observed.epoch!==c.epoch){Object.assign(m,observed);resetRecent(c,m);continue;}
    const pageNext=observed.next_seq;if(reconcileRecent(c,observed)){Object.assign(m,observed);continue;}
    const points=page.points.filter(p=>p.seq>=observed.oldest_seq);if(!points.every(validCachedPoint)||points.some(p=>p.seq>=observed.next_seq))throw new Error(tr('invalid_stored_data'));
    // Keep status and page snapshots on one monotonic retention watermark.
    Object.assign(m,observed);
    if(forward){
     let prior=c.cursor;for(const p of points){if(p.seq<=prior)throw new Error(tr('invalid_stored_data'));prior=p.seq;}
     c.cursor=page.points.length<128?Math.max(c.cursor,pageNext-1):prior;
    }else{
     if(page.recent!==true||typeof page.more!=='boolean'||(!page.points.length&&page.more))throw new Error(tr('invalid_stored_data'));
     let priorS=c.beforeS,priorSeq=c.beforeSeq;
     for(const p of points){const seconds=p.t===null?0:p.t/1000;if(seconds>priorS||seconds===priorS&&p.seq>=priorSeq||c.phase==='window'&&seconds<c.since)throw new Error(tr('invalid_stored_data'));priorS=seconds;priorSeq=p.seq;}
     if(points.length){c.beforeS=priorS;c.beforeSeq=priorSeq;c.coveredSince=Math.min(c.coveredSince,priorS);}
     if(!page.more||page.points.length&&!points.length){if(c.phase==='window'){c.coveredSince=Math.min(c.coveredSince,c.since);if(!points.length){c.beforeS=c.since;c.beforeSeq=0;}c.phase='all';}else{c.complete=true;c.loaded=true;c.coveredSince=0;}}
    }
    c.error='';mergeHistoryPage(c,points);persistHistory(c,c.complete||!wasReady&&recentReady());
   }
  })().catch(error=>{if(device===key&&generation===(c.generation||0))c.error=error.name==='AbortError'?tr('timed_out_will_retry'):error.message;}).finally(()=>{
   if(c.promise!==work)return;c.promise=null;c.at=performance.now();settleVisible(c,true);persistHistory(c,true);renderNotes();notify();
  });c.promise=work;return work;
 }
 function sync(type,force=false){return type==='history'&&meta('history').recent_paging?syncRecent(force):syncLegacy(type,force);}
 function syncHistory(force=false,visibleOnly=false){
  const complete=sync('history',force);if(!visibleOnly||!meta('history').recent_paging)return complete;if(recentReady())return Promise.resolve();
  return new Promise(resolve=>{const c=caches.history;(c.waiters||=[]).push({since:historySince(),resolve});complete.finally(()=>settleVisible(c,true));});
 }

 function timelineNow(){return utcAnchor===null?0:utcAnchor+performance.now()-utcReceived;}
 function datedHistory(points,includeUnknown=false){
  const anchors=new Map();
  for(const p of points)if(p.time_source!=='browser_legacy'&&p.time_confirmed!==false&&Number.isFinite(p.t)&&Number.isFinite(p.up)&&(!anchors.has(p.boot)||p.up>=anchors.get(p.boot).up))anchors.set(p.boot,{up:p.up,offset:p.t-p.up*1000});
  return points.map(p=>p.t!==null?p:anchors.has(p.boot)?{...p,t:Math.round(anchors.get(p.boot).offset+p.up*1000),time_estimated:true,time_confirmed:true}:p).filter(p=>includeUnknown||Number.isFinite(p.t)).slice().sort((a,b)=>(a.t??Infinity)-(b.t??Infinity)||(a.seq||0)-(b.seq||0));
 }
 function historyRows(includeUnknown=false){return datedHistory(displaySource('history')==='browser'?browserHistory:caches.history.points,includeUnknown);}
 function chartHistory(){return historyRows();}
 function historyLoad(){const c=caches.history,m=meta('history');if(!raw)return {loading:true,message:tr("waiting_for_tracker_connection")};if(displaySource('history')==='browser')return {loading:false,message:''};if(c.error)return {loading:!!c.promise,error:true,message:tr("could_not_load_the_complete_chart_history")+c.error+tr("data_already_loaded_remains_visible")};if(!m.ready)return {loading:true,message:tr("waiting_for_the_tracker_s_chart_storage")};if(m.recent_paging&&!c.complete){const ready=recentReady();return {loading:!ready,background:ready,message:tr(ready?'history_older_loading':'history_recent_loading',{loaded:c.points.length.toLocaleString(TrackerI18n.locale()),total:(m.count||0).toLocaleString(TrackerI18n.locale())})};}if(c.promise&&!c.loaded)return {loading:true,message:tr("history_load_progress",{loaded:c.points.length.toLocaleString(TrackerI18n.locale()),total:(m.count||0).toLocaleString(TrackerI18n.locale())})};return {loading:false,message:''};}
 function historyForImport(){return datedHistory(browserHistory).filter(p=>p.time_source==='gnss'&&p.time_confirmed===true&&validHistory(p)&&Number.isInteger(p.boot)&&p.boot>0&&p.boot<=4294967295&&Number.isFinite(p.up)&&p.up>=0&&p.up<=4294967295&&typeof p.fix==='boolean'&&(p.total===null||Number.isInteger(p.total)&&p.total>=0&&p.total<=96));}
 function track(){
  const source=displaySource('track')==='browser'?browserTrack:caches.track.points;
  return source.slice().sort((a,b)=>a.t-b.t||(a.seq||0)-(b.seq||0)).map((p,i,all)=>({...p,break:!!p.break||i===0}));
 }
 function update(value,p,satellites,used){
  initStorage(value);raw=value;
  // Status may briefly omit storage fields while a history page owns its mutex.
  // Keep the last valid storage snapshot instead of treating 'busy' as a reset.
  for(const name of ['track','sat_history']){if(value[name]?.ready)storageMetadata[name]={...value[name]};else if(value[name]?.state==='busy'&&storageMetadata[name])raw={...raw,[name]:{...storageMetadata[name],state:'busy'}};}
  const now=performance.now(),clock=value.gnss_time||{},measurement=value.measurement_time||{};
  if(clock.recent&&Number.isFinite(clock.utc_ms)){utcAnchor=clock.utc_ms;utcReceived=now;}
  if(lastUptime!==null&&(p.uptime_s<lastUptime||now-lastSeen>25000))breakTrack=true;lastSeen=now;lastUptime=p.uptime_s;
  if(!p.gnss_fix)breakTrack=true;
  if(mode('track')==='browser'&&p.gnss_fix&&(!lastTrack||now-lastTrack>=period('track')*1000)){
   if(Number.isFinite(measurement.position_utc_ms)){browserTrack.push({t:measurement.position_utc_ms,lat:p.latitude,lon:p.longitude,alt:p.altitude,break:breakTrack,time_source:'gnss'});browserTrack=browserTrack.slice(-MAX_BROWSER_TRACK);lastTrack=now;breakTrack=false;save();}
  }
  if(mode('history')==='browser'&&(!lastHistory||now-lastHistory>=period('history')*1000)){
   browserHistory.push({t:clock.recent&&Number.isFinite(clock.utc_ms)?clock.utc_ms:null,up:measurement.uptime_ms/1000,boot:value.sat_history?.boot,v:systems.map(n=>p.satellites_visible_by_system[n]??null),u:used.map(v=>v??null),total:p.satellites_used??null,fix:!!p.gnss_fix,interval_s:period('history'),time_source:'gnss',time_confirmed:!!clock.confirmed});browserHistory=browserHistory.slice(-MAX_HISTORY);lastHistory=now;save();
  }
  for(const type of ['history','track'])if((type!=='track'||!meta('history').recent_paging||recentReady()||$('tab-track').getAttribute?.('aria-selected')==='true')&&meta(type).ready&&(now-caches[type].at>=5000||caches[type].epoch!==meta(type).epoch||caches[type].retention!==meta(type).retention_revision))sync(type);
  renderNotes();
 }
 async function setMode(type,next){
  if(changing||!['none','browser','tracker'].includes(next))return;const previous=mode(type);
  if(previous==='tracker'&&next!=='tracker'&&!await TrackerDashboard.confirm(tr("turn_off_recording_on_the_tracker"),tr("this_change_applies_to_all_connected_browsers_stored_data"),tr("turn_off"))){renderNotes();return;}
  changing=true;renderNotes();
  try{await request(type==='track'?'/api/track':'/api/history',{action:'configure',enabled:next==='tracker'});meta(type).enabled=next==='tracker';prefs[type+'Mode']=next;if(next!=='none')prefs[type+'Source']=next;prefs[type+'Interval']||=meta(type).interval_s||10;lastTrack=lastHistory=0;breakTrack=true;save();caches[type].error='';}
  catch(error){caches[type].error=error.message;}finally{changing=false;renderNotes();notify();}
 }
 async function setPeriod(type,value){
  const seconds=Number(value);if(!Number.isInteger(seconds)||seconds<5||seconds>3600)return;
  try{if(mode(type)==='tracker'){await request(type==='track'?'/api/track':'/api/history',{action:'configure',interval_s:seconds});meta(type).interval_s=seconds;}else{prefs[type+'Interval']=seconds;save();}lastTrack=lastHistory=0;caches[type].error='';}catch(error){caches[type].error=error.message;}renderNotes();notify();
 }
 function renderNotes(){
  if(!raw)return;
  for(const type of ['track','history']){const name=type==='track'?'position':'history',current=mode(type);for(const radio of document.querySelectorAll('input[name="'+name+'-mode"]')){radio.checked=radio.value===current;radio.disabled=changing;}
   $(type==='track'?'track-interval':'history-interval').value=String(period(type));$(type==='track'?'track-interval':'history-interval').disabled=current==='none';
   $(type==='track'?'position-mode-note':'history-mode-note').textContent=current==='tracker'?tr("recording_on_the_tracker_is_active_this_setting_applies"):current==='browser'?tr("only_this_browser_records_while_the_page_is_open"):tr("no_new_data_is_being_recorded_stored_data_is");
  }
  const log=meta('track'),c=caches.track,labels={recording:tr("recording_active"),waiting_fix:tr("waiting_for_a_valid_gnss_fix"),waiting_time:tr("waiting_for_confirmed_gnss_time"),disabled:tr("recording_off"),storage_error:tr("storage_error")};
 $('device-track-state').textContent=(labels[log.state]||tr("reading_tracker_storage"))+(log.ready?tr("track_state_counts",{count:log.count,capacity:log.capacity,interval:log.interval_s}):'');
 $('track-note').textContent=(displaySource('track')==='browser'?tr("browser_track_count",{count:browserTrack.length}):tr("tracker_track_count",{count:log.count||0}))+(c.error?' · '+c.error:'')+tr("track_display_source",{source:displaySource('track')==='browser'?tr('this_browser'):'Tracker'});
  const legacy=browserTrack.filter(p=>p.time_source!=='gnss').length;
 $('browser-track-note').textContent=tr("stored_browser_track_count",{count:browserTrack.length})+(legacy?tr("legacy_browser_track_count",{count:legacy}):'.');
  $('import-browser-track').disabled=importing||!raw.track_merge_ready||!browserTrack.some(p=>!p.imported&&p.time_source==='gnss');
  const m=meta('history'),h=caches.history,source=displaySource('history'),count=source==='browser'?browserHistory.length:h.points.length;
  const dated=historyRows(),unassigned=Math.max(0,count-dated.length),estimated=dated.filter(p=>p.time_estimated).length;

 $('history-note').textContent=(source==='browser'?tr("browser_sample_count",{count}):tr("tracker_sample_count",{count:m.count||0,capacity:m.capacity||0}))+'. '+(source==='browser'?tr("recording_only_while_the_web_page_is_active_browser"):tr("recording_continues_without_a_position_fix_times_come_from"))+(storageFailed?tr("browser_storage_unavailable"):'');
 if(estimated)$('history-note').textContent+=tr("estimated_sample_time",{count:estimated});
 if(unassigned)$('history-note').textContent+=tr("unassigned_sample_time",{count:unassigned});
  const eligible=historyForImport();$('import-browser-history').disabled=importingHistory||!m.merge_supported||!eligible.length;
  const blocked=browserHistory.length-eligible.length;
 $('browser-history-note').textContent=!browserHistory.length?tr("no_chart_samples_stored_in_this_browser"):tr("stored_browser_sample_count",{count:browserHistory.length})+(eligible.length?tr("eligible_browser_sample_count",{count:eligible.length}):tr("these_samples_lack_reliable_gnss_time_and_cannot_be"))+(eligible.length&&blocked?tr("blocked_browser_sample_count",{count:blocked}):'')+tr("the_browser_copy_is_preserved");
  const load=historyLoad();$('history-load-state').hidden=!load.message;$('history-load-state').textContent=load.message;$('history-load-state').className=load.error?'notice warning':'notice history-progress';
  for(const id of ['visible-chart','used-chart'])$(id).setAttribute('aria-busy',String(load.loading));
  if(mode('track')==='browser'&&!Number.isFinite(raw.measurement_time?.position_utc_ms))$('position-mode-note').textContent+=tr("waiting_for_a_valid_fix_with_confirmed_gnss_time");

 }
 async function importBrowser(){
  const points=browserTrack.filter(p=>!p.imported&&p.time_source==='gnss').slice().sort((a,b)=>a.t-b.t).filter((p,i,a)=>i===0||p.t!==a[i-1].t||p.lat!==a[i-1].lat||p.lon!==a[i-1].lon);if(!points.length)return;
  if(!await TrackerDashboard.confirm(tr("merge_browser_track_into_the_tracker"),tr("confirm_track_merge",{count:points.length}),tr("merge")))return;
  importing=true;renderNotes();
  let hash=2166136261;for(const p of points)for(const ch of JSON.stringify([p.t,p.lat,p.lon]))hash=Math.imul(hash^ch.charCodeAt(0),16777619);const session='browser-'+(hash>>>0).toString(16)+'-'+points.length;
  try{for(let offset=0;offset<points.length;){const response=await request('/api/import',{session,offset,points:points.slice(offset,offset+32)});if(!Number.isInteger(response.next_offset)||response.next_offset<=offset)throw new Error(tr("invalid_import_progress"));offset=response.next_offset;$('import-note').textContent=offset+' / '+points.length+tr("points_merged");}
   for(const p of points)p.imported=true;const imported=new Set(points.map(p=>p.t));for(const p of browserTrack)if(imported.has(p.t))p.imported=true;save();$('import-note').textContent=points.length+tr("points_merged_browser_copy_preserved");caches.track.at=0;await sync('track',true);notify();
  }catch(error){$('import-note').textContent=tr("import_interrupted")+error.message+tr("merge_again_to_resume_the_same_transfer");}finally{importing=false;renderNotes();}
 }
 async function importHistory(){
  const points=historyForImport();if(importingHistory||!points.length)return;
  if(!await TrackerDashboard.confirm(tr("merge_browser_satellite_statistics_into_the_tracker"),tr("confirm_history_merge",{count:points.length}),tr("merge")))return;
  importingHistory=true;renderNotes();let added=0,duplicates=0,tooOld=0;
  try{for(let offset=0;offset<points.length;){const batch=points.slice(offset,offset+16),reply=await request('/api/history/import',{offset,points:batch});if(reply.next_offset!==offset+batch.length)throw new Error(tr("invalid_merge_progress"));offset=reply.next_offset;added+=reply.added||0;duplicates+=reply.duplicates||0;tooOld+=reply.too_old||0;$('history-import-note').textContent=offset+' / '+points.length+tr("samples_checked");}
   $('history-import-note').textContent=added+tr("newly_merged")+duplicates+tr("already_present")+(tooOld?' · '+tooOld+tr("older_than_the_full_ring_buffer_left_unchanged_in"):'')+tr("browser_copy_preserved");await sync('history',true);notify();
  }catch(error){$('history-import-note').textContent=tr("merge_interrupted")+error.message+tr("merge_again_to_skip_identical_samples_already_stored");}
  finally{importingHistory=false;renderNotes();}
 }
 async function clearDevice(type){const endpoint=type==='history'?'/api/history':'/api/track',label=type==='history'?tr("tracker_chart_history"):tr("position_track_on_the_tracker");if(!await TrackerDashboard.confirm(tr("delete_named_data",{name:label}),tr("this_stored_data_will_be_deleted_from_the_tracker")))return;try{await request(endpoint,{action:'clear'});if(type==='history'){caches[type].generation=(caches[type].generation||0)+1;caches[type].promise=null;caches[type].hydrated=false;caches[type].complete=false;diskCache()?.remove(key+':tracker-history');}caches[type].epoch=null;caches[type].at=0;caches[type].points=[];caches[type].error='';}catch(error){caches[type].error=error.message;}renderNotes();notify();}
 async function readDiagnostics(probe=false){try{if(probe){const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),10000);try{const r=await fetch('/api/gnss',{method:'POST',headers:{'X-Tracker-Token':raw?.request_token||''},signal:controller.signal});if(!r.ok)throw new Error(tr("query_rejected")+r.status+')');}finally{clearTimeout(timer);}}
   const result=await request('/api/gnss');$('gnss-diagnostics').textContent=(probe?tr("query_sent_read_again_in_a_few_seconds"):'')+result.lines.map(l=>'['+(l.received_uptime_ms/1000).toFixed(1)+' s] '+l.raw).join('\n');}catch(error){$('gnss-diagnostics').textContent=error.message;}}
 function languageChanged(){renderNotes();}
 function init(){
  for(const radio of document.querySelectorAll('input[name="position-mode"]'))radio.addEventListener('change',()=>{if(radio.checked)setMode('track',radio.value);});for(const radio of document.querySelectorAll('input[name="history-mode"]'))radio.addEventListener('change',()=>{if(radio.checked)setMode('history',radio.value);});
  $('track-interval').addEventListener('change',()=>setPeriod('track',$('track-interval').value));$('history-interval').addEventListener('change',()=>setPeriod('history',$('history-interval').value));
  $('reload-track').addEventListener('click',()=>sync('track',true));
  $('clear-track').addEventListener('click',()=>clearDevice('track'));$('clear-device-history').addEventListener('click',()=>clearDevice('history'));
  $('clear-browser-track').addEventListener('click',async()=>{if(await TrackerDashboard.confirm(tr("delete_browser_track"),tr("delete_only_this_browser_s_position_points_data_stored"))){browserTrack=[];breakTrack=true;save();renderNotes();notify();}});
  $('clear-history').addEventListener('click',async()=>{if(await TrackerDashboard.confirm(tr("delete_browser_chart_history"),tr("delete_only_this_browser_s_satellite_statistics_data_stored"))){browserHistory=[];save();renderNotes();notify();}});
  $('import-browser-history').addEventListener('click',importHistory);$('import-browser-track').addEventListener('click',importBrowser);$('load-gnss-diagnostics').addEventListener('click',()=>readDiagnostics());$('probe-gnss').addEventListener('click',()=>readDiagnostics(true));
 }
 document.addEventListener('DOMContentLoaded',init);
 return {update,track,historyRows,historyLoad,historySource:()=>displaySource('history'),historyError:()=>caches.history.error,trackError:()=>caches.track.error,history:chartHistory,period,mode,renderNotes,timelineNow,syncTracks:force=>sync('track',force),syncHistory,languageChanged};
})();
