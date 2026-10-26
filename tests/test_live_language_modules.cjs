/* Language changes repaint the current module state without downloads or form resets. */
'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const web=__dirname+'/../main/web/';
(async()=>{
 const nodes=new Map(),listeners=[],calls=[],timers=[];let rafCalls=0;
 function el(id='',tag='div'){
  return {id,tagName:tag,children:[],style:{},dataset:{},handlers:{},value:'',checked:false,disabled:false,hidden:true,textContent:'',type:'',files:[],parentElement:{dataset:{}},
   append(...items){for(const item of items){item.parentElement=this;this.children.push(item);}},replaceChildren(...items){this.children=[];this.append(...items);},
   addEventListener(name,fn){(this.handlers[name]??=[]).push(fn);},setAttribute(k,v){this[k]=String(v);},getAttribute(k){return this[k]??null;},removeAttribute(k){delete this[k];},
   querySelectorAll(selector){const values=[];function walk(n){for(const c of n.children||[]){if(c.tagName==='input'&&(selector==='input'||selector==='input:checked'&&c.checked))values.push(c);walk(c);}}walk(this);return values;},
   get nextSibling(){const all=this.parentElement?.children||[],i=all.indexOf(this);return all[i+1]||null;},getContext(){return {putImageData(){}};},showModal(){this.open=true;},close(){this.open=false;},getBoundingClientRect(){return {width:640,height:320,left:0,top:0};},closest(){return get('timebar');},classList:{toggle(){}},click(){}
  };
 }
 const get=id=>{if(!nodes.has(id))nodes.set(id,el(id));return nodes.get(id);};
 const document={getElementById:get,createElement:tag=>el('',tag),createTextNode:text=>Object.assign(el('','#text'),{textContent:text}),createElementNS:(ns,tag)=>el('',tag),querySelectorAll:()=>[],addEventListener(name,fn){listeners.push([name,fn]);},body:{style:{}},fullscreenElement:null};
 const ctx={document,Intl,Date,Math,Number,Set,Map,TextEncoder,TextDecoder,Uint8Array,Uint8ClampedArray,DataView,Blob,URL,URLSearchParams,AbortSignal,AbortController,performance:{now:()=>1000},window:{indexedDB:null},localStorage:{getItem:key=>key==='tracker-language'?'de':null,setItem(){}},setTimeout(fn){timers.push(fn);return timers.length;},clearTimeout(){},setInterval(){},requestAnimationFrame(){rafCalls++;return rafCalls;},fetch(...args){calls.push(args);throw Error('Language change must not fetch');},location:{reload(){throw Error('Language change must not reload');}}};
 vm.createContext(ctx);
 for(const name of ['i18n','i18n-settings','i18n-sky','timezones','firmware','display','credentials','backup','adr','extras','orbits']){
  let source=fs.readFileSync(web+name+'.js','utf8');
  if(name==='backup')source=source.replace('return {init,update,languageChanged};','return {init,update,languageChanged,test:{loadFile,checked,bundle:()=>bundle}};');
  if(name==='orbits')source=source.replace('return {update,showTab,languageChanged};','return {update,showTab,languageChanged,test:{set(){records=[{id:"1",name:"Test",epoch:1792972800000}];catalog={format:"tle",source:"fixture"};selected="1";},state:()=>({records,catalog,selected})}};');
  vm.runInContext(source,ctx,{filename:name+'.js'});
 }
 const run=s=>vm.runInContext(s,ctx),language=code=>run(`TrackerI18n.setLanguage('${code}')`);
 for(const [id,value]of Object.entries({'display-mode':'on','display-timeout':'60','display-brightness':'75','display-saver-brightness':'15'}))get(id).value=value;
 run('TrackerDisplay.init();TrackerCredentials.init();TrackerBackup.init();TrackerFirmware.init({getToken:()=>"token",setUploading(){}})');
 const status={request_token:'token',firmware:{version:'0.29.0',slot_bytes:2097152,running_slot:'ota_0',image_bytes:1790000,free_heap_bytes:120000,min_free_heap_bytes:80000,flash_bytes:8388608},display:{language:0,mode:'on',timeout_s:60,brightness:75,brightness_supported:true,on:true,page:0,boot_on:true,highlight:3,clock_zone:'Europe/Berlin'},lora:{credentials:{configured:true,app_key_set:true,dev_eui:'0011223344556677',join_eui:'0000000000000000'},settings:{region:0},adr_diagnostics:{total:0,events:[]}},backup:{busy:false}};ctx.status=status;
 run('TrackerDisplay.update(status);TrackerCredentials.update(status);TrackerBackup.update(status);TrackerFirmware.update(status);TrackerAdr.update(status)');
 get('display-mode').value='auto';get('display-timeout').value='15';get('display-language').value='1';get('display-zone').value='America/New_York';
 const checkbox=get('display-cycle').children[2].children[0].children[0];checkbox.checked=false;checkbox.handlers.change[0]();
 get('otaa-dev').value='AABBCCDDEEFF0011';get('otaa-key').value='DRAFT-NOT-A-REAL-KEY';get('otaa-dev').handlers.input[0]();
 // Load a small real backup; changing labels must preserve the parsed bytes and selections.
 const manifest=Buffer.from(JSON.stringify({format:1,model:'heltec-wireless-tracker-v2',layout:'track32-sat36-v1',firmware:'0.29.0',device:'fixture',settings:[],areas:[]}));
 function crc(bytes){let n=0xffffffff;for(const b of bytes){n^=b;for(let i=0;i<8;i++)n=(n>>>1)^(0xedb88320&-(n&1));}return(n^0xffffffff)>>>0;}
 const backup=Buffer.alloc(16+manifest.length);backup.write('GLTBK001');backup.writeUInt32LE(manifest.length,8);backup.writeUInt32LE(crc(manifest),12);manifest.copy(backup,16);
 const backupFile={size:backup.length,arrayBuffer:async()=>backup.buffer.slice(backup.byteOffset,backup.byteOffset+backup.byteLength)};get('backup-file').files=[backupFile];await run('TrackerBackup.test.loadFile()');
 const parsed=run('TrackerBackup.test.bundle()');get('restore-groups').querySelectorAll('input')[1].checked=false;const selectedBefore=Array.from(run('TrackerBackup.test.checked("restore-groups")'));
 const image=new Uint8Array(320),view=new DataView(image.buffer);image[0]=0xe9;view.setUint16(12,9,true);view.setUint32(32,0xabcd5432,true);image.set(Buffer.from('0.30.0'),48);image.set(Buffer.from('tracker_v2_lorawan_test'),80);
 const file=Object.assign(new Blob([image]),{name:'tracker.bin'});get('ota-file').files=[file];await get('ota-file').handlers.change[0]();assert.equal(get('fw-state').textContent,'Datei ausgewählt');
 run('TrackerOrbits.test.set()');const orbitBefore=run('TrackerOrbits.test.state()');const listenerCount=listeners.length,formListenerCount=get('display-form').handlers.submit.length;
 language('en');run('TrackerDisplay.languageChanged();TrackerCredentials.languageChanged();TrackerBackup.languageChanged();TrackerFirmware.languageChanged();TrackerAdr.languageChanged();TrackerExtras.languageChanged();TrackerOrbits.languageChanged()');
 assert.equal(calls.length,0);assert.equal(listeners.length,listenerCount);assert.equal(get('display-form').handlers.submit.length,formListenerCount);
 assert.equal(get('display-mode').value,'auto');assert.equal(get('display-timeout').value,'15');assert.equal(get('display-language').value,'1');assert.equal(get('display-zone').value,'America/New_York');
 assert.equal(get('display-cycle').children[0].children[0].children[1].textContent,'Overview');assert.equal(get('display-cycle').children[2].children[0].children[0].checked,false);assert.match(get('display-mode-help').textContent,/15 seconds/);
 assert.equal(get('otaa-dev').value,'AABBCCDDEEFF0011');assert.equal(get('otaa-key').value,'DRAFT-NOT-A-REAL-KEY');assert.equal(get('otaa-reveal').textContent,'Show AppKey');
 assert.equal(run('TrackerBackup.test.bundle()'),parsed);assert.deepEqual(Array.from(run('TrackerBackup.test.checked("restore-groups")')),selectedBefore);assert.equal(get('backup-file').files[0],backupFile);assert.match(get('restore-groups').children[0].children[1].textContent,/Wi-Fi/);
 assert.equal(get('ota-file').files[0],file);assert.equal(get('fw-state').textContent,'File selected');assert.match(get('fw-candidate-info').textContent,/local file/i);assert.match(get('ota-note').textContent,/Tracker V2 application/);
 assert.match(get('adr-events-note').textContent,/No ADR requests/);assert.equal(run('TrackerOrbits.test.state().records'),orbitBefore.records);assert.equal(run('TrackerOrbits.test.state().catalog'),orbitBefore.catalog);assert.equal(run('TrackerOrbits.test.state().selected'),'1');assert.match(get('orbit-data-note').textContent,/fixture/);
 const selected=get('display-zone').children.find(n=>n.value==='America/New_York');assert.match(selected.textContent,/America/);
 language('de');run('TrackerDisplay.languageChanged();TrackerCredentials.languageChanged();TrackerBackup.languageChanged();TrackerFirmware.languageChanged();TrackerAdr.languageChanged();TrackerExtras.languageChanged();TrackerOrbits.languageChanged()');
 assert.equal(calls.length,0);assert.equal(get('display-cycle').children[0].children[0].children[1].textContent,'Übersicht');assert.match(get('display-zone').children.find(n=>n.value==='America/New_York').textContent,/Amerika/);assert.equal(get('fw-state').textContent,'Datei ausgewählt');assert.deepEqual(Array.from(run('TrackerBackup.test.checked("restore-groups")')),selectedBefore);
 console.log('Live module language changes: zero fetch/reload, preserved firmware file and phase, backup file/selection, credential/display drafts, timezone and orbit catalog/model identity; labels DE/EN: PASS');
})().catch(error=>{console.error(error);process.exitCode=1});
