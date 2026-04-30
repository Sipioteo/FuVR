// SPDX-License-Identifier: Apache-2.0

//! USB-tethering helpers for the UDP transport.
//!
//! When the Quest enables "USB Tethering" in its hidden Android settings,
//! the host (Mac) gets an `enX` interface assigned an address inside Android's
//! default tether pool `192.168.42.0/24`. The Quest itself is the gateway at
//! `.129`. This module finds the host's address on that subnet so the
//! UDP transport can bind to a deterministic local endpoint.

use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::time::{Duration, Instant};

/// Default Android USB-tethering gateway address (Quest side).
pub const ANDROID_TETHER_GATEWAY: Ipv4Addr = Ipv4Addr::new(192, 168, 42, 129);

/// Default tether subnet `/24` prefix (`192.168.42.0/24`).
pub const ANDROID_TETHER_PREFIX: [u8; 3] = [192, 168, 42];

/// Default UDP port for the dedicated VR stream.
pub const RNDIS_VR_PORT: u16 = super::DEFAULT_RNDIS_PORT;

/// Information about the local network interface bound to the RNDIS link.
#[derive(Debug, Clone)]
pub struct RndisInterface {
    /// Interface name (e.g. `en7`, `en8`) on macOS.
    pub name: String,
    /// IPv4 address assigned to the host on the tether subnet.
    pub ipv4: Ipv4Addr,
}

impl RndisInterface {
    /// Local bind address: `<host-ip>:RNDIS_VR_PORT`.
    pub fn bind_addr(&self) -> SocketAddr {
        SocketAddr::new(IpAddr::V4(self.ipv4), RNDIS_VR_PORT)
    }

    /// Default peer (Quest) address: `192.168.42.129:RNDIS_VR_PORT`.
    pub fn peer_addr(&self) -> SocketAddr {
        SocketAddr::new(IpAddr::V4(ANDROID_TETHER_GATEWAY), RNDIS_VR_PORT)
    }
}

/// Search the system's network interfaces for an IPv4 address inside the
/// Android tether subnet. Returns `None` if no matching interface is up; this
/// usually means the user has not yet toggled "USB Tethering" inside the
/// headset.
pub fn find_rndis_interface() -> Option<RndisInterface> {
    let ifaces = if_addrs::get_if_addrs().ok()?;
    for iface in ifaces {
        if iface.is_loopback() {
            continue;
        }
        let IpAddr::V4(v4) = iface.ip() else { continue };
        let octets = v4.octets();
        if octets[0] == ANDROID_TETHER_PREFIX[0]
            && octets[1] == ANDROID_TETHER_PREFIX[1]
            && octets[2] == ANDROID_TETHER_PREFIX[2]
        {
            return Some(RndisInterface { name: iface.name, ipv4: v4 });
        }
    }
    None
}

/// Block until the RNDIS interface appears, polling every `poll_ms` ms or
/// until `timeout_ms` elapses. Returns `None` on timeout.
pub async fn wait_for_rndis(poll_ms: u64, timeout_ms: u64) -> Option<RndisInterface> {
    let deadline = Instant::now() + Duration::from_millis(timeout_ms);
    loop {
        if let Some(iface) = find_rndis_interface() {
            return Some(iface);
        }
        if Instant::now() >= deadline {
            return None;
        }
        tokio::time::sleep(Duration::from_millis(poll_ms)).await;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn peer_addr_uses_quest_gateway() {
        let iface = RndisInterface {
            name: "en7".into(),
            ipv4: Ipv4Addr::new(192, 168, 42, 1),
        };
        assert_eq!(iface.peer_addr().port(), RNDIS_VR_PORT);
        assert_eq!(iface.peer_addr().ip().to_string(), "192.168.42.129");
    }
}
