/* Shared browser/display timezone selection. Tracker offsets live in flash. */
const TrackerTimezones=(()=>{
 const tr=(key,params)=>TrackerI18n.t('timezones.'+key,params);
 const names=TZ_NAMES;
 const known=new Set(names);
 function resolve(value){return value==='auto'?Intl.DateTimeFormat().resolvedOptions().timeZone:value;}
 function fill(select,value='auto'){
 const regions={Europe:tr('europe'),America:tr('america'),Asia:tr('asia'),Africa:tr('africa'),Australia:tr('australia'),Pacific:tr('pacific'),Atlantic:tr('atlantic'),Indian:tr('indian'),Antarctica:tr('antarctica'),Arctic:tr('arctic')};
  let zones=names;try{if(Intl.supportedValuesOf)zones=Intl.supportedValuesOf('timeZone').filter(n=>known.has(n));}catch(error){}
  if(value!=='auto'&&value!=='UTC'&&known.has(value)&&!zones.includes(value))zones=[...zones,value].sort();
  select.replaceChildren();for(const [id,label]of [['auto',tr('automatic')],['UTC',tr('utc')]]){const option=document.createElement('option');option.value=id;option.textContent=label;select.append(option);}
  for(const name of zones){if(name==='UTC')continue;let label=name.split('/').map((v,i)=>i===0?(regions[v]||v):v.replaceAll('_',' ')).join(' · ');try{label+=' ('+new Intl.DateTimeFormat(TrackerI18n.locale(),{timeZone:name,timeZoneName:'shortOffset'}).formatToParts(new Date()).find(p=>p.type==='timeZoneName').value+')';}catch(error){}const option=document.createElement('option');option.value=name;option.textContent=label;select.append(option);}
  select.value=value;return !!select.value;
 }
 return {fill,resolve};
})();
