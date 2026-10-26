/* Wi-Fi hotspot controls and optional Internet checks. No secrets in browser storage. */
const TrackerNetwork=(()=>{
 const $=id=>document.getElementById(id),tr=(k,p)=>TrackerI18n.t('network.'+k,p);
 function age(seconds){const unit=seconds<60?'seconds':seconds<3600?'minutes':'hours',count=Math.floor(seconds/(unit==='seconds'?1:unit==='minutes'?60:3600));return tr('age_'+unit+(count===1?'_one':''),{count});}
 let raw=null,tab='live',wifiAction,getToken,initialized=false,loaded=false,loading=false,baseline='',timer,internetInitialized=false,lastPositions=null;
 function hide(){clearTimeout(timer);$('ap-password').type='password';$('ap-reveal').textContent=tr('show');$('ap-reveal').setAttribute('aria-pressed','false');}
 function warning(){if(!raw)return;$('ap-open-warning').hidden=!(raw.wifi?.hotspot_open||loaded&&!$('ap-password').value);}
 async function loadPassword(){
  if(loaded||loading||!raw)return;loading=true;$('ap-load-note').textContent=tr('loading');$('ap-retry').hidden=true;
  try{let reply=raw.wifi?.hotspot_open?{password:''}:await wifiAction({action:'reveal_hotspot'});$('ap-password').value=reply.password;reply.password='';baseline=$('ap-password').value;loaded=true;$('ap-password').disabled=$('ap-reveal').disabled=$('hotspot-save').disabled=false;$('ap-load-note').textContent='';hide();}
  catch(error){$('ap-load-note').textContent=tr('load_failed');$('ap-retry').hidden=false;}finally{loading=false;warning();}
 }
 async function internet(data){const r=await fetch('/api/internet',{method:'POST',headers:{'Content-Type':'application/json','X-Tracker-Token':getToken()},body:JSON.stringify(data)});if(!r.ok)throw Error(TrackerI18n.serverMessage(await r.text()));return r.json();}
 function update(value){
  raw=value;const w=raw.wifi||{},n=raw.internet||{};if(!initialized){$('ap-ssid').value=w.hotspot_ssid||'Tracker-V2-Setup';initialized=true;}
  $('hotspot-state').textContent=tr(w.setup_active?'on':'off');$('hotspot-mode').textContent=w.hotspot_override?tr('user_override',{mode:tr('mode_name_'+(w.hotspot_saved_mode||'auto'))}):tr('mode_'+(w.hotspot_mode||'auto'));
  for(const [id,mode]of [['hotspot-auto','auto'],['hotspot-on','on'],['hotspot-off','off']]){$(id).setAttribute('aria-pressed',String((w.hotspot_mode||'auto')===mode));$(id).className=(w.hotspot_mode||'auto')===mode?'hotspot-selected':'ghost';}
  warning();if(tab==='wifi'&&!loaded&&!loading&&$('ap-retry').hidden)loadPassword();
  if(!internetInitialized&&n.state!==undefined){$('internet-auto').checked=!!n.automatic;$('internet-position').checked=!!n.position;$('internet-events').checked=!!n.events;internetInitialized=true;}
  const positions=raw.lora?.settings?.position_enabled!==false;
  if(lastPositions!==positions){$('internet-position').checked=positions&&!!n.position;$('internet-events').checked=!positions&&!!n.events;lastPositions=positions;}
  $('internet-position').disabled=!positions;$('internet-events').disabled=positions;
  $('internet-report-hint').textContent=tr(positions?'position_mode_hint':'event_mode_hint');
  $('internet-auto-hint').textContent=$('internet-auto').checked?tr('automatic_hint'):'';
  $('internet-state').dataset.state=n.checking?'checking':n.state===2?'reachable':n.state===1?'unavailable':'unknown';
  $('internet-state').textContent=tr(n.checking?'checking':n.state===2?'reachable':n.state===1?'unavailable':'unknown');$('internet-check').disabled=!!n.checking;
  $('internet-detail').textContent=n.age_s>=0?tr('last_check',{age:age(n.age_s),duration:n.duration_ms})+' · '+tr(!n.wifi_ip?'no_ip':n.http_status===204?'http_ok':'check_failed')+(n.state!==(n.http_status===204?2:1)&&!n.checking?tr('confirming'):'')+(n.event_pending?' · '+tr('pending'):''):tr('not_checked');
 }
 function init(options){
  wifiAction=options.wifiAction;getToken=options.getToken;$('ap-retry').hidden=true;$('ap-password').addEventListener('input',warning);
  $('ap-reveal').addEventListener('click',()=>{if($('ap-password').type==='text'){hide();return;}$('ap-password').type='text';$('ap-reveal').textContent=tr('hide');$('ap-reveal').setAttribute('aria-pressed','true');timer=setTimeout(hide,30000);});
  $('ap-retry').addEventListener('click',loadPassword);document.addEventListener('visibilitychange',()=>{if(document.hidden)hide();});
  $('hotspot-form').addEventListener('submit',async e=>{
   e.preventDefault();if(!loaded)return;const data={action:'hotspot',ssid:$('ap-ssid').value,password:$('ap-password').value},changed=data.ssid!==raw.wifi.hotspot_ssid||data.password!==baseline;
   if(changed&&raw.wifi.setup_active&&!await TrackerDashboard.confirm(tr('save_title'),tr('save_warning'),tr('save'),false))return;
   $('hotspot-save').disabled=true;try{await wifiAction(data);baseline=data.password;hide();$('hotspot-result').textContent=tr('saved');}catch(error){$('hotspot-result').textContent=tr('error',{error:error.message});}finally{$('hotspot-save').disabled=false;}
  });
  for(const [id,mode]of [['hotspot-auto','auto'],['hotspot-on','on'],['hotspot-off','off']])$(id).addEventListener('click',async()=>{
   const interrupts=raw?.wifi?.setup_active&&(mode==='off'||mode==='auto'&&raw?.wifi?.state==='connected');
   if(interrupts&&!await TrackerDashboard.confirm(tr('off_title'),tr('off_warning'),tr(mode==='auto'?'auto':'switch_off'),false))return;
   $(id).disabled=true;try{await wifiAction({action:'hotspot',mode});$('hotspot-result').textContent=tr('mode_'+mode);}catch(error){$('hotspot-result').textContent=tr('error',{error:error.message});}finally{$(id).disabled=false;}
  });
  $('internet-check').addEventListener('click',async()=>{try{$('internet-check').disabled=true;$('internet-result').textContent='';await internet({action:'check'});}catch(error){$('internet-result').textContent=tr('error',{error:error.message});}finally{$('internet-check').disabled=false;}});
  for(const id of ['internet-position','internet-events'])$(id).addEventListener('change',()=>{if($(id).checked){$('internet-auto').checked=true;$(id==='internet-position'?'internet-events':'internet-position').checked=false;}$('internet-auto-hint').textContent=$('internet-auto').checked?tr('automatic_hint'):'';});
  $('internet-auto').addEventListener('change',()=>{if(!$('internet-auto').checked)$('internet-position').checked=$('internet-events').checked=false;$('internet-auto-hint').textContent=$('internet-auto').checked?tr('automatic_hint'):'';});
  $('internet-form').addEventListener('submit',async e=>{e.preventDefault();$('internet-save').disabled=true;try{await internet({automatic:$('internet-auto').checked,position:$('internet-position').checked,events:$('internet-events').checked});$('internet-result').textContent=tr('settings_saved');}catch(error){$('internet-result').textContent=tr('error',{error:error.message});}finally{$('internet-save').disabled=false;}});
 }
 function showTab(name){tab=name;if(name!=='wifi')hide();else if(raw&&!loaded&&!loading)loadPassword();}
 function languageChanged(){hide();if(raw)update(raw);}
 return {init,update,showTab,languageChanged};
})();
