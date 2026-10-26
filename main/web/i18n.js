/* Offline UI translations. Catalogs are served from firmware flash and evaluated in the browser.
 * No text substitution in data, protocol fields, user input, or imported catalogs.
 * A future device-stored pack can use registerPack(); transport/storage is deliberately separate. */
'use strict';
const TrackerI18n=(()=>{
 const dictionaries=new Map(),metadata=new Map([['de',{label:'Deutsch',locale:'de-DE'}],['en',{label:'English',locale:'en-GB'}]]);
 const storageKey='tracker-language';
 let preferred=null;
 let selected='en';
 try{const saved=globalThis.localStorage?.getItem(storageKey);preferred=saved;if(metadata.has(saved))selected=saved;}catch(error){}
 const own=(o,k)=>Object.prototype.hasOwnProperty.call(o,k);
 function validMessage(value){return typeof value==='string'||!!value&&typeof value==='object'&&!Array.isArray(value)&&typeof value.other==='string'&&Object.keys(value).every(k=>['zero','one','two','few','many','other'].includes(k)&&typeof value[k]==='string');}
 function register(namespace,catalogs){
  if(!/^[a-z][a-z0-9_-]*$/.test(namespace))throw new Error('Invalid translation namespace');
  for(const [language,messages]of Object.entries(catalogs)){
   if(!/^[a-z]{2,3}(?:-[A-Z]{2})?$/.test(language)||!messages||typeof messages!=='object'||Array.isArray(messages))throw new Error('Invalid translation catalog');
   for(const [key,message]of Object.entries(messages))if(!/^[a-zA-Z0-9_.-]+$/.test(key)||!validMessage(message))throw new Error('Invalid translation message: '+namespace+'.'+key);
  }
  for(const [language,messages]of Object.entries(catalogs)){if(!dictionaries.has(language))dictionaries.set(language,new Map());const target=dictionaries.get(language);for(const [key,message]of Object.entries(messages))target.set(namespace+'.'+key,message);}
 }
 function forLanguage(language,key,parameters={}){
  const catalog=dictionaries.get(language),english=dictionaries.get('en'),german=dictionaries.get('de');
  let value=catalog?.get(key)??english?.get(key)??german?.get(key)??key;
  if(typeof value==='object'){const number=Number(parameters.count),rule=Number.isFinite(number)?new Intl.PluralRules(metadata.get(language)?.locale||'en-GB').select(number):'other';value=value[rule]??value.other;}
  return value.replace(/\{([a-zA-Z0-9_]+)\}/g,(match,name)=>own(parameters,name)?String(parameters[name]):match);
 }
 function t(key,parameters){return forLanguage(selected,key,parameters);}
 function locale(){return metadata.get(selected)?.locale||'en-GB';}
 function language(){return selected;}
 function languages(){return [...metadata].map(([code,data])=>({code,...data}));}
 function apply(root=globalThis.document){
  if(!root)return;
  if(root.documentElement)root.documentElement.lang=selected;
  if(!root.querySelectorAll)return;
  for(const node of root.querySelectorAll('[data-i18n]'))node.textContent=t(node.getAttribute('data-i18n'));
  for(const name of ['title','aria-label','placeholder'])for(const node of root.querySelectorAll('[data-i18n-'+name+']'))node.setAttribute(name,t(node.getAttribute('data-i18n-'+name)));
 }
 function setLanguage(code){
  if(!metadata.has(code))throw new Error('Unknown UI language');
  selected=code;preferred=code;try{globalThis.localStorage?.setItem(storageKey,code);}catch(error){}
  return selected;
 }
 /* Programmatic extension point only: no upload, fetch, or flash writes here.
  * Validate a complete pack before making any of its messages available. */
 function registerPack(pack){
  if(!pack||pack.format_version!==1||!/^[a-z]{2,3}(?:-[A-Z]{2})?$/.test(pack.language)||!pack.label||typeof pack.label!=='string'||pack.label.length>60||typeof pack.locale!=='string'||!pack.messages||typeof pack.messages!=='object'||Array.isArray(pack.messages))throw new Error('Invalid language pack');
  new Intl.DateTimeFormat(pack.locale);const rows=Object.entries(pack.messages);if(rows.length>5000)throw new Error('Language pack is too large');
  for(const [key,value]of rows)if(!/^[a-z][a-z0-9_-]*\.[a-zA-Z0-9_.-]+$/.test(key)||!validMessage(value)||JSON.stringify(value).length>8192)throw new Error('Invalid language pack message');
  const target=new Map(dictionaries.get(pack.language)||[]);for(const row of rows)target.set(...row);dictionaries.set(pack.language,target);metadata.set(pack.language,{label:pack.label,locale:pack.locale});if(preferred===pack.language)selected=pack.language;
 }
 function serverMessage(value){
  let message=String(value??'');
  if(/^\s*<!doctype|^\s*<html/i.test(message))message=message.replace(/<[^>]*>/g,' ').replace(/\s+/g,' ').trim();
  const entry=serverErrors.get(message);return entry?t(entry):message;
 }
 const serverErrors=new Map();
 function registerServerErrors(entries){for(const [raw,key]of Object.entries(entries))serverErrors.set(raw,key);}
 register('core',{de:{language:'Sprache',wifi:'WLAN',display_language:'Sprache am Tracker',firmware_region:'Firmware {version} · {region}',starting:'Tracker startet …'},en:{language:'Language',wifi:'Wi-Fi',display_language:'Tracker display language',firmware_region:'Firmware {version} · {region}',starting:'Tracker is starting …'}});
 return {register,t,forLanguage,locale,language,languages,apply,setLanguage,registerPack,serverMessage,registerServerErrors};
})();
