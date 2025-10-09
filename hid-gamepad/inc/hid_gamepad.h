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
    // uint8_t x;
    // uint8_t y;
    // uint8_t z;
    // uint8_t rx;
    // uint8_t ry;
    // uint8_t rz;
    uint8_t hat_switch;
    uint32_t button;
} gamepad_report_t;

#ifdef __cplusplus
}
#endif