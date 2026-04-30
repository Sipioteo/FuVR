// SPDX-License-Identifier: Apache-2.0

use bytes::Bytes;
use futures::StreamExt;
use std::net::SocketAddr;
use std::time::Duration;
use transport_core::channel::Channel;
use transport_core::fec::FecConfig;
use transport_core::transport::Transport;
use transport_udp::{UdpConfig, UdpTransport};

#[tokio::test]
async fn loopback_round_trip() {
    let cfg = UdpConfig {
        mtu_payload: 1200,
        fec: FecConfig { data_shards: 4, parity_shards: 2 },
        bitrate_bps_cap: None,
    };

    let any: SocketAddr = "127.0.0.1:0".parse().unwrap();
    let a = UdpTransport::bind(any, cfg).await.unwrap();
    let b = UdpTransport::bind(any, cfg).await.unwrap();
    let a_local = a.local_addr();
    let b_local = b.local_addr();
    a.set_peer(b_local);
    b.set_peer(a_local);

    let mut b_rx = b.recv_stream();

    let payload = Bytes::from(vec![0xAB; 800]);
    a.send_frame(Channel::Video, payload.clone()).await.unwrap();

    let (ch, got) = tokio::time::timeout(Duration::from_secs(2), b_rx.next())
        .await
        .expect("timeout")
        .expect("stream ended");
    assert_eq!(ch, Channel::Video);
    assert_eq!(got, payload);
}
