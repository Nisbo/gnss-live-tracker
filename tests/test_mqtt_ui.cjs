/* Production MQTT form in both languages; no broker or real credentials. */
const fs=require('fs'),vm=require('vm'),assert=require('assert'),web=__dirname+'/../main/web/';
async function run(language){
 const nodes=new Map(),timers=new Map();let nextTimer=1,fail=false,pause=null,requests=[];
 function element(){return {value:'',type:'password',hidden:false,checked:false,disabled:true,textContent:'',dataset:{},children:[],handlers:{},setAttribute(k,v){this[k]=v},append(...n){this.children.push(...n)},replaceChildren(...n){this.children=n},addEventListener(k,f){this.handlers[k]=f}}}
 const $=id=>{if(!nodes.has(id))nodes.set(id,element());return nodes.get(id)};
 const doc={hidden:false,getElementById:$,createElement:element,handlers:{},addEventListener(k,f){this.handlers[k]=f}};
 const ctx={document:doc,Intl,Date,Math,Number,Map,Set,AbortSignal,localStorage:{getItem:()=>language},navigator:{language},setTimeout(fn){let id=nextTimer++;timers.set(id,fn);return id},clearTimeout(id){timers.delete(id)},fetch:async(url,options)=>{const data=JSON.parse(options.body);requests.push({url,data,token:options.headers['X-Tracker-Token']});if(pause)await pause;if(fail)return {ok:false,text:async()=> 'Could not save'};return {ok:true,json:async()=>data.action?{password:'synthetic-password'}:{ok:true}}}};
 vm.createContext(ctx);for(const file of ['i18n.js','i18n-dashboard.js','i18n-server.js'])vm.runInContext(fs.readFileSync(web+file,'utf8'),ctx);
 let source=fs.readFileSync(web+'dashboard.js','utf8').replace('return {update,refresh,showTab,renderLoraTiming,confirm:confirmAction,selectedSystem:()=>selected,languageChanged};','return {showTab,test:{initMqtt,renderMqtt,loadMqttPassword,renderMqttTopics,setRaw:r=>{rawLatest=r}}};');vm.runInContext(source,ctx);const d=vm.runInContext('TrackerDashboard',ctx);
 const raw={request_token:'synthetic-token',mqtt:{state:'disabled',enabled:false,topic:'tracker/example',client_id:'example_id',host:'broker.example',port:18884,interval_s:10,password_set:true,username:'',discovery:true,commands:false}};
 d.test.initMqtt();d.test.setRaw(raw);d.test.renderMqtt(raw);assert.equal(requests.length,0);assert($('mqtt-save').disabled);
 d.showTab('mqtt');await new Promise(setImmediate);assert.equal($('mqtt-password').value,'synthetic-password');assert.equal($('mqtt-password').type,'password');assert(!$('mqtt-save').disabled);assert.equal(requests[0].data.action,'reveal');assert.equal(requests[0].token,'synthetic-token');
 assert.equal($('mqtt-topics').children.length,5);assert.equal($('mqtt-ha-topics').children.length,7);const paths=parent=>parent.children.map(r=>r.children[1].children[0].textContent);assert(paths($('mqtt-topics')).includes('tracker/example/command_result'));assert(paths($('mqtt-ha-topics')).includes('homeassistant/sensor/example_id/fix/config'));assert(paths($('mqtt-ha-topics')).includes('homeassistant/status'));
 const unchanged=$('mqtt-topics').children[0];d.test.renderMqtt(raw);assert.equal($('mqtt-topics').children[0],unchanged);
 $('mqtt-topic').value='example/changed';$('mqtt-topic').handlers.input();assert(paths($('mqtt-topics')).includes('example/changed/state'));$('mqtt-discovery').checked=false;$('mqtt-discovery').handlers.change();assert($('mqtt-discovery-topics').hidden);
 $('mqtt-reveal').handlers.click();assert.equal($('mqtt-password').type,'text');[...timers.values()].at(-1)();assert.equal($('mqtt-password').type,'password');$('mqtt-reveal').handlers.click();doc.hidden=true;doc.handlers.visibilitychange();assert.equal($('mqtt-password').type,'password');doc.hidden=false;$('mqtt-reveal').handlers.click();d.showTab('firmware');assert.equal($('mqtt-password').type,'password');d.showTab('mqtt');await new Promise(setImmediate);assert.equal(requests.length,1);
 // Saving prefilled, edited or cleared fields sends exactly their value.
 await $('mqtt-form').handlers.submit({preventDefault(){}});assert.equal(requests.at(-1).data.password,'synthetic-password');assert.equal(requests.at(-1).data.port,18884);
 $('mqtt-password').value='edited-password';fail=true;await $('mqtt-form').handlers.submit({preventDefault(){}});assert.equal($('mqtt-password').value,'edited-password');assert.equal($('mqtt-password').type,'password');fail=false;
 $('mqtt-password').value='';await $('mqtt-form').handlers.submit({preventDefault(){}});assert.equal(requests.at(-1).data.password,'');assert(!$('mqtt-save').disabled);
 // Status refresh must not re-enable a pending save.
 let release;pause=new Promise(resolve=>release=resolve);const pending=$('mqtt-form').handlers.submit({preventDefault(){}});d.test.renderMqtt(raw);assert($('mqtt-save').disabled);release();await pending;pause=null;
 const text=$('mqtt-topics').children[0].children[0].textContent;assert.equal(text,language==='de'?'Status und Satelliten':'Status and satellites');
 const html=fs.readFileSync(web+'index.html','utf8');assert(!html.includes('id="mqtt-clear-password"'));assert(!html.includes('ui.mqtt_form_leer_lassen_gespeichertes_behalten'));assert(html.includes('class="mqtt-topics-table"'));
}
(async()=>{for(const language of ['de','en'])await run(language);console.log('MQTT DE/EN: masked prefill, show/hide, no browser storage, preserved/edited/empty saves, failures, save locking, topic rows, discovery topics and stable refresh PASS')})().catch(e=>{console.error(e);process.exitCode=1});
