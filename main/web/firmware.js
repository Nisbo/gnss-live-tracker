/* Firmware preparation lives in the browser; only the verified application upload reaches the device. */
const TrackerFirmware=(()=>{
 const tr=(key,params)=>TrackerI18n.t('firmware.'+key,params);
 const $=id=>document.getElementById(id),text=(id,value)=>{$(id).textContent=value;};
 let raw=null,candidate=null,phase='idle',statusSnapshot=null,controller=null,generation=0,config=null,watching=false;
 const busy=()=>['reading','downloading','uploading','restarting'].includes(phase);
 const kib=n=>Math.round((n||0)/1024)+' KiB';
 function url(value){const u=new URL(value);if(!['http:','https:'].includes(u.protocol)||u.username||u.password)throw Error(tr('use_an_http_or_https_link_without'));u.hash='';return u.href;}
 function descriptor(bytes,size,limit){
  if(size<288||!limit||size>limit)throw Error(tr('slot_limit',{limit:kib(limit)}));
  const b=bytes instanceof Uint8Array?bytes:new Uint8Array(bytes),v=new DataView(b.buffer,b.byteOffset,b.byteLength);
  if(b.length<288||b[0]!==0xe9||v.getUint16(12,true)!==9||v.getUint32(32,true)!==0xabcd5432)throw Error(tr('this_is_not_an_esp32_s3_application'));
  const field=offset=>{let end=offset;while(end<offset+32&&b[end])end++;return new TextDecoder().decode(b.slice(offset,end));};
  if(field(80)!=='tracker_v2_lorawan_test')throw Error(tr('this_firmware_belongs_to_another_project_select'));
  return {version:field(48)||tr('unknown'),size};
 }
 async function readResponse(response,limit,signal,onProgress){
  if(!response.ok)throw Error(tr('http_status',{status:response.status}));
  const announced=Number(response.headers.get('content-length'))||0;
  if(announced>limit)throw Error(tr('the_download_file_is_larger_than_the'));
  if(!response.body?.getReader)throw Error(tr('this_browser_cannot_download_with_a_size'));
  const reader=response.body.getReader(),chunks=[];let size=0;
  try{while(true){if(signal.aborted)throw new DOMException(tr('cancelled'),'AbortError');const {done,value}=await reader.read();if(done)break;size+=value.byteLength;if(size>limit)throw Error(tr('the_download_file_is_larger_than_the'));chunks.push(value);onProgress(size,announced);}}catch(e){await reader.cancel().catch(()=>{});throw e;}finally{reader.releaseLock();}
  return new Blob(chunks,{type:'application/octet-stream'});
 }
 function controls(){const f=raw?.firmware||{},locked=busy()||f.updating||f.pending_validation;for(const id of ['ota-file','ota-url','ota-url-load'])$(id).disabled=locked||!f.slot_bytes;$('ota-upload').disabled=locked||!candidate;$('fw-cancel').hidden=phase!=='downloading';$('fw-candidate').hidden=!candidate;}
 function status(next,message,step='',error=false){statusSnapshot={next,message,step,error};phase=next;text('ota-note',typeof message==='function'?message():message);$('ota-note').parentElement.dataset.error=String(error);$('fw-state').dataset.state=error?'error':next==='success'?'success':'';text('fw-state',({idle:tr('ready'),ready:tr('file_selected'),reading:tr('checking_file'),downloading:tr('downloading'),uploading:tr('updating'),restarting:tr('restart_verification'),success:tr('update_confirmed'),error:tr('needs_attention')})[next]||tr('ready'));for(const el of document.querySelectorAll('#fw-steps li'))el.dataset.active=String(el.dataset.step===step);controls();}
 function progress(loaded,total){$('ota-progress').hidden=false;if(total){$('ota-progress').max=100;$('ota-progress').value=Math.min(100,Math.round(loaded/total*100));}else $('ota-progress').removeAttribute('value');}
 function update(value){raw=value;const f=raw.firmware||{},slot=f.slot_bytes||0; text('fw-version',f.version?'v'+f.version:'—');text('fw-slot',f.running_slot?tr('active')+f.running_slot:'—');text('fw-image',kib(f.image_bytes));$('fw-meter').value=slot?Math.round(f.image_bytes/slot*100):0;text('fw-space',slot?tr('slot_space',{free:kib(Math.max(0,slot-f.image_bytes)),total:kib(slot)}):tr('no_ota_slot_available'));text('fw-ram',kib(f.free_heap_bytes));text('fw-ram-min',tr('minimum_since_startup')+kib(f.min_free_heap_bytes));text('memory','Flash: '+Math.round((f.flash_bytes||0)/1048576)+tr('mib_active_slot')+(f.running_slot||'—')+tr('next_slot')+(f.next_slot||'—')+'.');
  if(!busy()&&phase==='idle'){text('fw-state',f.pending_validation?tr('confirming_new_firmware'):f.updating?tr('updating'):tr('ready'));if(f.pending_validation)text('ota-note',tr('new_firmware_started_it_will_be_confirmed'));else text('ota-note',tr('select_a_file_or_enter_a_download'));}controls();
 }
 function renderCandidate(){if(!candidate)return;const f=raw?.firmware||{};text('fw-candidate-version','Version '+candidate.version);text('fw-candidate-info',candidate.name+' · '+kib(candidate.size)+' · '+(candidate.source==='url'?tr('from_download_link'):tr('local_file'))+(candidate.version===f.version?tr('same_as_the_installed_version'):''));}
 async function prepare(blob,name,source,id){const f=raw?.firmware||{},info=descriptor(await blob.slice(0,288).arrayBuffer(),blob.size,f.slot_bytes);if(id!==generation)return;candidate={...info,blob,name,source};renderCandidate();$('ota-progress').hidden=true;status('ready',()=>(tr('tracker_v2_application_detected_the_tracker_will')),'load');}
 function failure(e){candidate=null;$('fw-recheck').hidden=true;$('ota-progress').hidden=true;status('error',()=>(e.name==='AbortError'?tr('download_cancelled_no_firmware_was_transferred'):e.message||tr('could_not_load_the_file')),'load',true);}
 async function chooseFile(){if(busy())return;const file=$('ota-file').files[0];candidate=null;if(!file){status('idle',()=>(tr('select_a_file_or_enter_a_download')));return;}const id=++generation;status('reading',()=>(tr('checking_file_2')),'load');try{await prepare(file,file.name,'file',id);}catch(e){if(id===generation)failure(e);}}
 async function download(event){event.preventDefault();if(busy())return;let link;try{link=url($('ota-url').value);}catch(e){failure(e);return;}const id=++generation;candidate=null;controller=new AbortController();const signal=controller.signal;status('downloading',()=>(tr('downloading_firmware_to_this_browser_nothing_on')),'load');progress(0,0);let timedOut=false;const timer=setTimeout(()=>{timedOut=true;controller?.abort();},120000);
  try{const response=await fetch(link,{signal,credentials:'omit',referrerPolicy:'no-referrer',cache:'no-store'});const blob=await readResponse(response,raw?.firmware?.slot_bytes||0,signal,(n,total)=>{progress(n,total);text('ota-note','Download: '+kib(n)+(total?tr('of')+kib(total):'')+tr('not_yet_installing_on_the_tracker'));});await prepare(blob,new URL(link).pathname.split('/').pop()||tr('firmware_download'),'url',id);}catch(e){if(id===generation){if(timedOut)e=Error(tr('the_download_timed_out_try_again_or'));else if(e instanceof TypeError)e=Error(tr('download_failed_check_the_link_and_internet'));failure(e);}}finally{clearTimeout(timer);controller=null;controls();}
 }
 const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
 async function verify(previous,expected){if(watching)return;watching=true;$('fw-recheck').hidden=true;config.setUploading(true);status('restarting',()=>(tr('waiting_for_restart_then_checking_the_new')),'restart');$('ota-progress').hidden=false;$('ota-progress').removeAttribute('value');const deadline=Date.now()+150000;let last=null;
  try{while(Date.now()<deadline){await wait(2000);const abort=new AbortController(),timer=setTimeout(()=>abort.abort(),5000);try{const response=await fetch('/api/status',{signal:abort.signal,cache:'no-store'});if(!response.ok)continue;last=await response.json();const f=last.firmware||{};update(last);const rebooted=f.running_slot!==previous.slot||(last.diagnostics?.uptime_ms??Infinity)<previous.uptime;
     if(rebooted&&f.version===expected){if(!f.pending_validation){progress(100,100);status('success',()=>('Version '+f.version+tr('has_started_and_is_confirmed_reload_the')),'restart');$('fw-recheck').textContent=tr('reload_web_interface');$('fw-recheck').hidden=false;return;}text('ota-note','Version '+f.version+tr('is_running_waiting_for_confirmation_after_30'));}
    }catch(e){}finally{clearTimeout(timer);}}
   status('error',()=>(last?tr('could_not_confirm_the_new_firmware_the')+(last.firmware?.version||tr('unknown_2'))+tr('check_the_device_status_again'):tr('the_tracker_is_not_reachable_yet_check')),'restart',true);$('fw-recheck').textContent=tr('check_device_status_again');$('fw-recheck').hidden=false;
  }finally{watching=false;config.setUploading(false);controls();}
 }
 function install(){if(busy()||!candidate||$('ota-upload').disabled)return;const file=candidate,previous={slot:raw.firmware.running_slot,uptime:raw.diagnostics?.uptime_ms||0};config.setUploading(true);status('uploading',()=>(tr('transferring_firmware_to_the_tracker_keep_the')),'upload');progress(0,file.size);$('fw-recheck').hidden=true;
  const xhr=new XMLHttpRequest();xhr.open('POST','/api/ota');xhr.setRequestHeader('Content-Type','application/octet-stream');xhr.setRequestHeader('X-Tracker-Token',config.getToken());xhr.timeout=240000;xhr.upload.onprogress=e=>{if(e.lengthComputable){progress(e.loaded,e.total);text('ota-note',e.loaded<e.total?tr('transfer')+kib(e.loaded)+tr('of')+kib(e.total)+'.':tr('transferred_the_tracker_is_checking_the_complete'));}};
  xhr.onload=()=>{if(xhr.status===200){verify(previous,file.version);return;}config.setUploading(false);status('error',()=>(tr('update_rejected')+TrackerI18n.serverMessage(xhr.responseText)+tr('the_previous_firmware_remains_active')),'upload',true);};xhr.onerror=xhr.ontimeout=()=>{verify(previous,file.version);};xhr.send(file.blob);
 }
 function init(options){config=options;$('ota-file').addEventListener('change',chooseFile);$('ota-url-form').addEventListener('submit',download);$('ota-upload').addEventListener('click',install);$('fw-cancel').addEventListener('click',()=>controller?.abort());$('fw-recheck').addEventListener('click',()=>location.reload());controls();}
 function languageChanged(){if(raw)update(raw);renderCandidate();if(statusSnapshot&&phase!=='idle'){const s=statusSnapshot;status(s.next,s.message,s.step,s.error);}if(!$('fw-recheck').hidden)$('fw-recheck').textContent=phase==='success'?tr('reload_web_interface'):tr('check_device_status_again');}
 return {init,update,languageChanged};
})();
