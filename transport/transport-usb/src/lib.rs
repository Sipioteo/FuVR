// SPDX-License-Identifier: Apache-2.0
#![deny(warnings)]

//! ADB-reverse tunnel transport. Mac binds 127.0.0.1:9943; Quest connects
//! through `adb reverse tcp:9943 tcp:9943`.

pub mod aoa;

use async_trait::async_trait;
use bytes::{Buf, Bytes, BytesMut};
use futures::stream::BoxStream;
use std::sync::Arc;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use tokio::process::Command;
use tokio::sync::{mpsc, Mutex};
use tracing::{debug, info, warn};
use transport_core::channel::Channel;
use transport_core::transport::{Result, Transport, TransportError};

pub const DEFAULT_PORT: u16 = 9943;

pub struct UsbServer {
    outgoing: mpsc::Sender<(Channel, Bytes)>,
    incoming_rx: Mutex<Option<mpsc::Receiver<(Channel, Bytes)>>>,
}

impl UsbServer {
    /// Runs `adb reverse tcp:PORT tcp:PORT`, binds the loopback listener, and
    /// returns once the first peer connects (or immediately, depending on
    /// `wait_for_peer`).
    pub async fn bind(port: u16, run_adb_reverse: bool) -> Result<Arc<Self>> {
        if run_adb_reverse {
            spawn_adb_reverse(port).await?;
        }
        let listener = TcpListener::bind(("127.0.0.1", port)).await?;
        info!(port, "transport-usb listening on loopback");

        let (out_tx, out_rx) = mpsc::channel::<(Channel, Bytes)>(1024);
        let (in_tx, in_rx) = mpsc::channel::<(Channel, Bytes)>(1024);

        let server = Arc::new(Self {
            outgoing: out_tx,
            incoming_rx: Mutex::new(Some(in_rx)),
        });

        // Why: the previous implementation moved `out_rx` into the per-peer
        // writer task and called `break` after the first peer disconnected,
        // so once Quest dropped its connection the daemon stopped accepting
        // new peers entirely (and the user had to `launchctl kickstart`).
        //
        // Resilient pattern: the outgoing receiver lives in a single
        // long-running pump task that holds a `Mutex<Option<OwnedWriteHalf>>`
        // for the current peer. The accept loop hot-swaps the writer half
        // when peers come and go. Messages sent while no peer is connected
        // are dropped (the alternative — buffering forever — would build
        // unbounded back-pressure in 1 kHz pose flow).
        let writer_slot: Arc<Mutex<Option<tokio::net::tcp::OwnedWriteHalf>>> =
            Arc::new(Mutex::new(None));
        {
            let writer_slot = writer_slot.clone();
            tokio::spawn(async move {
                let mut out_rx = out_rx;
                while let Some((ch, payload)) = out_rx.recv().await {
                    let frame = transport_core::wire::encode_usb_frame(ch.into(), &payload);
                    let mut guard = writer_slot.lock().await;
                    if let Some(wr) = guard.as_mut() {
                        if let Err(e) = wr.write_all(&frame).await {
                            warn!(error = %e, "write failed; clearing writer slot");
                            *guard = None;
                        }
                    }
                    // No peer → drop frame silently.
                }
            });
        }

        tokio::spawn(async move {
            loop {
                let (sock, peer) = match listener.accept().await {
                    Ok(v) => v,
                    Err(e) => {
                        warn!(error = %e, "accept failed");
                        tokio::time::sleep(std::time::Duration::from_millis(50)).await;
                        continue;
                    }
                };
                info!(%peer, "transport-usb peer connected");
                let (mut rd, wr) = sock.into_split();
                {
                    // Replace the writer slot with this peer's write half.
                    let mut guard = writer_slot.lock().await;
                    *guard = Some(wr);
                }
                let in_tx_c = in_tx.clone();
                let writer_slot_c = writer_slot.clone();
                tokio::spawn(async move {
                    if let Err(e) = read_loop(&mut rd, in_tx_c).await {
                        debug!(error = %e, "read loop ended");
                    }
                    // Reader exited → peer is gone. Clear the writer slot so
                    // outgoing pump stops trying to write to a dead socket.
                    let mut guard = writer_slot_c.lock().await;
                    *guard = None;
                    info!("transport-usb peer disconnected; awaiting next");
                });
                // Continue accepting; the next peer takes over the writer slot.
            }
        });

        Ok(server)
    }
}

pub struct UsbClient {
    outgoing: mpsc::Sender<(Channel, Bytes)>,
    incoming_rx: Mutex<Option<mpsc::Receiver<(Channel, Bytes)>>>,
}

impl UsbClient {
    pub async fn connect(port: u16) -> Result<Arc<Self>> {
        let stream = TcpStream::connect(("127.0.0.1", port)).await?;
        let (out_tx, mut out_rx) = mpsc::channel::<(Channel, Bytes)>(1024);
        let (in_tx, in_rx) = mpsc::channel::<(Channel, Bytes)>(1024);
        let (mut rd, mut wr) = stream.into_split();
        tokio::spawn(async move {
            if let Err(e) = read_loop(&mut rd, in_tx).await {
                debug!(error = %e, "client read loop ended");
            }
        });
        tokio::spawn(async move {
            while let Some((ch, payload)) = out_rx.recv().await {
                let frame = transport_core::wire::encode_usb_frame(ch.into(), &payload);
                if wr.write_all(&frame).await.is_err() {
                    break;
                }
            }
        });
        Ok(Arc::new(Self {
            outgoing: out_tx,
            incoming_rx: Mutex::new(Some(in_rx)),
        }))
    }
}

async fn spawn_adb_reverse(port: u16) -> Result<()> {
    let arg = format!("tcp:{port}");
    let out = Command::new("adb")
        .arg("reverse")
        .arg(&arg)
        .arg(&arg)
        .output()
        .await
        .map_err(|e| TransportError::Other(format!("failed to spawn adb: {e}")))?;
    if !out.status.success() {
        return Err(TransportError::Other(format!(
            "adb reverse failed: {}",
            String::from_utf8_lossy(&out.stderr)
        )));
    }
    Ok(())
}

async fn read_loop<R>(rd: &mut R, tx: mpsc::Sender<(Channel, Bytes)>) -> Result<()>
where
    R: tokio::io::AsyncRead + Unpin,
{
    let mut buf = BytesMut::with_capacity(64 * 1024);
    loop {
        while buf.len() < 4 {
            let n = rd.read_buf(&mut buf).await?;
            if n == 0 {
                return Ok(());
            }
        }
        let total_len = u32::from_be_bytes(buf[..4].try_into().unwrap()) as usize;
        if total_len == 0 || total_len > 16 * 1024 * 1024 {
            return Err(TransportError::InvalidFrame(format!("len={total_len}")));
        }
        while buf.len() < 4 + total_len {
            let n = rd.read_buf(&mut buf).await?;
            if n == 0 {
                return Ok(());
            }
        }
        buf.advance(4);
        let ch_byte = buf[0];
        buf.advance(1);
        let payload = buf.split_to(total_len - 1).freeze();
        let channel = Channel::try_from(ch_byte)
            .map_err(|e| TransportError::InvalidFrame(format!("{e}")))?;
        if tx.send((channel, payload)).await.is_err() {
            return Ok(());
        }
    }
}

#[async_trait]
impl Transport for UsbServer {
    async fn send_frame(&self, channel: Channel, bytes: Bytes) -> Result<()> {
        self.outgoing
            .send((channel, bytes))
            .await
            .map_err(|_| TransportError::Closed)
    }

    fn recv_stream(&self) -> BoxStream<'static, (Channel, Bytes)> {
        let rx = self.incoming_rx.try_lock().ok().and_then(|mut g| g.take());
        match rx {
            Some(rx) => Box::pin(tokio_stream::wrappers::ReceiverStream::new(rx)),
            None => Box::pin(futures::stream::empty()),
        }
    }
}

#[async_trait]
impl Transport for UsbClient {
    async fn send_frame(&self, channel: Channel, bytes: Bytes) -> Result<()> {
        self.outgoing
            .send((channel, bytes))
            .await
            .map_err(|_| TransportError::Closed)
    }

    fn recv_stream(&self) -> BoxStream<'static, (Channel, Bytes)> {
        let rx = self.incoming_rx.try_lock().ok().and_then(|mut g| g.take());
        match rx {
            Some(rx) => Box::pin(tokio_stream::wrappers::ReceiverStream::new(rx)),
            None => Box::pin(futures::stream::empty()),
        }
    }
}
