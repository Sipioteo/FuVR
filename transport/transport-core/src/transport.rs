// SPDX-License-Identifier: Apache-2.0

use crate::channel::Channel;
use async_trait::async_trait;
use bytes::Bytes;
use futures::stream::BoxStream;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum TransportError {
    #[error("io error: {0}")]
    Io(#[from] std::io::Error),
    #[error("capnp error: {0}")]
    Capnp(#[from] capnp::Error),
    #[error("channel closed")]
    Closed,
    #[error("invalid frame: {0}")]
    InvalidFrame(String),
    #[error("other: {0}")]
    Other(String),
}

pub type Result<T> = std::result::Result<T, TransportError>;

#[async_trait]
pub trait Transport: Send + Sync {
    async fn send_frame(&self, channel: Channel, bytes: Bytes) -> Result<()>;

    fn recv_stream(&self) -> BoxStream<'static, (Channel, Bytes)>;
}
