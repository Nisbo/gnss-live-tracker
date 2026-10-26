'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const source=fs.readFileSync(__dirname+'/../main/web/i18n.js','utf8');
function boot({saved=null,browser='de-DE',blocked=false}={}){
 const storage=new Map(saved===null?[]:[['tracker-language',saved]]),context={navigator:{language:browser},localStorage:{getItem(key){if(blocked)throw Error('blocked');return storage.get(key)||null;},setItem(key,value){if(blocked)throw Error('blocked');storage.set(key,value);}}};
 vm.createContext(context);vm.runInContext(source+';globalThis.i18n=TrackerI18n;',context);return {api:context.i18n,context,storage};
}
assert.equal(boot().api.language(),'en');assert.equal(boot({browser:'en-US'}).api.language(),'en');assert.equal(boot({browser:'fr-FR'}).api.language(),'en');assert.equal(boot({saved:'de',browser:'en-US'}).api.language(),'de');assert.equal(boot({saved:'en',browser:'de-DE'}).api.language(),'en');assert.equal(boot({saved:'unknown',browser:'en-US'}).api.language(),'en');
const {api,storage}=boot({saved:'de'});
api.register('test',{de:{greeting:'Hallo {name}, {count}!',items:{one:'{count} Punkt',other:'{count} Punkte'},missing_in_en:'Nur Deutsch',safe:'<img src=x onerror=alert(1)> {value}'},en:{greeting:'Hello {name}, {count}!',items:{one:'{count} point',other:'{count} points'},fallback:'English fallback',safe:'<img src=x onerror=alert(1)> {value}'}});
assert.equal(api.t('test.greeting',{name:'Mia',count:2}),'Hallo Mia, 2!');assert.equal(api.t('test.items',{count:1}),'1 Punkt');assert.equal(api.t('test.items',{count:2}),'2 Punkte');assert.equal(api.t('test.items',{count:0}),'0 Punkte');assert.equal(api.t('test.greeting',{name:'<script>'}),'Hallo <script>, {count}!');
api.setLanguage('en');assert.equal(api.locale(),'en-GB');assert.equal(storage.get('tracker-language'),'en');assert.equal(api.t('test.items',{count:1}),'1 point');assert.equal(api.t('test.items',{count:2}),'2 points');assert.equal(api.t('test.missing_in_en'),'Nur Deutsch');assert.equal(api.forLanguage('de','test.greeting',{name:'Kai',count:3}),'Hallo Kai, 3!');assert.equal(api.t('test.no_key'),'test.no_key');
api.registerPack({format_version:1,language:'fr',label:'Français',locale:'fr-FR',messages:{'test.greeting':'Bonjour {name}, {count} !','test.items':{one:'{count} point',other:'{count} points'}}});api.setLanguage('fr');assert.equal(api.locale(),'fr-FR');assert.equal(api.t('test.greeting',{name:'Camille',count:1}),'Bonjour Camille, 1 !');assert.equal(api.t('test.fallback'),'English fallback');assert.equal(api.t('test.missing_in_en'),'Nur Deutsch');assert.equal(api.t('core.language'),'Language');assert.equal(api.languages().find(x=>x.code==='fr').label,'Français');
// Every field is validated before a pack mutates the catalog or language list.
const before=JSON.stringify(api.languages());
for(const patch of [
 {format_version:2}, {language:'../../de'}, {label:''}, {locale:'not_a_valid_locale'},
 {messages:{'test.greeting':'BAD','bad key':'invalid'}},
 {messages:{'test.greeting':'BAD','test.invalid':{one:'missing other'}}},
 {messages:{'test.greeting':'BAD','test.invalid':'x'.repeat(8193)}},
 {messages:Object.fromEntries(Array.from({length:5001},(_,i)=>['test.k'+i,'x']))}
]){
 assert.throws(()=>api.registerPack({format_version:1,language:'fr',label:'Français',locale:'fr-FR',messages:{'test.greeting':'BAD'},...patch}));
 assert.equal(api.t('test.greeting',{name:'Camille',count:1}),'Bonjour Camille, 1 !');assert.equal(JSON.stringify(api.languages()),before);
}
// Prototype-like keys are ordinary Map keys; inherited parameters remain inert.
const hostile=JSON.parse('{"custom.__proto__":"inert","custom.constructor":"safe"}');api.registerPack({format_version:1,language:'fr',label:'Français',locale:'fr-FR',messages:hostile});assert.equal(api.t('custom.__proto__'),'inert');assert.equal(api.t('custom.constructor'),'safe');assert.equal({}.polluted,undefined);assert.equal(api.forLanguage('en','test.greeting',Object.create({name:'inherited',count:7})),'Hello {name}, {count}!');assert.throws(()=>api.registerPack({format_version:1,language:'es',label:'Español',locale:'es-ES',messages:JSON.parse('{"__proto__.polluted":"bad"}')}));assert(!api.languages().some(x=>x.code==='es'));
// With browser storage unavailable, selection works for the open page without throwing.
const blockedEnglish=boot({blocked:true,browser:'en-US'});assert.equal(blockedEnglish.api.language(),'en');
const future=boot({saved:'fr',browser:'de-DE'});assert.equal(future.api.language(),'en');future.api.registerPack({format_version:1,language:'fr',label:'Français',locale:'fr-FR',messages:{'core.language':'Langue'}});assert.equal(future.api.language(),'fr');assert.equal(future.api.t('core.language'),'Langue');
const blocked=boot({blocked:true});assert.doesNotThrow(()=>blocked.api.setLanguage('en'));assert.equal(blocked.api.language(),'en');assert.equal(blocked.api.t('core.language'),'Language');assert.throws(()=>blocked.api.setLanguage('missing'));
// apply() changes only explicit translated text/attributes, never parses HTML or changes values.
const node=attributes=>({attrs:{...attributes},textContent:'original',value:'user-entered',innerHTML:'untouched',getAttribute(key){return this.attrs[key]},setAttribute(key,value){this.attrs[key]=value;}});
const text=node({'data-i18n':'test.safe'}),title=node({'data-i18n-title':'test.safe'}),label=node({'data-i18n-aria-label':'core.language'}),placeholder=node({'data-i18n-placeholder':'core.language'});
const groups={'[data-i18n]':[text],'[data-i18n-title]':[title],'[data-i18n-aria-label]':[label],'[data-i18n-placeholder]':[placeholder]},doc={documentElement:{lang:''},querySelectorAll(selector){return groups[selector]||[]}};
api.setLanguage('en');api.apply(doc);assert.equal(doc.documentElement.lang,'en');assert.equal(text.textContent,'<img src=x onerror=alert(1)> {value}');assert.equal(text.innerHTML,'untouched');assert.equal(text.value,'user-entered');assert.equal(title.attrs.title,'<img src=x onerror=alert(1)> {value}');assert.equal(label.attrs['aria-label'],'Language');assert.equal(placeholder.attrs.placeholder,'Language');assert.doesNotThrow(()=>api.apply({}));
api.registerServerErrors({'Text vom Server':'test.fallback'});assert.equal(api.serverMessage('Text vom Server'),'English fallback');assert.equal(api.serverMessage('<!doctype html><html><body>404 Not Found</body></html>'),'404 Not Found');assert.equal(api.serverMessage('unknown error'),'unknown error');
console.log('i18n: initial/saved language, DE/EN plurals and placeholders, offline pack fallback, atomic rejection, prototype safety, unavailable storage and safe text/attribute rendering: PASS');
