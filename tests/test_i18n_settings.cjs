/* Exercise real module output in both languages, without touching persisted device data. */
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const web=__dirname+'/../main/web/';
const catalogue=JSON.parse(fs.readFileSync(web+'locales/ui.json','utf8'));
assert(Object.keys(catalogue.de).length>=493);
assert.deepEqual(Object.keys(catalogue.de),Object.keys(catalogue.en));
function context(language){
 const nodes=new Map();
 function element(id=''){return {id,value:'',type:'password',checked:false,hidden:false,disabled:false,children:[],style:{},dataset:{},handlers:{},parentElement:{dataset:{}},textContent:'',append(...children){this.children.push(...children);},replaceChildren(...children){this.children=[...children];},setAttribute(k,v){this[k]=v;},addEventListener(k,f){this.handlers[k]=f;},querySelectorAll(){return [];}};}
 const $=id=>{if(!nodes.has(id))nodes.set(id,element(id));return nodes.get(id);};
 const ctx={document:{getElementById:$,createElement:()=>element(),querySelectorAll:()=>[]},localStorage:{getItem:()=>language,setItem(){}},navigator:{language:language==='de'?'de-DE':'en-GB'},Intl,Date,TextDecoder,TextEncoder,Uint8Array,Uint8ClampedArray,DataView,URL,URLSearchParams,AbortSignal,AbortController,setTimeout,clearTimeout};
 vm.createContext(ctx);
 for(const file of ['i18n.js','i18n-settings.js','i18n-ui.js','i18n-server.js','timezones.js','firmware.js','display.js','credentials.js','backup.js','adr.js']){
  let source=fs.readFileSync(web+file,'utf8');
  if(file==='firmware.js')source=source.replace('return {init,update,languageChanged};','return {init,update,test:{descriptor}};');
  if(file==='backup.js')source=source.replace('return {init,update,languageChanged};','return {init,update,test:{parse}};');
  if(file==='adr.js')source=source.replace('return {update,languageChanged};','return {update,test:{description}};');
  vm.runInContext(source,ctx,{filename:file});
 }
 return {ctx,$,run:code=>vm.runInContext(code,ctx)};
}
for(const language of ['de','en']){
 const {ctx,$,run}=context(language);
 assert.equal(run('TrackerI18n.language()'),language);
 const wireErrors=new Set();
 // Parse balanced calls instead of assuming the final argument is a bare literal.
 // Both branches of conditional restore errors must be translated as well.
 for(const file of fs.readdirSync(web+'..').filter(n=>n.endsWith('.c'))){
  const code=fs.readFileSync(web+'../'+file,'utf8'),calls=file==='tracker_ota.c'?/\b(?:httpd_resp_send_err|fail)\(/g:/\bhttpd_resp_send_err\(/g;
  for(const call of code.matchAll(calls)){
   let depth=1,quote=false,escaped=false,end=call.index+call[0].length;
   for(;end<code.length&&depth;end++){
    const c=code[end];if(quote){if(escaped)escaped=false;else if(c==='\\')escaped=true;else if(c==='"')quote=false;}
    else if(c==='"')quote=true;else if(c==='(')depth++;else if(c===')')depth--;
   }
   const args=code.slice(call.index+call[0].length,end-1);
   for(const literal of args.matchAll(/"((?:[^"\\]|\\.)*)"/g))wireErrors.add(JSON.parse('"'+literal[1]+'"'));
  }
 }
 assert(wireErrors.has('Invalid restore action'));assert(wireErrors.has('Restore interrupted; keep the file and resume'));
 wireErrors.add('Display preview busy');wireErrors.add('Display preview starting');assert(wireErrors.size>=70);
 for(const error of wireErrors){const rendered=run(`TrackerI18n.serverMessage(${JSON.stringify(error)})`);assert(rendered&&!rendered.startsWith('server.'));if(language==='de')assert.notEqual(rendered,error,'Untranslated wire error: '+error);}
 assert.equal(run("TrackerI18n.serverMessage('Could not save WLAN settings')"),language==='de'?'Die WLAN-Einstellungen konnten nicht gespeichert werden.':'Could not save the Wi-Fi settings.');
 assert.equal(run("TrackerI18n.serverMessage('Unknown future backend error')"),'Unknown future backend error');
 for(const [key,text]of Object.entries(catalogue[language]))assert.equal(run(`TrackerI18n.t(${JSON.stringify('ui.'+key)})`),text);
 assert.throws(()=>run('TrackerFirmware.test.descriptor(new Uint8Array(8),8,2097152)'),language==='de'?/maximal/:/maximum/);
 assert.throws(()=>run('TrackerBackup.test.parse(new ArrayBuffer(0))'),language==='de'?/Dateigröße/:/file size/);
 const fallback=run("TrackerAdr.test.description({source:'fallback'})");assert.match(fallback,language==='de'?/ausbleibender Downlinks/:/no downlinks were received/);
 run("TrackerCredentials.update({lora:{credentials:{configured:true,app_key_set:true}}})");assert.equal($('otaa-state').textContent,language==='de'?'Zugangsdaten gespeichert':'Credentials saved');assert.equal($('otaa-reveal').textContent,language==='de'?'Gespeicherten AppKey einblenden':'Show saved AppKey');
 const select=$('zones');ctx.select=select;run("TrackerTimezones.fill(select,'UTC')");assert.equal(select.children[0].textContent,language==='de'?'Automatisch · dieses Gerät':'Automatic · this device');assert(select.children.some(v=>v.textContent.startsWith(language==='de'?'Europa ·':'Europe ·')));
 run('TrackerDisplay.update({display:{mode:"on",on:true,language:1,page:0,brightness:100,brightness_supported:true}})');assert.equal($('display-language').value,'1');assert.equal($('display-state').textContent,language==='de'?'Display an':'Display on');assert.equal($('display-cycle').children[0].children[0].children[1].textContent,language==='de'?'Übersicht':'Overview');
 assert.equal(run("TrackerI18n.t('display.pages_selected_count',{count:1})"),language==='de'?'1 Seite ausgewählt.':'1 page selected.');assert.equal(run("TrackerI18n.t('display.pages_selected_count',{count:2})"),language==='de'?'2 Seiten ausgewählt.':'2 pages selected.');
}
console.log('DE/EN settings: '+Object.keys(catalogue.de).length+' static UI keys, real firmware/backup errors, ADR diagnostics, credential labels, timezone lists, independent display language, plural forms and exact server-error coverage: PASS');
