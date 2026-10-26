/* Test the real IndexedDB adapter: atomic incremental writes and optional storage failures. */
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const source=fs.readFileSync(__dirname+'/../main/web/history-cache.js','utf8'),clone=structuredClone;
function fakeDatabase(){const maps=new Map();let writes=0,failSeq=null;
 const matches=(key,range)=>Array.isArray(key)&&key[0]===range.lower[0]&&key[1]>=range.lower[1]&&key[1]<=range.upper[1];
 const db={objectStoreNames:{contains:name=>maps.has(name)},createObjectStore(name){maps.set(name,new Map());},close(){},transaction(names,mode){
  if(!Array.isArray(names))names=[names];const local=new Map(names.map(name=>[name,new Map([...maps.get(name)].map(([key,value])=>[key,clone(value)]))]));let pending=0,aborted=false,done=false;
  const tx={objectStore(name){const map=local.get(name);function operation(fn){const request={};pending++;setImmediate(()=>{if(!aborted){try{request.result=fn();request.onsuccess?.();}catch(error){aborted=true;request.error=error;request.onerror?.();tx.onerror?.();tx.onabort?.();}}pending--;finish();});return request;}
   return {get:key=>operation(()=>map.get(JSON.stringify(key))),getAll:range=>operation(()=>[...map].filter(([key])=>matches(JSON.parse(key),range)).sort((a,b)=>JSON.parse(a[0])[1]-JSON.parse(b[0])[1]).map(([,value])=>value)),put:(value,key)=>operation(()=>{if(value?.seq===failSeq){failSeq=null;throw Error('quota');}writes++;map.set(JSON.stringify(key),clone(value));}),delete:key=>operation(()=>{if(key&&key.lower){for(const id of map.keys())if(matches(JSON.parse(id),key))map.delete(id);}else map.delete(JSON.stringify(key));})};}};
  function finish(){if(pending||done)return;done=true;if(!aborted){if(mode==='readwrite')for(const [name,map]of local)maps.set(name,map);tx.oncomplete?.();}}
  setImmediate(finish);return tx;
 }};
 return {api:{open(){const request={};setImmediate(()=>{request.result=db;request.onupgradeneeded?.();request.onsuccess?.();});return request;}},rows:()=>maps.get('samples')?.size||0,writes:()=>writes,fail:seq=>failSeq=seq};
}
function boot(api){const context={indexedDB:api,IDBKeyRange:{bound:(lower,upper)=>({lower,upper})},Map,Set};vm.createContext(context);vm.runInContext(source+';globalThis.cache=TrackerHistoryCache;',context);return context.cache;}
(async()=>{
 const fake=fakeDatabase(),cache=boot(fake.api),head={version:1,epoch:1,retention:1,snapshotCount:2,snapshotNext:3,cursor:2,points:[{seq:1,t:1000},{seq:2,t:2000}]};
 assert.equal(await cache.get('one'),null);assert(await cache.put('one',head));assert.equal(fake.rows(),2);let saved=await cache.get('one');assert.deepEqual(clone(saved.points),head.points);
 let before=fake.writes();assert(await cache.put('one',{...head,points:[...head.points,{seq:3,t:3000}],snapshotCount:3,snapshotNext:4,cursor:3}));assert.equal(fake.writes()-before,2,'one new sample plus metadata, not the entire history');assert.equal(fake.rows(),3);
 fake.fail(4);assert.equal(await cache.put('one',{...head,points:[...head.points,{seq:3,t:3000},{seq:4,t:4000}],snapshotCount:4,snapshotNext:5,cursor:4}),false);saved=await cache.get('one');assert.equal(saved.points.length,3);assert.equal(saved.snapshotNext,4,'metadata must roll back with failed samples');
 assert(await cache.put('one',{...head,points:[{seq:4,t:4000}],epoch:4,retention:4,snapshotCount:1,snapshotNext:5,cursor:4}));saved=await cache.get('one');assert.deepEqual(clone(saved.points),[{seq:4,t:4000}]);assert.equal(fake.rows(),1,'new retention removes stale samples');
 assert(await cache.put('two',head));await cache.remove('one');assert.equal(await cache.get('one'),null);assert.equal((await cache.get('two')).points.length,2);
 const unavailable=boot(undefined);assert.equal(await unavailable.get('one'),null);assert.equal(await unavailable.put('one',head),false);await unavailable.remove('one');
 console.log('IndexedDB history cache: metadata/sample atomicity, incremental row writes, quota rollback, retention cleanup, device isolation and unavailable storage fallback: PASS');
})().catch(error=>{console.error(error);process.exit(1);});
