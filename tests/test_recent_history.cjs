/* Recent-first, complete exports, disposable persistent cache, retention and race handling. */
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const web=__dirname+'/../main/web/',NOW=1793000000000,MAX=4294967295,names=['GPS','GLONASS','Galileo','BeiDou','QZSS','SBAS','NavIC'];
let points=Array.from({length:14000},(_,i)=>({seq:i+1,t:i?NOW-(13999-i)*10000:null,up:i*10,boot:42,v:[10,7,9,14,0,5,null],u:[6,4,5,8,0,1,null],total:24,fix:true,time_confirmed:true,break:i===0}));
let next=14001,epoch=1;const disk=new Map();
const clone=v=>JSON.parse(JSON.stringify(v));
function metadata(){return {ready:true,enabled:true,interval_s:10,capacity:14336,count:points.length,oldest_seq:points.length?Math.min(...points.map(p=>p.seq)):next,next_seq:next,epoch,retention_revision:next-points.length,recent_paging:true};}
function boot({pauseOlder=false,device='device-one',cache=true,changeDuringRead=false}={}){
 const elements=new Map(),calls=[],notices=[],saved=[];let unpause,paused=false,record,changed=false;
 const element=id=>({id,value:id==='chart-window'?'14400000':'',children:[],checked:false,hidden:false,dataset:{},style:{},textContent:'',setAttribute(k,v){this[k]=v;},getAttribute(k){return this[k]??null;},addEventListener(){}});
 const doc={getElementById(id){if(!elements.has(id))elements.set(id,element(id));return elements.get(id);},querySelectorAll:()=>[],addEventListener(){}};
 const raw={request_token:'test',wifi:{mac:device},track:{ready:false},sat_history:metadata(),measurement_time:{position_utc_ms:NOW,uptime_ms:140000000},gnss_time:{recent:true,confirmed:true,utc_ms:NOW}};
 function reply(path){
  const u=new URL(path,'http://test'),q=u.searchParams,limit=Number(q.get('limit')||128),recent=q.get('order')==='recent';let rows;
  if(changeDuringRead&&!changed&&calls.length===2){points=points.filter(p=>p.seq>100);changed=true;}
  if(recent){const before=Number(q.get('before_s')??MAX),seq=Number(q.get('before_seq')??MAX),since=Number(q.get('since_s')||0);rows=points.filter(p=>{const t=(p.t||0)/1000;return t>=since&&(t<before||t===before&&p.seq<seq);}).sort((a,b)=>(b.t||0)-(a.t||0)||b.seq-a.seq);}
  else rows=points.filter(p=>p.seq>Number(q.get('after')||0)).sort((a,b)=>a.seq-b.seq);
  const m=metadata();return {ok:true,json:async()=>({...m,total:m.count,recent,more:recent&&rows.length>limit,points:clone(rows.slice(0,limit))})};
 }
 const local=new Map();const ctx={document:doc,performance:{now:()=>100000},Date,Intl,Math,Map,Set,URL,AbortController,setTimeout:()=>1,clearTimeout(){},localStorage:{getItem:k=>local.get(k)||null,setItem:(k,v)=>local.set(k,v)},TrackerDashboard:{refresh(){if(record)notices.push(record.historyRows().map(p=>p.t).filter(Number.isFinite));}},fetch:async path=>{calls.push(path);if(pauseOlder&&!paused&&path.includes('order=recent')&&path.includes('since_s=0')){paused=true;await new Promise(resolve=>{unpause=resolve;});}return reply(path);}};
 if(cache)ctx.TrackerHistoryCache={get:async key=>clone(disk.get(key)||null),put:async(key,value)=>{const copy=clone(value);disk.set(key,copy);saved.push(copy);return true;},remove:async key=>disk.delete(key)};
 vm.createContext(ctx);for(const file of ['i18n','i18n-dashboard'])vm.runInContext(fs.readFileSync(web+file+'.js','utf8'),ctx);
 let source=fs.readFileSync(web+'recording.js','utf8').replace('return {update,track,historyRows,','return {test:{caches,cacheCompatible},update,track,historyRows,');vm.runInContext(source+';globalThis.r=TrackerRecords;',ctx);record=ctx.r;
 const p={gnss_fix:false,uptime_s:140000,satellites_used:24,satellites_visible_by_system:Object.fromEntries(names.map(n=>[n,10]))};
 record.update(raw,p,[],[6,4,5,8,0,1,null]);return {r:record,ctx,raw,p,calls,notices,saved,elements,local,isPaused:()=>paused,release:()=>unpause?.()};
}
async function until(predicate){for(let i=0;i<1000;i++){if(predicate())return;await new Promise(resolve=>setImmediate(resolve));}throw Error('condition not reached');}
(async()=>{
 const cold=boot({pauseOlder:true});let fullDone=false;const full=cold.r.syncHistory().then(()=>fullDone=true);await until(cold.isPaused);
 assert(cold.calls[0].includes('order=recent'));assert(cold.calls[0].includes('since_s='+Math.floor((NOW-14400000)/1000)));assert(!cold.calls[0].includes('after=0'));
 assert.equal(cold.notices[0].at(-1),NOW);assert.equal(cold.r.historyRows(true).length,1441);assert.equal(cold.r.historyLoad().loading,false);assert(cold.r.historyLoad().background);
 await cold.r.syncHistory(false,true);assert(!fullDone,'visible export must not await old background pages');assert(cold.saved.length<=2,'no full IndexedDB copy on every page');
 // A reload during the background transfer reuses the visible window and resumes its cursor.
 const partial=boot();await partial.r.syncHistory();assert.equal(partial.r.historyRows(true).length,14000);assert(!partial.calls.some(p=>p.includes('before_s=4294967295')));assert.equal(partial.r.historyLoad().message,'');
 cold.release();await full;assert(fullDone);assert.equal(cold.r.historyRows(true).length,14000);assert.equal(new Set(cold.r.historyRows(true).map(p=>p.seq)).size,14000);
 // A long-absent but otherwise valid cache must not force old-first delta downloads.
 const cacheKey='tracker-observations-v1:device-one:tracker-history',ancient=clone(disk.get(cacheKey));ancient.points=ancient.points.filter(p=>p.seq<=10);Object.assign(ancient,{snapshotCount:10,snapshotNext:11,cursor:10,complete:true,phase:'all',coveredSince:0,beforeS:0,beforeSeq:1});disk.set(cacheKey,ancient);const longAbsent=boot();await longAbsent.r.syncHistory();assert(longAbsent.calls[0].includes('order=recent'));assert.equal(longAbsent.r.historyRows(true).length,14000);
 // A warm reload uses only sequence deltas, including a newly imported old measurement.
 points.push({...clone(points[100]),seq:next++,t:NOW-100000000,imported:true});const warm=boot();await warm.r.syncHistory();assert.equal(warm.r.historyRows(true).length,14001);assert(warm.calls.length===1&&warm.calls[0].includes('after=14000'));
 const same=clone(warm.r.historyRows(true));vm.runInContext("TrackerI18n.setLanguage('de')",warm.ctx);warm.r.languageChanged();assert.deepEqual(clone(warm.r.historyRows(true)),same);assert.equal(warm.calls.length,1);
 // A different tracker never sees the first tracker's cached data.
 const other=boot({device:'device-two'});await other.r.syncHistory();assert(other.calls[0].includes('before_s=4294967295'));
 // Retiring sectors prunes the cached mirror without downloading the visible window again.
 points=points.filter(p=>p.seq>112);const wrapped=boot();await wrapped.r.syncHistory();assert.equal(wrapped.calls.length,0,'normal ring rollover must reuse the cache');assert(wrapped.r.historyRows(true).every(p=>p.seq>112));assert.equal(wrapped.r.historyRows(true).length,points.length);
 // The backend can return only {state:busy} while a history read owns its mutex.
 // This is temporary lock contention, not an empty or replaced ring.
 const busy=boot({device:'busy-device',cache:false,pauseOlder:true});const busyWork=busy.r.syncHistory();await until(busy.isPaused);
 const busyCount=busy.r.historyRows(true).length;busy.r.update({...busy.raw,sat_history:{state:'busy'},track:{state:'busy'}},busy.p,[],[6,4,5,8,0,1,null]);
 assert.equal(busy.r.historyLoad().loading,false);assert.equal(busy.r.historyRows(true).length,busyCount);
 busy.release();await busyWork;assert.equal(busy.r.historyRows(true).length,points.length);
 assert.equal(busy.calls.filter(path=>path.includes('before_s=4294967295')).length,1,'busy status must not restart history paging');
 assert(busy.notices.every(rows=>rows.some(t=>t>=NOW-14400000)),'busy status must not clear the recent view');
 // Rollover during an older-page transfer must preserve visible data and cursor.
 const rolling=boot({device:'rolling-device',cache:false,pauseOlder:true});const rollingWork=rolling.r.syncHistory();await until(rolling.isPaused);
 const firstRecent=rolling.calls.filter(path=>path.includes('before_s=4294967295')).length;
 const previousMeta=metadata();points=points.filter(p=>p.seq>224);points.push({...clone(points.at(-1)),seq:next++,t:NOW+10000});
 rolling.raw.sat_history=metadata();rolling.r.update(rolling.raw,rolling.p,[],[6,4,5,8,0,1,null]);
 assert.equal(rolling.r.historyLoad().loading,false);rolling.release();await rollingWork;
 assert.equal(rolling.r.historyRows(true).length,points.length);assert(rolling.r.historyRows(true).every(p=>p.seq>224));
 assert.equal(rolling.calls.filter(path=>path.includes('before_s=4294967295')).length,firstRecent,'rollover must not restart recent paging');
 assert(rolling.notices.every(rows=>rows.some(t=>t>=NOW-14400000)),'recent data must never disappear during backfill');
 // A slower status response from before the rollover must not move retention backward.
 rolling.raw.sat_history=previousMeta;rolling.r.update(rolling.raw,rolling.p,[],[6,4,5,8,0,1,null]);await rolling.r.syncHistory();
 assert.equal(rolling.r.historyRows(true).length,points.length);assert.equal(rolling.r.historyLoad().loading,false);
 // A global clear changes epoch; an old complete cache must not reappear.
 points=[];epoch=next;const cleared=boot();await cleared.r.syncHistory();assert.equal(cleared.r.historyRows(true).length,0);assert.equal(cleared.r.historyLoad().message,'');
 // Missing IndexedDB support is a performance fallback, never a loss of functionality.
 const unavailable=boot({cache:false});await unavailable.r.syncHistory();assert.equal(unavailable.r.historyRows(true).length,0);
 console.log('Recent history: newest visible window first, 14k complete background, immediate visible export, partial/warm reload cache, old import delta, device isolation, rollover pruning during backfill, stale status, clear invalidation, no duplicate points, no IndexedDB fallback: PASS');
})().catch(error=>{console.error(error);process.exit(1);});
