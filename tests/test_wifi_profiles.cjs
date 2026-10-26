/* Test profile editing and connection confirmation without real credentials or networking. */
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const web=__dirname+'/../main/web/',html=fs.readFileSync(web+'index.html','utf8');
function element(tag=''){return {tag,type:'',value:'',checked:false,disabled:false,isConnected:true,children:[],dataset:{},handlers:{},textContent:'',append(...v){this.children.push(...v)},replaceChildren(...v){this.children=v},setAttribute(k,v){this[k]=v},addEventListener(k,v){this.handlers[k]=v}};}
const elements=new Map(),$=id=>{if(!elements.has(id))elements.set(id,element(id));return elements.get(id)};
const document={hidden:false,createElement:element,addEventListener(){},getElementById:$};
let allow=true,failReveal=false,deferredReveal=null;const actions=[],dialogs=[],timers=new Map();let timerId=0;
const profile={id:0,ssid:'Example Wi-Fi',secure:true},other={id:1,ssid:'Other Wi-Fi',secure:false};
const ctx={document,Map,Set,Intl,localStorage:{getItem:()=> 'de',setItem(){}},navigator:{language:'de'},byId:$,text:(id,s)=>{$(id).textContent=s},profilesSignature:'',activeTab:'wifi',lastRaw:{wifi:{profiles:[profile,other],current_profile:0,state:'connected'}},setTimeout:fn=>{timers.set(++timerId,fn);return timerId},clearTimeout:id=>timers.delete(id),TrackerDashboard:{confirm:async(...args)=>{dialogs.push(args);return allow}},wifiAction:async data=>{actions.push({...data});if(data.action==='reveal'){if(failReveal)throw Error('unavailable');if(deferredReveal)return deferredReveal;return {password:'example-password'}}return {ok:true}}};
vm.createContext(ctx);for(const f of ['i18n.js','i18n-inline.js'])vm.runInContext(fs.readFileSync(web+f,'utf8'),ctx);
vm.runInContext("const tr=(k,p)=>TrackerI18n.t('inline.'+k,p);",ctx);
const section=html.slice(html.indexOf('function newProfile()'),html.indexOf("function showTab(name)"));
vm.runInContext(section+';this.ui={renderProfiles,closeWifiEditor,refreshWifiProfiles,editors:wifiEditors};',ctx);
const walk=n=>[n,...n.children.flatMap(walk)],find=(n,fn)=>walk(n).find(fn),row=id=>$('profiles').children.find(n=>n.dataset.profileId===String(id)),button=(r,label)=>find(r,n=>n.tag==='button'&&n.textContent===label),form=id=>find(row(id),n=>n.tag==='form'),input=(f,label)=>find(f,n=>n.tag==='label'&&n.textContent===label).children.find(n=>n.tag==='input'),password=f=>find(f,n=>n.tag==='input'&&n.type==='password'),submit=f=>f.handlers.submit({preventDefault(){}});
async function edit(id){await button(row(id),'Bearbeiten').handlers.click();return form(id)}
(async()=>{
 ctx.ui.renderProfiles(ctx.lastRaw.wifi);
 assert(button(row(0),'Neu verbinden'));assert(button(row(1),'Verbinden'));
 allow=false;await button(row(0),'Neu verbinden').handlers.click();assert.equal(actions.length,0);assert.match(dialogs.at(-1)[1],/unterbrochen/);assert.equal(dialogs.at(-1)[3],false);
 allow=true;await button(row(0),'Neu verbinden').handlers.click();assert.deepEqual(actions.at(-1),{action:'connect',id:0});
 let f=await edit(0),p=password(f);assert.equal(p.value,'example-password');assert.equal(p.type,'password');assert.equal($('ssid').value,'');
 const eye=button(f,'Passwort anzeigen');eye.onclick();assert.equal(p.type,'text');for(const fn of [...timers.values()])fn();assert.equal(p.type,'password');assert.equal(p.value,'example-password');
 // A language change repaints the editor, preserving the draft without another credential request.
 const count=actions.length;input(f,'WLAN-Name (SSID)').value='Edited Wi-Fi';input(f,'WLAN-Name (SSID)').handlers.input();vm.runInContext("TrackerI18n.setLanguage('en')",ctx);ctx.ui.refreshWifiProfiles();f=form(0);assert.equal(password(f).value,'example-password');assert.equal(input(f,'Wi-Fi name (SSID)').value,'Edited Wi-Fi');assert.equal(actions.length,count);button(f,'Cancel').handlers.click();assert(!form(0));assert.equal(ctx.ui.editors.size,0);
 vm.runInContext("TrackerI18n.setLanguage('de')",ctx);ctx.ui.refreshWifiProfiles();
 // Unchanged saves always send the real password, with no reconnect and no confirmation.
 f=await edit(0);const dc=dialogs.length;await submit(f);assert.equal(dialogs.length,dc);assert.deepEqual(actions.at(-1),{action:'save',id:0,ssid:'Example Wi-Fi',password:'example-password',connect:false});assert.equal(ctx.ui.editors.size,0);
 // Clearing the active password needs confirmation; cancelling leaves everything unchanged.
 f=await edit(0);p=password(f);p.value='';p.handlers.input();allow=false;const ac=actions.length;await submit(f);assert.equal(actions.length,ac);assert(form(0));allow=true;await submit(f);assert.deepEqual(actions.at(-1),{action:'save',id:0,ssid:'Example Wi-Fi',password:'',connect:true});
 // Editing an open, inactive profile never loads a password and never switches networks.
 const before=actions.length;f=await edit(1);assert.equal(actions.length,before);assert.equal(password(f).value,'');password(f).value='another-password';password(f).handlers.input();await submit(f);assert.deepEqual(actions.at(-1),{action:'save',id:1,ssid:'Other Wi-Fi',password:'another-password',connect:false});
 // Loading failures cannot produce an empty-password save.
 failReveal=true;f=await edit(0);assert(!find(f,n=>n.tag==='button'&&n.type==='submit'));button(f,'Abbrechen').handlers.click();failReveal=false;
 // Adding an open profile is explicit through its empty password, without a second checkbox.
 $('ssid').value='New open network';$('password').value='';$('connect-now').checked=false;await $('wifi-form').handlers.submit({preventDefault(){}});assert.deepEqual(actions.at(-1),{action:'save',id:-1,ssid:'New open network',password:'',connect:false});
 $('ssid').value='Example Wi-Fi';const beforeDuplicate=actions.length;await $('wifi-form').handlers.submit({preventDefault(){}});assert.equal(actions.length,beforeDuplicate);assert.match($('setup-result').textContent,/bereits/);
 assert(!html.includes('id="profile-open"'));assert(!section.includes('scrollIntoView'));assert(!html.includes('id="profile-id"'));
 console.log('Wi-Fi profiles: inline edits, real masked passwords, DE/EN drafts, unchanged/cleared passwords, active save/reconnect confirmations, cancellation, open profiles, loading failure and duplicate protection PASS');
})().catch(e=>{console.error(e);process.exitCode=1});
