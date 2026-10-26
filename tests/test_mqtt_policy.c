#include "mqtt_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 assert(mqtt_settings_valid("", "tracker/test",1883,10,false));
 assert(!mqtt_settings_valid("", "tracker/test",1883,10,true));
 assert(mqtt_settings_valid("broker.local","tracker/test",65535,5,true));
 assert(mqtt_settings_valid("192.168.1.1","tracker/test",18884,3600,true));
 assert(!mqtt_settings_valid("mqtt://user@host","tracker/test",1883,10,true));
 assert(!mqtt_settings_valid("host","tracker/#",1883,10,true));
 assert(!mqtt_settings_valid("host","tracker//test",1883,10,true));
 assert(!mqtt_settings_valid("host","tracker/test/",1883,10,true));
 assert(!mqtt_settings_valid("host","tracker/test",0,10,true));
 int on,interval;assert(mqtt_command_parse("{\"recording\":false}",&on,&interval)&&on==0&&interval==0);
 assert(mqtt_command_parse("{\"interval_s\":30}",&on,&interval)&&on==-1&&interval==30);
 assert(mqtt_command_parse("{\"recording\":true,\"interval_s\":5}",&on,&interval)&&on==1&&interval==5);
 assert(!mqtt_command_parse("{\"interval_s\":4}",&on,&interval));
 assert(!mqtt_command_parse("{\"interval_s\":5.5}",&on,&interval));
 assert(!mqtt_command_parse("{\"recording\":1}",&on,&interval));
 assert(!mqtt_command_parse("{\"clear\":true}",&on,&interval));
 assert(!mqtt_command_parse("{\"recording\":true,\"clear\":true}",&on,&interval));
 assert(!mqtt_command_parse("{}",&on,&interval));
 puts("MQTT policy: optional credentials, custom ports, topic validation, command allowlist/ranges passed");
}
