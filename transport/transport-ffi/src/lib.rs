// SPDX-License-Identifier: Apache-2.0
#![allow(clippy::not_unsafe_ptr_arg_deref)]

//! C ABI for the FuVR transport. Used by the macOS encoder/runtime.
//!
//! All `extern "C"` entry points take raw pointers because that's the
//! shape callers across the FFI boundary speak. We document the safety
//! contract per-function rather than marking every entry `unsafe`, which
//! would be unusable from C++.

use bytes::Bytes;
use futures::StreamExt;
use parking_lot::Mutex;
use std::ffi::{c_char, c_void, CStr};
use std::net::SocketAddr;
use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};
use std::sync::Arc;
use tokio::runtime::Runtime;
use transport_core::channel::Channel;
use transport_core::transport::Transport;
use transport_udp::{UdpConfig, UdpTransport};
use transport_usb::{UsbClient, UsbServer, DEFAULT_PORT};

#[repr(C)]
pub enum FuvrTransportKind {
    UsbServer = 0,
    UsbClient = 1,
    Udp = 2,
}

#[repr(C)]
pub enum FuvrChannel {
    Video = 0,
    Audio = 1,
    Pose = 2,
    Input = 3,
    Haptics = 4,
    Control = 5,
}

/// Snapshot of transport-level diagnostics. Filled by `fuvr_transport_stats`.
///
/// `rtt_ms` is a smoothed estimate maintained from control-channel round
/// trips fed in via `fuvr_transport_record_rtt_us`; it is 0.0 until the
/// first sample lands. `loss_pct` is a rolling estimate set by
/// `fuvr_transport_record_loss`. Both `sent_bytes` and `recv_bytes` are
/// monotonic since the transport was created.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct FuvrTransportStats {
    pub rtt_ms: f64,
    pub loss_pct: f64,
    pub sent_bytes: u64,
    pub recv_bytes: u64,
}

impl From<FuvrChannel> for Channel {
    fn from(c: FuvrChannel) -> Channel {
        match c {
            FuvrChannel::Video => Channel::Video,
            FuvrChannel::Audio => Channel::Audio,
            FuvrChannel::Pose => Channel::Pose,
            FuvrChannel::Input => Channel::Input,
            FuvrChannel::Haptics => Channel::Haptics,
            FuvrChannel::Control => Channel::Control,
        }
    }
}

pub type FuvrRecvCallback = extern "C" fn(
    user: *mut c_void,
    channel: u8,
    data: *const u8,
    len: usize,
);

#[derive(Default)]
struct StatsCounters {
    sent_bytes: AtomicU64,
    recv_bytes: AtomicU64,
    /// RTT EWMA in microseconds; 0 means "no sample yet".
    rtt_us_ewma: AtomicU64,
    /// Loss fraction stored as parts-per-million.
    loss_ppm: AtomicU32,
}

pub struct FuvrTransport {
    rt: Arc<Runtime>,
    inner: Arc<dyn Transport>,
    callback: Mutex<Option<(FuvrRecvCallback, usize)>>,
    stats: Arc<StatsCounters>,
}

unsafe impl Send for FuvrTransport {}
unsafe impl Sync for FuvrTransport {}

#[no_mangle]
pub extern "C" fn fuvr_transport_create(
    kind: FuvrTransportKind,
    endpoint: *const c_char,
) -> *mut FuvrTransport {
    let rt = match Runtime::new() {
        Ok(r) => Arc::new(r),
        Err(_) => return std::ptr::null_mut(),
    };
    let endpoint_str = if endpoint.is_null() {
        ""
    } else {
        match unsafe { CStr::from_ptr(endpoint) }.to_str() {
            Ok(s) => s,
            Err(_) => return std::ptr::null_mut(),
        }
    };

    let result: Result<Arc<dyn Transport>, ()> = rt.block_on(async {
        match kind {
            FuvrTransportKind::UsbServer => {
                let port = endpoint_str.parse().unwrap_or(DEFAULT_PORT);
                UsbServer::bind(port, false)
                    .await
                    .map(|s| s as Arc<dyn Transport>)
                    .map_err(|_| ())
            }
            FuvrTransportKind::UsbClient => {
                let port = endpoint_str.parse().unwrap_or(DEFAULT_PORT);
                UsbClient::connect(port)
                    .await
                    .map(|s| s as Arc<dyn Transport>)
                    .map_err(|_| ())
            }
            FuvrTransportKind::Udp => {
                let addr: SocketAddr = endpoint_str.parse().map_err(|_| ())?;
                UdpTransport::bind(addr, UdpConfig::default())
                    .await
                    .map(|s| s as Arc<dyn Transport>)
                    .map_err(|_| ())
            }
        }
    });
    let inner = match result {
        Ok(t) => t,
        Err(()) => return std::ptr::null_mut(),
    };
    let stats: Arc<StatsCounters> = Arc::new(StatsCounters::default());
    let handle = Box::new(FuvrTransport {
        rt: rt.clone(),
        inner: inner.clone(),
        callback: Mutex::new(None),
        stats: stats.clone(),
    });
    let raw: *mut FuvrTransport = Box::into_raw(handle);
    let raw_addr = raw as usize;

    let cb_inner = inner.clone();
    let stats_rx = stats.clone();
    rt.spawn(async move {
        let mut stream = cb_inner.recv_stream();
        while let Some((ch, bytes)) = stream.next().await {
            stats_rx
                .recv_bytes
                .fetch_add(bytes.len() as u64, Ordering::Relaxed);
            unsafe {
                let h = raw_addr as *const FuvrTransport;
                if h.is_null() {
                    break;
                }
                let cb_opt = *(*h).callback.lock();
                if let Some((cb, user)) = cb_opt {
                    cb(user as *mut c_void, ch.into(), bytes.as_ptr(), bytes.len());
                }
            }
        }
    });
    raw
}

#[no_mangle]
pub extern "C" fn fuvr_transport_send(
    handle: *mut FuvrTransport,
    channel: FuvrChannel,
    data: *const u8,
    len: usize,
) -> i32 {
    if handle.is_null() || (data.is_null() && len > 0) {
        return -1;
    }
    let h = unsafe { &*handle };
    let slice = unsafe { std::slice::from_raw_parts(data, len) };
    let bytes = Bytes::copy_from_slice(slice);
    let inner = h.inner.clone();
    let ch: Channel = channel.into();
    let n = bytes.len() as u64;
    let res = h.rt.block_on(async move { inner.send_frame(ch, bytes).await });
    match res {
        Ok(()) => {
            h.stats.sent_bytes.fetch_add(n, Ordering::Relaxed);
            0
        }
        Err(_) => -2,
    }
}

#[no_mangle]
pub extern "C" fn fuvr_transport_set_recv_callback(
    handle: *mut FuvrTransport,
    cb: FuvrRecvCallback,
    user: *mut c_void,
) {
    if handle.is_null() {
        return;
    }
    let h = unsafe { &*handle };
    *h.callback.lock() = Some((cb, user as usize));
}

/// Fill `out` with a snapshot of transport diagnostics. Returns 0 on success
/// and -1 if `handle` or `out` is null.
#[no_mangle]
pub extern "C" fn fuvr_transport_stats(
    handle: *mut FuvrTransport,
    out: *mut FuvrTransportStats,
) -> i32 {
    if handle.is_null() || out.is_null() {
        return -1;
    }
    let h = unsafe { &*handle };
    let rtt_us = h.stats.rtt_us_ewma.load(Ordering::Relaxed);
    let loss_ppm = h.stats.loss_ppm.load(Ordering::Relaxed);
    let snap = FuvrTransportStats {
        rtt_ms: (rtt_us as f64) / 1000.0,
        loss_pct: (loss_ppm as f64) / 10_000.0,
        sent_bytes: h.stats.sent_bytes.load(Ordering::Relaxed),
        recv_bytes: h.stats.recv_bytes.load(Ordering::Relaxed),
    };
    unsafe { *out = snap };
    0
}

/// Record a single round-trip latency sample (microseconds). The daemon's
/// control-channel ping/pong calls this so `fuvr_transport_stats` can
/// surface a smoothed RTT.
#[no_mangle]
pub extern "C" fn fuvr_transport_record_rtt_us(handle: *mut FuvrTransport, sample_us: u64) {
    if handle.is_null() {
        return;
    }
    let h = unsafe { &*handle };
    let prev = h.stats.rtt_us_ewma.load(Ordering::Relaxed);
    let next = if prev == 0 {
        sample_us
    } else {
        prev - prev / 8 + sample_us / 8
    };
    h.stats.rtt_us_ewma.store(next, Ordering::Relaxed);
}

/// Record an observed loss fraction in the 0.0..=1.0 range.
#[no_mangle]
pub extern "C" fn fuvr_transport_record_loss(handle: *mut FuvrTransport, fraction: f64) {
    if handle.is_null() {
        return;
    }
    let clamped = fraction.clamp(0.0, 1.0);
    let ppm = (clamped * 1_000_000.0) as u32;
    let h = unsafe { &*handle };
    h.stats.loss_ppm.store(ppm, Ordering::Relaxed);
}

#[no_mangle]
pub extern "C" fn fuvr_transport_destroy(handle: *mut FuvrTransport) {
    if handle.is_null() {
        return;
    }
    unsafe { drop(Box::from_raw(handle)) };
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;

    extern "C" fn noop_cb(_user: *mut c_void, _ch: u8, _data: *const u8, _len: usize) {}

    #[test]
    fn stats_counters_move_on_send() {
        let ep = CString::new("127.0.0.1:0").unwrap();
        let h = fuvr_transport_create(FuvrTransportKind::Udp, ep.as_ptr());
        assert!(!h.is_null());
        fuvr_transport_set_recv_callback(h, noop_cb, std::ptr::null_mut());

        let payload = [0u8; 64];
        let mut sent_ok = 0u64;
        for _ in 0..8 {
            if fuvr_transport_send(
                h,
                FuvrChannel::Control,
                payload.as_ptr(),
                payload.len(),
            ) == 0
            {
                sent_ok += payload.len() as u64;
            }
        }
        fuvr_transport_record_rtt_us(h, 4_000);
        fuvr_transport_record_loss(h, 0.025);

        let mut stats = FuvrTransportStats::default();
        let rc = fuvr_transport_stats(h, &mut stats as *mut _);
        assert_eq!(rc, 0);
        assert_eq!(stats.sent_bytes, sent_ok);
        assert!((stats.rtt_ms - 4.0).abs() < 1e-6);
        assert!((stats.loss_pct - 2.5).abs() < 1e-3);

        fuvr_transport_destroy(h);
    }

    #[test]
    fn stats_rejects_null() {
        let rc = fuvr_transport_stats(std::ptr::null_mut(), std::ptr::null_mut());
        assert_eq!(rc, -1);
    }
}
