/* Exercise hotspot/Internet UI and decoder in both languages, with no network access. */
const fs=require('fs'),vm=require('vm'),assert=require('assert'),web=__dirname+'/../main/web/';
function element(){return {value:'',type:'password',checked:false,disabled:false,hidden:false,textContent:'',dataset:{},handlers:{},addEventListener(k,f){this.handlers[k]=f},setAttribute(k,v){this[k]=v}}}
async function run(language){
 const nodes=new Map(),$=id=>{if(!nodes.has(id))nodes.set(id,element());return nodes.get(id)},actions=[],requests=[],confirmations=[];let allow=true;
 const ctx={document:{hidden:false,getElementById:$,addEventListener(){}},Intl,localStorage:{getItem:()=>language,setItem(){}},navigator:{language},setTimeout:()=>1,clearTimeout(){},TrackerDashboard:{confirm:async(...a)=>{confirmations.push(a);return allow}},fetch:async(url,opt)=>{requests.push({url,data:JSON.parse(opt.body),token:opt.headers['X-Tracker-Token']});return {ok:true,json:async()=>({ok:true})}}};
 vm.createContext(ctx);for(const file of ['i18n.js','i18n-inline.js','network.js','ttn-position-decoder.js'])vm.runInContext(fs.readFileSync(web+file,'utf8'),ctx);
 const n=vm.runInContext('TrackerNetwork',ctx);n.init({getToken:()=> 'synthetic-token',wifiAction:async a=>{actions.push({...a});return a.action==='reveal_hotspot'?{password:'example-password'}:{ok:true}}});
 const raw={wifi:{hotspot_ssid:'Example setup',hotspot_open:false,hotspot_mode:'auto',setup_active:true,state:'connected'},internet:{state:0,age_s:-1,automatic:false,position:false,events:false}};
 n.update(raw);n.showTab('wifi');await new Promise(setImmediate);assert.equal($('ap-password').value,'example-password');assert.equal($('ap-password').type,'password');assert(!$('hotspot-save').disabled);
 $('ap-reveal').handlers.click();assert.equal($('ap-password').type,'text');n.showTab('live');assert.equal($('ap-password').type,'password');n.showTab('wifi');assert.equal(actions.filter(a=>a.action==='reveal_hotspot').length,1);
 $('ap-password').value='';$('ap-password').handlers.input();assert(!$('ap-open-warning').hidden);
 allow=false;const ac=actions.length;await $('hotspot-form').handlers.submit({preventDefault(){}});assert.equal(actions.length,ac);
 allow=true;await $('hotspot-form').handlers.submit({preventDefault(){}});assert.deepEqual(actions.at(-1),{action:'hotspot',ssid:'Example setup',password:''});
 await $('hotspot-off').handlers.click();assert.deepEqual(actions.at(-1),{action:'hotspot',mode:'off'});assert(confirmations.length>=3);
 raw.wifi.setup_active=false;raw.wifi.hotspot_open=true;raw.wifi.hotspot_mode='off';n.update(raw);assert(!$('ap-open-warning').hidden);assert.equal($('hotspot-off')['aria-pressed'],'true');
 raw.lora={settings:{position_enabled:false}};n.update(raw);assert(!$('internet-events').disabled&&$('internet-position').disabled);assert.equal($('hotspot-off').className,'hotspot-selected');
 $('internet-events').checked=true;$('internet-events').handlers.change();assert($('internet-auto').checked);$('internet-auto').checked=false;$('internet-auto').handlers.change();assert(!$('internet-events').checked&&!$('internet-position').checked);
 await $('internet-form').handlers.submit({preventDefault(){}});assert.deepEqual(requests.at(-1).data,{automatic:false,position:false,events:false});assert.equal(requests.at(-1).token,'synthetic-token');
 await $('internet-check').handlers.click();assert.equal(requests.at(-1).data.action,'check');assert.equal($('internet-result').textContent,'');
 raw.internet={state:2,http_status:204,wifi_ip:true,age_s:3,duration_ms:100};n.update(raw);assert.equal($('internet-state').textContent,language==='de'?'Erreichbar':'Reachable');assert.match($('internet-detail').textContent,/100 ms/);assert.equal($('internet-state').dataset.state,'reachable');
 raw.lora.settings.position_enabled=true;n.update(raw);assert($('internet-events').disabled&&!$('internet-position').disabled);
 raw.internet.state=1;raw.internet.age_s=60;n.update(raw);assert.equal($('internet-state').dataset.state,'unavailable');assert($('internet-detail').textContent.includes(language==='de'?'vor einer Minute':'one minute ago'));
 for(let code=0;code<=3;code++){const bytes=Array(24).fill(0);bytes[0]=3;bytes[1]=2|(code<<6);const d=ctx.decodeUplink({fPort:10,bytes});assert(!d.errors);if(!code)assert(!('internet_status' in d.data));else assert.equal(d.data.internet_reachable,code===1?null:code===3);}
 let d=ctx.decodeUplink({fPort:12,bytes:[1,1,0,0,0,0,0,3]});assert.equal(d.data.internet_reachable,false);assert.equal(d.data.wifi_has_ip,false);assert.equal(d.data.probe_failures,3);assert.equal(d.data.reported_at_utc,null);
 assert(ctx.decodeUplink({fPort:12,bytes:[1,3,0,0,0,0,0,0]}).errors);
 const html=fs.readFileSync(web+'index.html','utf8');assert(!html.includes('id="new-profile"'));assert(!html.includes('id="ap-open"'));assert(html.indexOf('id="hotspot-off"')<html.indexOf('id="hotspot-form"'));
}
(async()=>{for(const language of ['de','en'])await run(language);console.log('DE/EN hotspot/Internet: masked prefills, empty/open save, confirmations, mode controls, persistent warning, automatic/report switches, manual checks and compatible packet decoder PASS')})().catch(e=>{console.error(e);process.exitCode=1});
