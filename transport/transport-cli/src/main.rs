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
    }
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
