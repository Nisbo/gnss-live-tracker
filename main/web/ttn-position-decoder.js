// Tracker V2: FPort 10 position/status v1–v7; FPort 11 satellite inventory v1.
// ES5-compatible: copy this entire file into the TTN uplink formatter.
function decodeUplink(input) {
  var b = input.bytes || [];
  var systems = ["unknown", "GPS", "GLONASS", "Galileo", "BeiDou", "QZSS", "SBAS", "NavIC"];
  function u16(o) { return b[o] * 256 + b[o + 1]; }
  function i32(o) { return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]; }
  function optional(n, scale) { return n === 65535 ? null : n / scale; }
  if (input.fPort === 11) {
    if (b.length < 7 || b[0] !== 1 || b.length !== 7 + b[6] * 4 ||
        b[3] < 1 || b[3] > b[4]) return { errors: ["Invalid satellite inventory"] };
    var satellites = [];
    for (var i = 0; i < b[6]; i++) {
      var o = 7 + i * 4, system = b[o] & 15;
      if (system < 1 || system > 7) return { errors: ["Invalid satellite system"] };
      satellites.push({
        system: systems[system],
        nmea_id: u16(o + 1),
        visible: (b[o] & 64) !== 0,
        used_in_fix: (b[o] & 32) !== 0 ? (b[o] & 128) !== 0 : null,
        cn0_db_hz: b[o + 3] === 255 ? null : b[o + 3]
      });
    }
    return { data: {
      message_type: "satellites", format_version: 1,
      position_sequence: u16(1), part: b[3], parts: b[4],
      satellites_total: b[5], satellites: satellites
    } };
  }
  if (input.fPort !== 10) return { errors: ["Expected FPort 10 or 11"] };
  if ((b[0] & 0xF0) === 0x60 || (b[0] & 0xF0) === 0x70) {
    var small = (b[0] & 0xF0) === 0x60, flags = b[0] & 15;
    if (b.length !== (small ? 11 : 9) || (flags & 1)) return {errors:["Invalid compact position"]};
    var latS=i32(1)/1e7,lonS=i32(5)/1e7,fixS=(flags&2)!==0;
    if(fixS&&(Math.abs(latS)>90||Math.abs(lonS)>180))return {errors:["Coordinates out of range"]};
    var dataS={message_type:"position",format_version:small?6:7,compact:true,simulated:false,gnss_fix:fixS,gnss_data_recent:(flags&4)!==0,latitude:fixS?latS:null,longitude:fixS?lonS:null,satellite_list_truncated:(flags&8)!==0};
    if(small){dataS.satellites_used=b[9]===255?null:b[9];dataS.satellites_visible=b[10]===255?null:b[10];}
    return {data:dataS};
  }
  if (b[0] === 3 || b[0] === 4 || b[0] === 5) {
    var expected = b[0] === 4 ? 23 : b[0] === 5 ? 38 : 24;
    if (b.length !== expected) return {errors: ["Invalid position packet length"]};
    var fix3 = (b[1] & 2) !== 0, lat3 = i32(2) / 1e7, lon3 = i32(6) / 1e7;
    if (fix3 && (Math.abs(lat3) > 90 || Math.abs(lon3) > 180)) return {errors: ["Coordinates out of range"]};
    var alt3 = u16(10); if (alt3 >= 32768) alt3 -= 65536;
    var utc3 = b[19] * 16777216 + b[20] * 65536 + b[21] * 256 + b[22], used3 = [];
    for (var k3 = 1; k3 <= 7; k3++) if (b[23] & (1 << (k3 - 1))) used3.push(systems[k3]);
    var data3 = {message_type: "position", format_version: b[0], simulated: false,
      gnss_fix: fix3, gnss_data_recent: (b[1] & 4) !== 0,
      latitude: fix3 ? lat3 : null, longitude: fix3 ? lon3 : null,
      altitude: fix3 && alt3 !== -32768 ? alt3 : null, sequence: u16(12),
      satellites_used: b[14] === 255 ? null : b[14], satellites_visible: b[15] === 255 ? null : b[15],
      hdop: b[16] === 255 ? null : b[16] / 10, hdop_is_lower_bound: (b[1] & 16) !== 0,
      speed_kmh: u16(17) === 65535 ? null : u16(17) * 0.036,
      measurement_utc: utc3 ? new Date(utc3 * 1000).toISOString() : null,
      measurement_unix_s: utc3 || null, satellite_list_truncated: (b[1] & 8) !== 0};
    if(b[0] !== 4){data3.systems_used=used3;data3.systems_usage_known=(b[1]&32)!==0;}
    if(b[0] === 5){
      data3.system_counts=[];
      for(var j3=0;j3<7;j3++)data3.system_counts.push({system:systems[j3+1],satellites_visible:b[24+2*j3]===255?null:b[24+2*j3],satellites_used:b[25+2*j3]===255?null:b[25+2*j3]});
    }
    return {data:data3};
  }
  if (!((b[0] === 1 && b.length === 14) || (b[0] === 2 && b.length === 39))) {
    return { errors: ["Expected position v1 (14 bytes) or v2 (39 bytes)"] };
  }
  var simulated = (b[1] & 1) !== 0, fix = (b[1] & 2) !== 0;
  var hasPosition = fix || simulated;
  var lat = i32(2) / 10000000, lon = i32(6) / 10000000;
  if (hasPosition && (Math.abs(lat) > 90 || Math.abs(lon) > 180)) {
    return { errors: ["Coordinates out of range"] };
  }
  var alt = u16(10); if (alt >= 32768) alt -= 65536;
  var data = {
    message_type: "position", format_version: b[0],
    latitude: hasPosition ? lat : null,
    longitude: hasPosition ? lon : null,
    altitude: hasPosition && alt !== -32768 ? alt : null,
    simulated: simulated, gnss_fix: fix, sequence: u16(12)
  };
  if (b[0] === 2) {
    data.gnss_data_recent = (b[1] & 4) !== 0;
    data.satellite_list_truncated = (b[1] & 8) !== 0;
    data.satellites_used = b[14] === 255 ? null : b[14];
    data.satellites_visible = b[15] === 255 ? null : b[15];
    data.hdop = optional(u16(16), 100);
    data.last_fix_age_s = optional(u16(18), 1);
    data.speed_kmh = u16(20) === 65535 ? null : u16(20) * 0.036;
    data.course_deg = optional(u16(22), 100);
    data.fix_quality = b[24] === 255 ? null : b[24];
    data.uptime_s = b[26] * 16777216 + b[27] * 65536 + b[28] * 256 + b[29];
    data.satellites_visible_by_system = {};
    data.systems_used = [];
    data.systems_with_usage_report = [];
    for (var s = 1; s <= 7; s++) {
      var mask = 1 << (s - 1);
      data.satellites_visible_by_system[systems[s]] = (b[38] & mask) !== 0 ? b[29 + s] : null;
      if ((b[25] & mask) !== 0) data.systems_used.push(systems[s]);
      if ((b[37] & mask) !== 0) data.systems_with_usage_report.push(systems[s]);
    }
    data.galileo_visible = (b[38] & 4) !== 0 ? b[32] : null;
    data.galileo_used = (b[37] & 4) !== 0 ? (b[25] & 4) !== 0 : null;
  }
  return { data: data };
}
