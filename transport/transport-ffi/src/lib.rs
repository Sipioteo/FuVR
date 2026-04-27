// SPDX-License-Identifier: Apache-2.0

//! C ABI for the FuVR transport. Used by the macOS encoder/runtime.

use bytes::Bytes;
use futures::StreamExt;
use parking_lot::Mutex;
use std::ffi::{c_char, c_void, CStr};
use std::net::SocketAddr;
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

pub struct FuvrTransport {
    rt: Arc<Runtime>,
    inner: Arc<dyn Transport>,
    callback: Mutex<Option<(FuvrRecvCallback, usize)>>,
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
    let handle = Box::new(FuvrTransport {
        rt: rt.clone(),
        inner: inner.clone(),
        callback: Mutex::new(None),
    });
    let raw: *mut FuvrTransport = Box::into_raw(handle);
    let raw_addr = raw as usize;

    let cb_inner = inner.clone();
    rt.spawn(async move {
        let mut stream = cb_inner.recv_stream();
        while let Some((ch, bytes)) = stream.next().await {
            unsafe {
                let h = raw_addr as *const FuvrTransport;
                if h.is_null() {
                    break;
                }
                let cb_opt = (*h).callback.lock().clone();
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
    let res = h.rt.block_on(async move { inner.send_frame(ch, bytes).await });
    match res {
        Ok(()) => 0,
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

#[no_mangle]
pub extern "C" fn fuvr_transport_destroy(handle: *mut FuvrTransport) {
    if handle.is_null() {
        return;
    }
    unsafe { drop(Box::from_raw(handle)) };
}
