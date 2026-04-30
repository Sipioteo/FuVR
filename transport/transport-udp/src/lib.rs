// SPDX-License-Identifier: Apache-2.0
#![deny(warnings)]

//! UDP datagram transport with MTU-aware fragmentation, FEC, and
//! timeout-based reassembly. ARQ-free.

pub mod jitter;
pub mod tethering;

use async_trait::async_trait;
use bytes::{Buf, BufMut, Bytes, BytesMut};
use futures::stream::BoxStream;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::net::UdpSocket;
use tokio::sync::mpsc;
use tracing::{debug, warn};
use transport_core::channel::Channel;
use transport_core::fec::{FecConfig, FecEncoder};
use transport_core::pacing::TokenBucket;
use transport_core::transport::{Result, Transport, TransportError};

/// Max payload bytes per UDP datagram (excluding IP/UDP headers + our header).
///
/// Tuned for RNDIS/NCM USB tethering: the virtual link advertises a 1500-byte
/// MTU but the RNDIS encapsulation eats ~44 bytes for the BTH+ETH frame; 1450
/// keeps every shard one cable transaction with margin for IP/UDP headers
/// (28 B) and our `PktHeader` (25 B).
pub const DEFAULT_MTU_PAYLOAD: usize = 1450;

/// Default UDP port for the dedicated RNDIS VR stream.
pub const DEFAULT_RNDIS_PORT: u16 = 59000;

const REASSEMBLY_TIMEOUT: Duration = Duration::from_millis(200);

/// Datagram header (little-endian on the wire):
///   u8  channel
///   u64 seq        (per-frame id)
///   u32 shard_idx  (0..data+parity)
///   u32 shard_count
///   u16 data_shards
///   u16 original_len_lo16  -- actually the low 16 bits; full length can be
///   we use a 4-byte total below; redo:
///
/// Realized layout we actually emit:
///   u8  channel
///   u64 seq
///   u32 shard_idx
///   u32 shard_count
///   u16 data_shards
///   u16 _reserved
///   u32 original_len
struct PktHeader {
    channel: u8,
    seq: u64,
    shard_idx: u32,
    shard_count: u32,
    data_shards: u16,
    original_len: u32,
}

impl PktHeader {
    const SIZE: usize = 1 + 8 + 4 + 4 + 2 + 2 + 4;

    fn encode(&self, out: &mut BytesMut) {
        out.put_u8(self.channel);
        out.put_u64_le(self.seq);
        out.put_u32_le(self.shard_idx);
        out.put_u32_le(self.shard_count);
        out.put_u16_le(self.data_shards);
        out.put_u16_le(0);
        out.put_u32_le(self.original_len);
    }

    fn decode(mut buf: &[u8]) -> Option<Self> {
        if buf.len() < Self::SIZE {
            return None;
        }
        let channel = buf.get_u8();
        let seq = buf.get_u64_le();
        let shard_idx = buf.get_u32_le();
        let shard_count = buf.get_u32_le();
        let data_shards = buf.get_u16_le();
        let _reserved = buf.get_u16_le();
        let original_len = buf.get_u32_le();
        Some(Self { channel, seq, shard_idx, shard_count, data_shards, original_len })
    }
}

#[derive(Clone, Copy)]
pub struct UdpConfig {
    pub mtu_payload: usize,
    pub fec: FecConfig,
    /// Optional bitrate cap (bytes/sec). When `Some`, the sender acquires from
    /// a token bucket sized at `bitrate_bps_cap / 8` capacity, smoothing burst
    /// emission so the Quest NDK ring buffer is not overwhelmed (UDP is
    /// fire-and-forget; without pacing a 150 Mbps keyframe spike will drop).
    pub bitrate_bps_cap: Option<u64>,
}

impl Default for UdpConfig {
    fn default() -> Self {
        Self {
            mtu_payload: DEFAULT_MTU_PAYLOAD,
            fec: FecConfig::default(),
            bitrate_bps_cap: None,
        }
    }
}

pub struct HeartbeatGuard {
    stop: Arc<std::sync::atomic::AtomicBool>,
    handle: Option<tokio::task::JoinHandle<()>>,
}

impl Drop for HeartbeatGuard {
    fn drop(&mut self) {
        self.stop.store(true, std::sync::atomic::Ordering::Relaxed);
        if let Some(h) = self.handle.take() {
            h.abort();
        }
    }
}

struct ReassemblyEntry {
    shards: Vec<Option<Vec<u8>>>,
    received: usize,
    data_shards: usize,
    original_len: u32,
    deadline: Instant,
}

pub struct UdpTransport {
    cfg: UdpConfig,
    sock: Arc<UdpSocket>,
    peer: Mutex<Option<SocketAddr>>,
    seq: std::sync::atomic::AtomicU64,
    outgoing: mpsc::Sender<(Channel, Bytes)>,
    incoming_rx: tokio::sync::Mutex<Option<mpsc::Receiver<(Channel, Bytes)>>>,
    fec: FecEncoder,
    pacer: Option<Arc<TokenBucket>>,
}

impl UdpTransport {
    pub async fn bind(addr: SocketAddr, cfg: UdpConfig) -> Result<Arc<Self>> {
        let sock = Arc::new(UdpSocket::bind(addr).await?);
        Self::from_socket(sock, None, cfg)
    }

    pub async fn bind_connected(local: SocketAddr, peer: SocketAddr, cfg: UdpConfig) -> Result<Arc<Self>> {
        let sock = Arc::new(UdpSocket::bind(local).await?);
        sock.connect(peer).await?;
        Self::from_socket(sock, Some(peer), cfg)
    }

    /// Discover the RNDIS host interface, bind to `<host-ip>:port`, and target
    /// the Quest gateway at `192.168.42.129:port`. Returns an error if the
    /// tether interface is not present (caller should prompt the user to
    /// enable USB Tethering inside the headset).
    pub async fn bind_rndis(cfg: UdpConfig) -> Result<Arc<Self>> {
        let iface = tethering::find_rndis_interface().ok_or_else(|| {
            TransportError::Other(
                "no RNDIS interface in 192.168.42.0/24 — enable USB Tethering on the Quest"
                    .into(),
            )
        })?;
        Self::bind_connected(iface.bind_addr(), iface.peer_addr(), cfg).await
    }

    /// Spawn a background task that emits a small UDP datagram on the Control
    /// channel at `period`, keeping the RNDIS link awake on both ends. The
    /// task exits when the returned guard is dropped.
    pub fn spawn_heartbeat(self: &Arc<Self>, period: Duration) -> HeartbeatGuard {
        let me = self.clone();
        let stop = Arc::new(std::sync::atomic::AtomicBool::new(false));
        let stop_inner = stop.clone();
        let handle = tokio::spawn(async move {
            // Tiny payload — Control channel is non-FEC-critical and the
            // peer just needs the cable to see traffic.
            let payload = Bytes::from_static(b"\x00\x00\x00\x00fuvr-hb");
            let mut tick = tokio::time::interval(period);
            tick.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
            loop {
                if stop_inner.load(std::sync::atomic::Ordering::Relaxed) {
                    break;
                }
                tick.tick().await;
                let _ = me.outgoing.send((Channel::Control, payload.clone())).await;
            }
        });
        HeartbeatGuard { stop, handle: Some(handle) }
    }

    fn from_socket(sock: Arc<UdpSocket>, peer: Option<SocketAddr>, cfg: UdpConfig) -> Result<Arc<Self>> {
        let (out_tx, mut out_rx) = mpsc::channel::<(Channel, Bytes)>(1024);
        let (in_tx, in_rx) = mpsc::channel::<(Channel, Bytes)>(1024);
        let fec = FecEncoder::new(cfg.fec).map_err(|e| TransportError::Other(format!("{e}")))?;

        let pacer = cfg.bitrate_bps_cap.map(|bps| {
            let bytes_per_sec = (bps / 8).max(1) as f64;
            // 50 ms burst budget — large enough to not stall on a keyframe,
            // small enough to keep buffer pressure bounded.
            let capacity = (bytes_per_sec * 0.05).max(64_000.0);
            Arc::new(TokenBucket::new(capacity, bytes_per_sec))
        });

        let me = Arc::new(Self {
            cfg,
            sock: sock.clone(),
            peer: Mutex::new(peer),
            seq: std::sync::atomic::AtomicU64::new(1),
            outgoing: out_tx,
            incoming_rx: tokio::sync::Mutex::new(Some(in_rx)),
            fec,
            pacer,
        });

        // sender task
        {
            let me2 = me.clone();
            tokio::spawn(async move {
                while let Some((ch, payload)) = out_rx.recv().await {
                    if let Err(e) = me2.send_fragmented(ch, payload).await {
                        warn!(error = %e, "udp send failed");
                    }
                }
            });
        }
        // receiver task
        {
            let me2 = me.clone();
            tokio::spawn(async move {
                me2.recv_loop(in_tx).await;
            });
        }
        Ok(me)
    }

    pub fn set_peer(&self, peer: SocketAddr) {
        *self.peer.lock() = Some(peer);
    }

    pub fn local_addr(&self) -> SocketAddr {
        self.sock.local_addr().expect("udp socket lost local addr")
    }

    async fn send_fragmented(&self, channel: Channel, payload: Bytes) -> Result<()> {
        let shards = self.fec.encode(&payload).map_err(|e| TransportError::Other(format!("{e}")))?;
        let total = shards.len();
        let data_shards = self.cfg.fec.data_shards;
        let seq = self.seq.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        let original_len = payload.len() as u32;

        for (idx, shard) in shards.iter().enumerate() {
            // Each shard might still exceed mtu_payload; split coarsely. For
            // typical 90 Hz HEVC sub-frames this rarely triggers because shard
            // size = ceil(frame/data_shards) ≈ MTU at our defaults. We cap by
            // emitting one datagram per shard; users tuning bitrate must pick
            // FEC config so shard_len <= mtu_payload.
            if shard.len() > self.cfg.mtu_payload {
                return Err(TransportError::InvalidFrame(format!(
                    "shard {} exceeds mtu_payload {} (>{}B); raise data_shards",
                    idx, self.cfg.mtu_payload, shard.len()
                )));
            }
            let mut buf = BytesMut::with_capacity(PktHeader::SIZE + shard.len());
            PktHeader {
                channel: channel.into(),
                seq,
                shard_idx: idx as u32,
                shard_count: total as u32,
                data_shards: data_shards as u16,
                original_len,
            }
            .encode(&mut buf);
            buf.extend_from_slice(shard);

            if let Some(pacer) = &self.pacer {
                pacer.acquire(buf.len() as f64).await;
            }

            let peer = *self.peer.lock();
            match peer {
                Some(p) => {
                    self.sock.send_to(&buf, p).await?;
                }
                None => {
                    self.sock.send(&buf).await?;
                }
            }
        }
        Ok(())
    }

    async fn recv_loop(self: Arc<Self>, tx: mpsc::Sender<(Channel, Bytes)>) {
        let mut buf = vec![0u8; self.cfg.mtu_payload + PktHeader::SIZE + 64];
        let mut pending: HashMap<(u8, u64), ReassemblyEntry> = HashMap::new();
        let mut last_gc = Instant::now();

        loop {
            let (n, from) = match self.sock.recv_from(&mut buf).await {
                Ok(v) => v,
                Err(e) => {
                    debug!(error = %e, "udp recv error");
                    continue;
                }
            };
            if self.peer.lock().is_none() {
                *self.peer.lock() = Some(from);
            }
            let datagram = &buf[..n];
            let Some(hdr) = PktHeader::decode(datagram) else {
                continue;
            };
            let body = &datagram[PktHeader::SIZE..];
            let key = (hdr.channel, hdr.seq);

            let entry = pending.entry(key).or_insert_with(|| ReassemblyEntry {
                shards: vec![None; hdr.shard_count as usize],
                received: 0,
                data_shards: hdr.data_shards as usize,
                original_len: hdr.original_len,
                deadline: Instant::now() + REASSEMBLY_TIMEOUT,
            });
            if (hdr.shard_idx as usize) < entry.shards.len()
                && entry.shards[hdr.shard_idx as usize].is_none()
            {
                entry.shards[hdr.shard_idx as usize] = Some(body.to_vec());
                entry.received += 1;
            }

            if entry.received >= entry.data_shards {
                let mut entry = pending.remove(&key).unwrap();
                if entry.shards.iter().take(entry.data_shards).all(|s| s.is_some()) {
                    // fast path: no FEC reconstruct needed
                } else {
                    let cfg = FecConfig {
                        data_shards: entry.data_shards,
                        parity_shards: entry.shards.len() - entry.data_shards,
                    };
                    let enc = match FecEncoder::new(cfg) {
                        Ok(e) => e,
                        Err(_) => continue,
                    };
                    if enc.reconstruct(&mut entry.shards).is_err() {
                        continue;
                    }
                }
                let mut payload = Vec::with_capacity(entry.original_len as usize);
                for s in entry.shards.into_iter().take(entry.data_shards) {
                    payload.extend_from_slice(&s.unwrap());
                }
                payload.truncate(entry.original_len as usize);
                let ch = match Channel::try_from(hdr.channel) {
                    Ok(c) => c,
                    Err(_) => continue,
                };
                if tx.send((ch, Bytes::from(payload))).await.is_err() {
                    return;
                }
            }

            if last_gc.elapsed() > Duration::from_millis(50) {
                let now = Instant::now();
                pending.retain(|_, v| v.deadline > now);
                last_gc = now;
            }
        }
    }
}

#[async_trait]
impl Transport for UdpTransport {
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
