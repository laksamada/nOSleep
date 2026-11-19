#ifndef _CMOS_H
#define _CMOS_H

#include "header/process/syscall.h"

#define CMOS_ADDRESS_PORT 0x70
#define CMOS_DATA_PORT    0x71

// Baca waktu sekarang dari RTC (CMOS), sudah dikonversi ke biner dan format 24 jam
void cmos_get_time(struct CMOSTime *time);

#endif
