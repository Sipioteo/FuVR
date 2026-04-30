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
    /* UDP over RNDIS USB tethering. The endpoint string is ignored; the
     * transport auto-discovers the host interface in 192.168.42.0/24 and
     * targets the Quest at 192.168.42.129:59000. A 500ms heartbeat is
     * started automatically. */
    FuvrTransportKind_UdpRndis = 3,
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

typedef struct FuvrTransportStats {
    double rtt_ms;
    double loss_pct;
    uint64_t sent_bytes;
    uint64_t recv_bytes;
} FuvrTransportStats;

typedef void (*FuvrRecvCallback)(void *user, uint8_t channel, const uint8_t *data, size_t len);

FuvrTransport *fuvr_transport_create(FuvrTransportKind kind, const char *endpoint);
int32_t fuvr_transport_send(FuvrTransport *handle, FuvrChannel channel, const uint8_t *data, size_t len);
void fuvr_transport_set_recv_callback(FuvrTransport *handle, FuvrRecvCallback cb, void *user);

/* Fill *out with a snapshot of transport diagnostics.
 * Returns 0 on success, -1 if either handle or out is NULL. */
int32_t fuvr_transport_stats(FuvrTransport *handle, FuvrTransportStats *out);

/* Record a single round-trip latency sample in microseconds, and a packet
 * loss fraction in [0.0, 1.0]. Both feed fuvr_transport_stats. */
void fuvr_transport_record_rtt_us(FuvrTransport *handle, uint64_t sample_us);
void fuvr_transport_record_loss(FuvrTransport *handle, double fraction);

void fuvr_transport_destroy(FuvrTransport *handle);

#ifdef __cplusplus
}
#endif

#endif
