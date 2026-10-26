// Identification reference: GSC constellation/SAR tables, checked 2026-10-22.
// GSAT names are catalog mappings, not names transmitted in the NMEA sentence.
// CCSDS identifiers are separate from ranging-code PRN/SVID identifiers.
const galileoCatalog = {
  1:['GSAT0210','26A'],2:['GSAT0211','26B'],3:['GSAT0212','26C'],4:['GSAT0213','26D'],
  5:['GSAT0214','26E'],6:['GSAT0227',null],7:['GSAT0207','267'],8:['GSAT0208','268'],
  9:['GSAT0209','269'],10:['GSAT0224',null],11:['GSAT0101','3A5'],12:['GSAT0102','3A6'],
  13:['GSAT0220','2C0'],14:['GSAT0202','262'],15:['GSAT0221','2C1'],16:['GSAT0232',null],
  18:['GSAT0201','261'],19:['GSAT0103','3A7'],20:['GSAT0104','316'],21:['GSAT0215','2C5'],
  22:['GSAT0204','264'],23:['GSAT0226',null],24:['GSAT0205','265'],25:['GSAT0216','2C6'],
  26:['GSAT0203','263'],27:['GSAT0217','2C7'],28:['GSAT0233',null],29:['GSAT0225',null],
  30:['GSAT0206','266'],31:['GSAT0218','2C8'],32:['GSAT0234',null],33:['GSAT0222','2C2'],
  34:['GSAT0223',null],36:['GSAT0219','2C9']
};
function satelliteLabel(s) {
  const tr=(key,params)=>TrackerI18n.t('satellites.'+key,params);
  const pad=n=>String(n).padStart(2,'0');
  if(s.system==='Galileo'&&s.nmea_id>=1&&s.nmea_id<=36){
    const entry=galileoCatalog[s.nmea_id];
    return {id:'E'+pad(s.nmea_id),name:entry?entry[0]:tr('missing_gsat'),extra:entry&&entry[1]?'CCSDS HEX '+entry[1]+' / DEC '+parseInt(entry[1],16):''};
  }
  if(s.system==='GPS')return {id:'G'+pad(s.nmea_id),name:'PRN '+s.nmea_id,extra:''};
  if(s.system==='GLONASS')return {id:'R'+pad(s.nmea_id>=65&&s.nmea_id<=92?s.nmea_id-64:s.nmea_id),name:tr('glonass_slot'),extra:''};
  if(s.system==='BeiDou')return {id:'C'+pad(s.nmea_id),name:'PRN '+s.nmea_id,extra:''};
  if(s.system==='QZSS')return {id:'J'+pad(s.nmea_id),name:tr('qzss_satellite'),extra:''};
  if(s.system==='NavIC')return {id:'I'+pad(s.nmea_id),name:tr('navic_satellite'),extra:''};
  return {id:'NMEA '+s.nmea_id,name:tr('sbas_system'),extra:''};
}
