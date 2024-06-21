#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void PM4P_Print(uint32_t const * pm4, uint32_t pm4_dword_count, bool is_r9xx);

#ifdef __cplusplus
}

void KMTI_Begin();
#endif
