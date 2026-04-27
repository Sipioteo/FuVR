// SPDX-License-Identifier: Apache-2.0
#![deny(warnings)]

//! FuVR transport-core: codec-agnostic types, traits, and wire helpers.

pub mod channel;
pub mod control;
pub mod fec;
pub mod pacing;
pub mod fuvr_capnp;
pub use fuvr_capnp as proto;
pub mod sequence;
pub mod transport;
pub mod wire;

pub use channel::{Channel, Direction};
pub use fec::{FecConfig, FecEncoder};
pub use pacing::TokenBucket;
pub use sequence::SequenceNumber;
pub use transport::{Transport, TransportError};
pub use wire::FragmentHeader;
