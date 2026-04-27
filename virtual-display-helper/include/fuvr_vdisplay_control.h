// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fuvr_vdisplay_handle fuvr_vdisplay_handle;

fuvr_vdisplay_handle* fuvr_vdisplay_spawn(uint32_t w, uint32_t h, uint32_t hz);
uint32_t              fuvr_vdisplay_id(fuvr_vdisplay_handle*);
void                  fuvr_vdisplay_kill(fuvr_vdisplay_handle*);

#ifdef __cplusplus
}
#endif
