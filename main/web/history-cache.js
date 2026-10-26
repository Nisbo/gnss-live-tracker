/* Disposable device mirror, separate from browser recordings. IndexedDB writes only
 * newly received samples; metadata/samples commit together, never whole-history rewrites. */
'use strict';
const TrackerHistoryCache=(()=>{
 const DATABASE='gnss-tracker-cache-v1',META='statistics',SAMPLES='samples';let opening;
 const known=new Map(),queues=new Map();
 function database(){
  let api;try{api=globalThis.indexedDB;}catch(error){}if(!api)return Promise.resolve(null);
  if(!opening)opening=new Promise(resolve=>{let request;try{request=api.open(DATABASE,2);}catch(error){resolve(null);return;}
   request.onupgradeneeded=()=>{for(const name of [META,SAMPLES])if(!request.result.objectStoreNames.contains(name))request.result.createObjectStore(name);};
   request.onerror=request.onblocked=()=>resolve(null);request.onsuccess=()=>{request.result.onversionchange=()=>{request.result.close();opening=null;};resolve(request.result);};
  });return opening;
 }
 const range=key=>IDBKeyRange.bound([key,0],[key,Number.MAX_SAFE_INTEGER]);
 function queued(key,operation){const work=(queues.get(key)||Promise.resolve()).then(operation,operation);queues.set(key,work);work.finally(()=>{if(queues.get(key)===work)queues.delete(key);});return work;}
 async function get(key){const db=await database();if(!db)return null;return new Promise(resolve=>{try{
  const tx=db.transaction([META,SAMPLES],'readonly'),request=tx.objectStore(META).get(key);let result=null;
  request.onsuccess=()=>{const head=request.result;if(!head)return;
   if(Array.isArray(head.points)){result=head;return;} // Read-only compatibility with early snapshot caches.
   const rows=tx.objectStore(SAMPLES).getAll(range(key));rows.onsuccess=()=>{result={...head,points:rows.result};known.set(key,{epoch:head.epoch,retention:head.retention,seqs:new Set(rows.result.map(p=>p.seq))});};
  };
  tx.oncomplete=()=>resolve(result);tx.onerror=tx.onabort=()=>resolve(null);
 }catch(error){resolve(null);}});}
 function put(key,value){return queued(key,async()=>{const db=await database();if(!db)return false;return new Promise(resolve=>{try{
  const tx=db.transaction([META,SAMPLES],'readwrite'),store=tx.objectStore(SAMPLES),request=tx.objectStore(META).get(key);let next;
  request.onsuccess=()=>{const old=request.result,prior=known.get(key),reset=!old||old.epoch!==value.epoch||old.retention!==value.retention||old.snapshotNext>value.snapshotNext;
   if(reset)store.delete(range(key));
   const seqs=!reset&&prior?.epoch===value.epoch&&prior?.retention===value.retention?new Set(prior.seqs):new Set();
   for(const p of value.points)if(!seqs.has(p.seq)){store.put(p,[key,p.seq]);seqs.add(p.seq);}
   const {points,...head}=value;tx.objectStore(META).put(head,key);next={epoch:head.epoch,retention:head.retention,seqs};
  };
  tx.oncomplete=()=>{if(next)known.set(key,next);resolve(true);};tx.onerror=tx.onabort=()=>{known.delete(key);resolve(false);};
 }catch(error){known.delete(key);resolve(false);}});});}
 function remove(key){return queued(key,async()=>{const db=await database();known.delete(key);if(!db)return;return new Promise(resolve=>{try{const tx=db.transaction([META,SAMPLES],'readwrite');tx.objectStore(META).delete(key);tx.objectStore(SAMPLES).delete(range(key));tx.oncomplete=tx.onerror=tx.onabort=()=>resolve();}catch(error){resolve();}});});}
 return {get,put,remove};
})();
