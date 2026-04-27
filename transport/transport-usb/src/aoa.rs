// SPDX-License-Identifier: Apache-2.0

//! AOA (Android Open Accessory) USB fallback — scaffolding only.
//!
//! ADB-reverse over USB is the v1 path (see ADR-0006 and `transport-usb/lib.rs`).
//! AOA is the documented escalation per SPEC §3.1.4: if a user's environment
//! cannot run `adb` (no Android Platform Tools, locked-down corporate Mac,
//! USB permissions blocked, etc.), AOA gives us a vendor-neutral USB pipe
//! that does not depend on adbd.
//!
//! This module is a deliberate stub: every method returns
//! [`AoaError::NotImplemented`]. It exists so that callers can compile
//! against the surface today and the full implementation can land later
//! without an API shape change.
//!
//! The intended implementation will use libusb (via the `rusb` crate) on
//! the Mac side to:
//!   1. Enumerate devices, find one matching the Quest's VID/PID.
//!   2. Issue the AOA control transfers (51/52/53/55/57) to flip the device
//!      into accessory mode (see Android Open Accessory Protocol v2 spec).
//!   3. Re-enumerate to find the accessory-mode endpoints (in/out bulk).
//!   4. Pipe length-delimited frames identical to the ADB-reverse format
//!      (see `wire::encode_usb_frame`) through bulk transfers.
//!
//! The Quest side will use the Android `UsbAccessory` API (Java) and bridge
//! into the C++ transport client through JNI.

use thiserror::Error;
use transport_core::transport::TransportError;

#[derive(Debug, Error)]
pub enum AoaError {
    #[error("AOA transport is not implemented yet (escalation per SPEC §3.1.4)")]
    NotImplemented,
}

impl From<AoaError> for TransportError {
    fn from(e: AoaError) -> Self {
        TransportError::Other(e.to_string())
    }
}

/// AOA USB transport handle. Currently a no-op shell; see module docs.
pub struct AoaTransport {
    _private: (),
}

impl AoaTransport {
    /// Open the first attached Quest in AOA accessory mode. Always returns
    /// [`AoaError::NotImplemented`] until the libusb implementation lands.
    pub fn open() -> Result<Self, AoaError> {
        Err(AoaError::NotImplemented)
    }

    /// Send a single length-delimited frame on the AOA bulk-out endpoint.
    pub fn send_frame(&self, _channel: u8, _payload: &[u8]) -> Result<(), AoaError> {
        Err(AoaError::NotImplemented)
    }

    /// Block until one length-delimited frame arrives on the bulk-in endpoint.
    pub fn recv_frame(&self) -> Result<(u8, Vec<u8>), AoaError> {
        Err(AoaError::NotImplemented)
    }

    /// Close the AOA endpoints and release the USB device.
    pub fn close(self) -> Result<(), AoaError> {
        Err(AoaError::NotImplemented)
    }
}
