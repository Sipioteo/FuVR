// SPDX-License-Identifier: Apache-2.0

//! Wire framing.
//!
//! Video on the wire is `[capnp-encoded VideoFragmentHeader][raw codec bytes]`.
//! The Cap'n Proto header carries `totalSizeBytes`, `fragmentIndex`,
//! `fragmentCount`, codec id, and the rendered pose used by ATW; the codec
//! payload follows immediately after as a length-prefixed binary blob whose
//! length is the remainder of the datagram (UDP) or the announced frame
//! length (USB length-delimited stream). The packed C-style `FragmentHeader`
//! struct below is provided for FFI convenience and bench tooling only — it
//! mirrors the Cap'n Proto layout but is NOT what travels on the wire.

use bytes::{Buf, BufMut, Bytes, BytesMut};

#[derive(Debug, Clone, Copy)]
#[repr(C, packed)]
pub struct FragmentHeader {
    pub frame_id: u64,
    pub render_start_ns: u64,
    pub target_display_time_ns: u64,
    pub total_size_bytes: u32,
    pub fragment_index: u32,
    pub fragment_count: u32,
    pub codec: u16,
    pub flags: u16,
}

impl FragmentHeader {
    pub const SIZE: usize = std::mem::size_of::<Self>();

    pub fn encode(&self, out: &mut BytesMut) {
        out.put_u64_le(self.frame_id);
        out.put_u64_le(self.render_start_ns);
        out.put_u64_le(self.target_display_time_ns);
        out.put_u32_le(self.total_size_bytes);
        out.put_u32_le(self.fragment_index);
        out.put_u32_le(self.fragment_count);
        out.put_u16_le(self.codec);
        out.put_u16_le(self.flags);
    }

    pub fn decode(mut buf: &[u8]) -> Option<Self> {
        if buf.len() < Self::SIZE {
            return None;
        }
        Some(Self {
            frame_id: buf.get_u64_le(),
            render_start_ns: buf.get_u64_le(),
            target_display_time_ns: buf.get_u64_le(),
            total_size_bytes: buf.get_u32_le(),
            fragment_index: buf.get_u32_le(),
            fragment_count: buf.get_u32_le(),
            codec: buf.get_u16_le(),
            flags: buf.get_u16_le(),
        })
    }
}

/// USB length-delimited frame format:
///   [u32 BE total_len][u8 channel_id][payload...]
/// `total_len` covers the channel id + payload (not itself).
pub fn encode_usb_frame(channel_id: u8, payload: &[u8]) -> Bytes {
    let total_len = (payload.len() + 1) as u32;
    let mut out = BytesMut::with_capacity(4 + 1 + payload.len());
    out.put_u32(total_len);
    out.put_u8(channel_id);
    out.extend_from_slice(payload);
    out.freeze()
}
