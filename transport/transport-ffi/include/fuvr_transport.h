// SPDX-License-Identifier: Apache-2.0
#ifndef FUVR_TRANSPORT_H
#define FUVR_TRANSPORT_H
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FuvrTransportKind {
    FuvrTransportKind_UsbServer = 0,
    FuvrTransportKind_UsbClient = 1,
    FuvrTransportKind_Udp = 2,
} FuvrTransportKind;

typedef enum FuvrChannel {
    FuvrChannel_Video = 0,
    FuvrChannel_Audio = 1,
    FuvrChannel_Pose = 2,
    FuvrChannel_Input = 3,
    FuvrChannel_Haptics = 4,
    FuvrChannel_Control = 5,
} FuvrChannel;

typedef struct FuvrTransport FuvrTransport;

typedef void (*FuvrRecvCallback)(void *user, uint8_t channel, const uint8_t *data, size_t len);

FuvrTransport *fuvr_transport_create(FuvrTransportKind kind, const char *endpoint);
int32_t fuvr_transport_send(FuvrTransport *handle, FuvrChannel channel, const uint8_t *data, size_t len);
void fuvr_transport_set_recv_callback(FuvrTransport *handle, FuvrRecvCallback cb, void *user);
void fuvr_transport_destroy(FuvrTransport *handle);

#ifdef __cplusplus
}
#endif

#endif
