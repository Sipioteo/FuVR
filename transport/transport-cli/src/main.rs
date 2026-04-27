// SPDX-License-Identifier: Apache-2.0

use anyhow::{Context, Result};
use bytes::Bytes;
use clap::{Parser, Subcommand, ValueEnum};
use futures::StreamExt;
use std::net::SocketAddr;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tracing_subscriber::EnvFilter;
use transport_core::channel::Channel;
use transport_core::transport::Transport;
use transport_udp::{UdpConfig, UdpTransport};
use transport_usb::{UsbClient, UsbServer, DEFAULT_PORT};

#[derive(Parser)]
#[command(name = "fuvr-transport", version, about = "FuVR transport spike tool")]
struct Cli {
    #[command(subcommand)]
    cmd: Cmd,
}

#[derive(Subcommand)]
enum Cmd {
    LoopbackBench {
        #[arg(long, value_enum, default_value_t = Mode::Udp)]
        mode: Mode,
        #[arg(long, default_value_t = 90)]
        hz: u32,
        #[arg(long, default_value_t = 200_000)]
        frame_bytes: usize,
        #[arg(long, default_value_t = 5)]
        seconds: u64,
    },
    Dump {
        #[arg(long, value_enum, default_value_t = Mode::Udp)]
        mode: Mode,
        #[arg(long, default_value_t = false)]
        server: bool,
    },
    ClockSync {
        #[arg(long, value_enum, default_value_t = Mode::Udp)]
        mode: Mode,
        #[arg(long, default_value_t = 100)]
        pings: u32,
    },
    /// Advertise this host on the LAN as a fuvrd via mDNS (ADR-0009).
    MdnsAdvertise {
        #[arg(long, default_value_t = 9943)]
        port: u16,
        /// Seconds to keep the registration alive (0 = forever).
        #[arg(long, default_value_t = 0)]
        seconds: u64,
    },
    /// Browse the LAN for fuvrd advertisements via mDNS (ADR-0009).
    MdnsBrowse {
        #[arg(long, default_value_t = 5)]
        timeout: u64,
    },
    /// Pretend to be a Quest: TCP-connect to 127.0.0.1:PORT, send
    /// helloFromQuest, then receive video fragments, reassemble Annex-B HEVC
    /// access units, and pipe them into `ffplay` so you can watch what the
    /// real headset would see in a 2D window. Pair with `feed-quest`
    /// (synthetic) or with the live daemon (Blender / vdisplay).
    FakeQuest {
        #[arg(long, default_value_t = 9943)]
        port: u16,
        /// Per-eye width to advertise in helloFromQuest.
        #[arg(long, default_value_t = 2064)]
        per_eye_width: u32,
        /// Per-eye height to advertise in helloFromQuest.
        #[arg(long, default_value_t = 2208)]
        per_eye_height: u32,
        /// Refresh rate to advertise (Hz).
        #[arg(long, default_value_t = 90)]
        fps: u32,
        /// Optional path to ALSO write the reassembled HEVC Annex-B stream
        /// (so you can replay it later with ffplay/QuickTime).
        #[arg(long)]
        out: Option<String>,
        /// Skip launching ffplay (just print stats / write to --out).
        #[arg(long, default_value_t = false)]
        no_player: bool,
    },
    /// Listen on TCP 9943 and stream a pre-encoded HEVC Annex-B file to
    /// the connected Quest as a debug test pattern. Loops the file forever.
    FeedQuest {
        /// Path to the HEVC Annex-B file (e.g. produced by fuvr-encode-synthetic).
        #[arg(long)]
        hevc: String,
        /// Total stereo frame width.
        #[arg(long, default_value_t = 4128)]
        width: u32,
        /// Per-eye frame height.
        #[arg(long, default_value_t = 2208)]
        height: u32,
        /// Refresh rate (fps pacing).
        #[arg(long, default_value_t = 90)]
        fps: u32,
        /// Bitrate to advertise in helloFromMac.
        #[arg(long, default_value_t = 50_000_000)]
        bitrate: u32,
    },
}

#[derive(Clone, Copy, ValueEnum)]
enum Mode {
    Udp,
    Usb,
}

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::try_from_default_env().unwrap_or_else(|_| "info".into()))
        .init();
    let cli = Cli::parse();
    match cli.cmd {
        Cmd::LoopbackBench { mode, hz, frame_bytes, seconds } => {
            loopback_bench(mode, hz, frame_bytes, seconds).await
        }
        Cmd::Dump { mode, server } => dump(mode, server).await,
        Cmd::ClockSync { mode, pings } => clock_sync(mode, pings).await,
        Cmd::MdnsAdvertise { port, seconds } => mdns_advertise(port, seconds).await,
        Cmd::MdnsBrowse { timeout } => mdns_browse(timeout).await,
        Cmd::FeedQuest { hevc, width, height, fps, bitrate } => {
            feed_quest(hevc, width, height, fps, bitrate).await
        }
        Cmd::FakeQuest { port, per_eye_width, per_eye_height, fps, out, no_player } => {
            fake_quest(port, per_eye_width, per_eye_height, fps, out, no_player).await
        }
    }
}

async fn feed_quest(
    hevc_path: String,
    width: u32,
    height: u32,
    fps: u32,
    bitrate: u32,
) -> Result<()> {
    use ::capnp::message::{Builder, HeapAllocator};
    use ::capnp::serialize_packed;
    use tokio::io::AsyncWriteExt;
    use tokio::net::TcpListener;
    use transport_core::proto as wire;

    let bytes = std::fs::read(&hevc_path).with_context(|| format!("reading {hevc_path}"))?;
    let access_units = split_annexb(&bytes);
    if access_units.is_empty() {
        anyhow::bail!("no NAL units found in {hevc_path} (expected Annex-B)");
    }
    println!(
        "loaded {} bytes from {hevc_path}, {} access units",
        bytes.len(),
        access_units.len()
    );

    // Why: the Quest connects out via the ADB-reverse tunnel (Quest → 127.0.0.1:9943
    // → Mac:9943). UsbServer's adb-reverse spawn would conflict if `adb reverse`
    // was already set up, so we use a raw TCP listener here and assume the user
    // has already run `adb reverse tcp:9943 tcp:9943`.
    let listener = TcpListener::bind(("127.0.0.1", DEFAULT_PORT)).await?;
    println!("listening on 127.0.0.1:{DEFAULT_PORT} (run `adb reverse tcp:9943 tcp:9943` if not already)");

    loop {
        let (sock, peer) = listener.accept().await?;
        println!("Quest connected from {peer}");
        let (mut rd, mut wr) = sock.into_split();

        // Drain anything from the Quest in the background (helloFromQuest, q-metrics, pose).
        let drain = tokio::spawn(async move {
            let mut buf = [0u8; 65536];
            loop {
                use tokio::io::AsyncReadExt;
                match rd.read(&mut buf).await {
                    Ok(0) | Err(_) => break,
                    Ok(_) => {}
                }
            }
        });

        // Send helloFromMac with negotiated session config.
        let mut hello = Builder::<HeapAllocator>::new_default();
        {
            let mut ctrl = hello.init_root::<wire::control_message::Builder>();
            let mut sc = ctrl.reborrow().init_hello_from_mac();
            sc.set_per_eye_width(width / 2);
            sc.set_per_eye_height(height);
            sc.set_refresh_rate_hz(fps);
            sc.set_video_codec(wire::VideoCodec::Hevc);
            sc.set_video_bitrate_bps(bitrate);
            sc.set_audio_enabled(false);
        }
        let mut hello_bytes = Vec::new();
        serialize_packed::write_message(&mut hello_bytes, &hello)?;
        let hello_frame = transport_core::wire::encode_usb_frame(
            transport_core::channel::Channel::Control as u8,
            &hello_bytes,
        );
        wr.write_all(&hello_frame).await?;
        println!("sent helloFromMac (codec=HEVC {width}x{height} @ {fps}Hz {} Mbps)",
                 bitrate / 1_000_000);

        // Stream the access units in a loop, paced at fps.
        let frame_period = Duration::from_micros(1_000_000 / fps as u64);
        let mut frame_id: u64 = 0;
        let mut next_deadline = Instant::now();

        'outer: loop {
            for au in &access_units {
                let mut fb = Builder::<HeapAllocator>::new_default();
                {
                    let mut hdr = fb.init_root::<wire::video_fragment_header::Builder>();
                    hdr.set_frame_id(frame_id);
                    hdr.set_render_start_ns(now_ns());
                    hdr.set_total_size_bytes(au.len() as u32);
                    hdr.set_fragment_index(0);
                    hdr.set_fragment_count(1);
                    hdr.set_codec(wire::VideoCodec::Hevc);
                    hdr.set_flags(0x2 | (if frame_id < 4 { 0x1 } else { 0 })); // EndOfFrame, IDR for first few
                    hdr.set_target_display_time_ns(0);
                }
                let mut hdr_bytes = Vec::new();
                serialize_packed::write_message(&mut hdr_bytes, &fb)?;

                // Payload = [packed VideoFragmentHeader][raw NAL bytes].
                let mut payload = Vec::with_capacity(hdr_bytes.len() + au.len());
                payload.extend_from_slice(&hdr_bytes);
                payload.extend_from_slice(au);

                let frame = transport_core::wire::encode_usb_frame(
                    transport_core::channel::Channel::Video as u8,
                    &payload,
                );
                if wr.write_all(&frame).await.is_err() {
                    println!("write failed; peer disconnected");
                    break 'outer;
                }

                frame_id += 1;
                next_deadline += frame_period;
                let now = Instant::now();
                if next_deadline > now {
                    tokio::time::sleep(next_deadline - now).await;
                } else {
                    next_deadline = now;
                }

                if frame_id % (fps as u64) == 0 {
                    println!("sent {frame_id} frames");
                }
            }
        }

        let _ = drain.await;
        println!("Quest disconnected; waiting for next connection...");
    }
}

async fn fake_quest(
    port: u16,
    per_eye_width: u32,
    per_eye_height: u32,
    fps: u32,
    out: Option<String>,
    no_player: bool,
) -> Result<()> {
    use ::capnp::message::{Builder, HeapAllocator, ReaderOptions};
    use ::capnp::serialize_packed;
    use std::collections::HashMap;
    use std::io::Write;
    use std::process::{Command, Stdio};
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    use tokio::net::TcpStream;
    use transport_core::proto as wire;

    let mut sock = TcpStream::connect(("127.0.0.1", port))
        .await
        .with_context(|| format!("connecting to 127.0.0.1:{port}"))?;
    println!("fake-quest: connected to 127.0.0.1:{port}");

    // helloFromQuest
    let mut hello = Builder::<HeapAllocator>::new_default();
    {
        let mut ctrl = hello.init_root::<wire::control_message::Builder>();
        let mut caps = ctrl.reborrow().init_hello_from_quest();
        caps.set_device_model("Fake Quest 3 (Mac)");
        caps.set_system_version("fake");
        caps.set_per_eye_width(per_eye_width);
        caps.set_per_eye_height(per_eye_height);
        {
            let mut rates = caps.reborrow().init_refresh_rates_hz(1);
            rates.set(0, fps);
        }
        {
            let mut codecs = caps.reborrow().init_supported_codecs(2);
            codecs.set(0, wire::VideoCodec::Hevc);
            codecs.set(1, wire::VideoCodec::H264);
        }
        caps.set_has_hand_tracking(false);
        caps.set_has_eye_tracking(false);
    }
    let mut hello_bytes = Vec::new();
    serialize_packed::write_message(&mut hello_bytes, &hello)?;
    let hello_frame = transport_core::wire::encode_usb_frame(
        transport_core::channel::Channel::Control as u8,
        &hello_bytes,
    );
    sock.write_all(&hello_frame).await?;
    println!("fake-quest: sent helloFromQuest ({}x{} @ {fps}Hz)", per_eye_width, per_eye_height);

    // ffplay subprocess (low-latency HEVC)
    let mut player = if no_player {
        None
    } else {
        match Command::new("ffplay")
            .args([
                "-hide_banner",
                "-loglevel", "warning",
                "-fflags", "nobuffer",
                "-flags", "low_delay",
                "-framedrop",
                "-probesize", "32",
                "-analyzeduration", "0",
                "-window_title", "fake-quest",
                "-f", "hevc",
                "-i", "pipe:0",
            ])
            .stdin(Stdio::piped())
            .spawn()
        {
            Ok(child) => {
                println!("fake-quest: spawned ffplay (close the window to quit)");
                Some(child)
            }
            Err(e) => {
                eprintln!("fake-quest: ffplay spawn failed ({e}); continuing without player");
                None
            }
        }
    };
    let mut player_stdin = player.as_mut().and_then(|c| c.stdin.take());

    let mut out_file = match out.as_deref() {
        Some(p) => Some(std::fs::File::create(p).with_context(|| format!("creating {p}"))?),
        None => None,
    };

    // Reassembly buffer: frame_id -> (fragments_seen, total_count, bytes)
    struct Pending {
        seen: u32,
        total: u32,
        bytes: Vec<u8>,
    }
    let mut pending: HashMap<u64, Pending> = HashMap::new();
    let mut total_frames: u64 = 0;
    let mut total_bytes: u64 = 0;
    let mut last_log = Instant::now();

    // Read loop: [u32 BE total_len][u8 channel][payload].
    let mut hdr = [0u8; 4];
    loop {
        if sock.read_exact(&mut hdr).await.is_err() {
            println!("fake-quest: peer closed");
            break;
        }
        let total_len = u32::from_be_bytes(hdr) as usize;
        if total_len == 0 || total_len > 64 * 1024 * 1024 {
            anyhow::bail!("fake-quest: unreasonable frame length {total_len}");
        }
        let mut buf = vec![0u8; total_len];
        sock.read_exact(&mut buf).await.context("read frame body")?;
        let channel = buf[0];
        let payload = &buf[1..];

        match transport_core::channel::Channel::try_from(channel) {
            Ok(transport_core::channel::Channel::Control) => {
                println!("fake-quest: control frame ({} bytes)", payload.len());
            }
            Ok(transport_core::channel::Channel::Video) => {
                // [packed VideoFragmentHeader][raw NAL bytes]
                let mut cursor = std::io::Cursor::new(payload);
                let reader = match serialize_packed::read_message(&mut cursor, ReaderOptions::new())
                {
                    Ok(r) => r,
                    Err(e) => {
                        eprintln!("fake-quest: bad video header capnp: {e}");
                        continue;
                    }
                };
                let h: wire::video_fragment_header::Reader = reader.get_root()?;
                let frame_id = h.get_frame_id();
                let total = h.get_fragment_count().max(1);
                let idx = h.get_fragment_index();
                let pos = cursor.position() as usize;
                let nal = &payload[pos..];

                let entry = pending.entry(frame_id).or_insert_with(|| Pending {
                    seen: 0,
                    total,
                    bytes: Vec::with_capacity(nal.len() * total as usize),
                });
                entry.bytes.extend_from_slice(nal);
                entry.seen += 1;
                let _ = idx;

                let end_of_frame = (h.get_flags() & 0x2) != 0;
                if entry.seen >= entry.total || end_of_frame {
                    let pending_entry = pending.remove(&frame_id).unwrap();
                    total_frames += 1;
                    total_bytes += pending_entry.bytes.len() as u64;

                    if let Some(stdin) = player_stdin.as_mut() {
                        if stdin.write_all(&pending_entry.bytes).is_err() {
                            println!("fake-quest: ffplay closed; dropping further frames to player");
                            player_stdin = None;
                        }
                    }
                    if let Some(f) = out_file.as_mut() {
                        let _ = f.write_all(&pending_entry.bytes);
                    }
                }

                if last_log.elapsed() >= Duration::from_secs(1) {
                    println!(
                        "fake-quest: {} frames decoded, {:.2} MB total, {} pending reassembly",
                        total_frames,
                        total_bytes as f64 / (1024.0 * 1024.0),
                        pending.len()
                    );
                    last_log = Instant::now();
                }
            }
            Ok(other) => {
                println!("fake-quest: ignoring channel {:?} ({} bytes)", other, payload.len());
            }
            Err(_) => {
                eprintln!("fake-quest: invalid channel id {channel}");
            }
        }
    }

    if let Some(mut child) = player {
        drop(player_stdin);
        let _ = child.wait();
    }
    Ok(())
}

fn split_annexb(buf: &[u8]) -> Vec<&[u8]> {
    let mut out = Vec::new();
    let mut start = 0usize;
    let mut i = 0usize;
    let starts = |b: &[u8], i: usize| {
        (i + 4 <= b.len() && &b[i..i + 4] == [0u8, 0, 0, 1])
            || (i + 3 <= b.len() && &b[i..i + 3] == [0u8, 0, 1])
    };
    while i + 3 <= buf.len() {
        if starts(buf, i) {
            if i > start {
                out.push(&buf[start..i]);
            }
            start = i;
            i += if buf[i + 2] == 1 { 3 } else { 4 };
        } else {
            i += 1;
        }
    }
    if start < buf.len() {
        out.push(&buf[start..]);
    }
    out
}

fn now_ns() -> u64 {
    use std::time::SystemTime;
    SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .map(|d| d.as_nanos() as u64)
        .unwrap_or(0)
}

async fn mdns_advertise(port: u16, seconds: u64) -> Result<()> {
    let host = std::env::var("HOSTNAME")
        .ok()
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "fuvr-host".to_string());
    let name = format!("{host}-fuvr");
    let mut adv = transport_mdns::Advertiser::register(&name, port)
        .map_err(|e| anyhow::anyhow!("mdns register failed: {e}"))?;
    println!("advertising {name} on port {port} (Ctrl+C to stop)");
    if seconds == 0 {
        std::future::pending::<()>().await;
    } else {
        tokio::time::sleep(Duration::from_secs(seconds)).await;
    }
    let _ = adv.deregister();
    Ok(())
}

async fn mdns_browse(timeout_secs: u64) -> Result<()> {
    let timeout = Duration::from_secs(timeout_secs);
    let found = tokio::task::spawn_blocking(move || transport_mdns::Browser::discover(timeout))
        .await
        .context("browse task")?
        .map_err(|e| anyhow::anyhow!("mdns browse failed: {e}"))?;
    if found.is_empty() {
        println!("no fuvr services discovered in {timeout_secs}s");
    } else {
        for d in found {
            println!(
                "host={} ip={} port={} txt={:?}",
                d.hostname, d.ip, d.port, d.txt
            );
        }
    }
    Ok(())
}

async fn make_pair(mode: Mode) -> Result<(Arc<dyn Transport>, Arc<dyn Transport>)> {
    match mode {
        Mode::Udp => {
            let cfg = UdpConfig::default();
            let any: SocketAddr = "127.0.0.1:0".parse().unwrap();
            let a = UdpTransport::bind(any, cfg).await?;
            let b = UdpTransport::bind(any, cfg).await?;
            a.set_peer(b.local_addr());
            b.set_peer(a.local_addr());
            Ok((a as Arc<dyn Transport>, b as Arc<dyn Transport>))
        }
        Mode::Usb => {
            let server = UsbServer::bind(DEFAULT_PORT, false)
                .await
                .context("usb bind")?;
            let client = UsbClient::connect(DEFAULT_PORT).await.context("usb connect")?;
            Ok((client as Arc<dyn Transport>, server as Arc<dyn Transport>))
        }
    }
}

async fn loopback_bench(mode: Mode, hz: u32, frame_bytes: usize, seconds: u64) -> Result<()> {
    let (sender, receiver) = make_pair(mode).await?;
    let mut rx = receiver.recv_stream();
    let interval = Duration::from_secs_f64(1.0 / hz as f64);
    let total = hz as u64 * seconds;
    let payload = Bytes::from(vec![0xAB; frame_bytes]);

    let send_task = {
        let sender = sender.clone();
        let payload = payload.clone();
        tokio::spawn(async move {
            let mut tick = tokio::time::interval(interval);
            for _ in 0..total {
                tick.tick().await;
                let _ = sender.send_frame(Channel::Video, payload.clone()).await;
            }
        })
    };

    let start = Instant::now();
    let mut received_frames: u64 = 0;
    let mut received_bytes: u64 = 0;
    while let Ok(Some((_, b))) = tokio::time::timeout(Duration::from_secs(seconds + 2), rx.next()).await {
        received_frames += 1;
        received_bytes += b.len() as u64;
        if received_frames >= total {
            break;
        }
    }
    let _ = send_task.await;
    let elapsed = start.elapsed().as_secs_f64();
    let mbps = (received_bytes as f64 * 8.0) / elapsed / 1.0e6;
    println!(
        "frames={}/{}, bytes={}, elapsed={:.2}s, throughput={:.1} Mbps",
        received_frames, total, received_bytes, elapsed, mbps
    );
    Ok(())
}

async fn dump(mode: Mode, _server: bool) -> Result<()> {
    let (_a, b) = make_pair(mode).await?;
    let mut rx = b.recv_stream();
    while let Some((ch, bytes)) = rx.next().await {
        println!("ch={:?} len={}", ch, bytes.len());
    }
    Ok(())
}

async fn clock_sync(mode: Mode, pings: u32) -> Result<()> {
    let (a, b) = make_pair(mode).await?;
    let mut a_rx = a.recv_stream();
    let mut b_rx = b.recv_stream();

    // echo task on b
    let b_echo = b.clone();
    tokio::spawn(async move {
        while let Some((ch, bytes)) = b_rx.next().await {
            let _ = b_echo.send_frame(ch, bytes).await;
        }
    });

    let mut rtts = Vec::with_capacity(pings as usize);
    for _ in 0..pings {
        let t0 = Instant::now();
        let payload = Bytes::from_static(b"ping");
        a.send_frame(Channel::Control, payload).await?;
        if let Some((_, _)) = a_rx.next().await {
            rtts.push(t0.elapsed());
        }
    }
    if rtts.is_empty() {
        anyhow::bail!("no pongs received");
    }
    let mean = rtts.iter().sum::<Duration>() / rtts.len() as u32;
    let min = rtts.iter().min().unwrap();
    let max = rtts.iter().max().unwrap();
    println!("rtt min={:?} mean={:?} max={:?} samples={}", min, mean, max, rtts.len());
    Ok(())
}
