#pragma once
#include <stdbool.h>
#include <stdint.h>
/* SX126x image-calibration ranges used by the 863..928 MHz tracker. */
static inline bool sx1262_image_band(uint32_t hz,uint8_t out[2]){
 if(hz>=863000000&&hz<=870000000){out[0]=0xd7;out[1]=0xd8;return true;}
 if(hz>=902000000&&hz<=928000000){out[0]=0xe1;out[1]=0xe9;return true;}
 return false;
}
