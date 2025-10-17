#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t hid_report_desc[];
extern const uint16_t hid_report_desc_size;

typedef struct __attribute__ ((packed)) 
{
    uint8_t report_id;
    uint16_t button;
    uint8_t hat_switch;
    uint8_t x;
    uint8_t y;
    uint8_t z;
    uint8_t rz;
} gamepad_report_t;

#ifdef __cplusplus
}
#endif