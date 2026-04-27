// SPDX-License-Identifier: Apache-2.0
#![deny(warnings)]

//! mDNS / Bonjour discovery for the FuVR Wi-Fi mode (see ADR-0009).
//!
//! Service type: `_fuvr._udp.local.`
//! TXT records: `version=1`, `codecs=hevc,h264`, `transport=udp`, `port=N`.
//!
//! On macOS we use Apple's native dnssd via the `astro-dnssd` crate. On
//! other platforms (Linux CI runners) we fall back to `mdns-sd`. The two
//! paths share the public API.

use std::collections::HashMap;
use std::time::Duration;
use thiserror::Error;

/// Service type as registered on the LAN. Includes the mandatory trailing dot.
pub const SERVICE_TYPE: &str = "_fuvr._udp.local.";
/// Hostname suffix appended to `gethostname()` when registering, so a single
/// Mac can run multiple daemons without name collisions.
pub const HOSTNAME_SUFFIX: &str = "-fuvr";
/// Schema version we advertise in the TXT `version=` field.
pub const SCHEMA_VERSION: u32 = 1;
/// Codec list advertised via the TXT `codecs=` field.
pub const ADVERTISED_CODECS: &str = "hevc,h264";
/// Transport advertised via the TXT `transport=` field.
pub const ADVERTISED_TRANSPORT: &str = "udp";

#[derive(Debug, Error)]
pub enum MdnsError {
    #[error("mDNS backend error: {0}")]
    Backend(String),
    #[error("registration not active")]
    NotRegistered,
    #[error("mDNS not supported on this platform")]
    Unsupported,
}

/// Result of a successful browse.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Discovered {
    pub hostname: String,
    pub ip: String,
    pub port: u16,
    pub txt: HashMap<String, String>,
}

/// Build the canonical TXT record set for a given UDP port.
pub fn build_txt(port: u16) -> Vec<(String, String)> {
    vec![
        ("version".to_string(), SCHEMA_VERSION.to_string()),
        ("codecs".to_string(), ADVERTISED_CODECS.to_string()),
        ("transport".to_string(), ADVERTISED_TRANSPORT.to_string()),
        ("port".to_string(), port.to_string()),
    ]
}

#[cfg(target_os = "macos")]
mod imp {
    use super::*;
    use astro_dnssd::{
        DNSServiceBuilder, RegisteredDnsService, ServiceBrowserBuilder, ServiceEventType,
    };
    use std::time::Instant;

    pub struct Advertiser {
        _registration: Option<RegisteredDnsService>,
    }

    impl Advertiser {
        pub fn register(name: &str, port: u16) -> Result<Self, MdnsError> {
            let mut builder = DNSServiceBuilder::new("_fuvr._udp", port).with_name(name);
            for (k, v) in build_txt(port) {
                builder = builder.with_key_value(k, v);
            }
            let reg = builder
                .register()
                .map_err(|e| MdnsError::Backend(format!("register: {e:?}")))?;
            Ok(Self { _registration: Some(reg) })
        }

        pub fn deregister(&mut self) -> Result<(), MdnsError> {
            self._registration.take().ok_or(MdnsError::NotRegistered)?;
            Ok(())
        }
    }

    pub struct Browser;

    impl Browser {
        pub fn discover(timeout: Duration) -> Result<Vec<Discovered>, MdnsError> {
            let browser = ServiceBrowserBuilder::new("_fuvr._udp")
                .browse()
                .map_err(|e| MdnsError::Backend(format!("browse: {e:?}")))?;
            let deadline = Instant::now() + timeout;
            let mut found: Vec<Discovered> = Vec::new();
            while Instant::now() < deadline {
                let remaining = deadline.saturating_duration_since(Instant::now());
                let slice = std::cmp::min(remaining, Duration::from_millis(200));
                if slice.is_zero() {
                    break;
                }
                match browser.recv_timeout(slice) {
                    Ok(svc) => {
                        if svc.event_type != ServiceEventType::Added {
                            continue;
                        }
                        let txt = svc.txt_record.unwrap_or_default();
                        found.push(Discovered {
                            hostname: svc.hostname.clone(),
                            ip: svc.hostname,
                            port: svc.port,
                            txt,
                        });
                    }
                    Err(_) => {
                        // Timeout / would-block; loop and re-check deadline.
                    }
                }
            }
            Ok(found)
        }
    }
}

#[cfg(not(target_os = "macos"))]
mod imp {
    use super::*;
    use mdns_sd::{ServiceDaemon, ServiceInfo};

    /// Non-macOS no-op advertiser. Per ADR-0009 the daemon registration
    /// path is macOS-only (the Quest browses, the Mac advertises). On
    /// Linux runners we still want code to compile and the browse code
    /// path to function for integration tests, so we keep a stub here.
    pub struct Advertiser {
        _daemon: Option<ServiceDaemon>,
        fullname: Option<String>,
    }

    impl Advertiser {
        pub fn register(name: &str, port: u16) -> Result<Self, MdnsError> {
            // Use mdns-sd as a portable backend so the round-trip test can
            // run on Linux CI as well.
            let daemon = ServiceDaemon::new()
                .map_err(|e| MdnsError::Backend(format!("daemon: {e}")))?;
            let host = format!("{}{}.local.", name, HOSTNAME_SUFFIX);
            let txt: Vec<(String, String)> = build_txt(port);
            let info = ServiceInfo::new(
                SERVICE_TYPE,
                name,
                &host,
                "127.0.0.1",
                port,
                &txt[..],
            )
            .map_err(|e| MdnsError::Backend(format!("info: {e}")))?;
            let fullname = info.get_fullname().to_string();
            daemon
                .register(info)
                .map_err(|e| MdnsError::Backend(format!("register: {e}")))?;
            Ok(Self { _daemon: Some(daemon), fullname: Some(fullname) })
        }

        pub fn deregister(&mut self) -> Result<(), MdnsError> {
            let daemon = self._daemon.take().ok_or(MdnsError::NotRegistered)?;
            if let Some(name) = self.fullname.take() {
                let _ = daemon.unregister(&name);
            }
            // Give the daemon a chance to send the goodbye packet.
            std::thread::sleep(Duration::from_millis(50));
            Ok(())
        }
    }

    pub struct Browser;

    impl Browser {
        pub fn discover(timeout: Duration) -> Result<Vec<Discovered>, MdnsError> {
            let daemon = ServiceDaemon::new()
                .map_err(|e| MdnsError::Backend(format!("daemon: {e}")))?;
            let receiver = daemon
                .browse(SERVICE_TYPE)
                .map_err(|e| MdnsError::Backend(format!("browse: {e}")))?;
            let mut found: Vec<Discovered> = Vec::new();
            let deadline = std::time::Instant::now() + timeout;
            while let Some(rem) = deadline.checked_duration_since(std::time::Instant::now()) {
                match receiver.recv_timeout(rem) {
                    Ok(mdns_sd::ServiceEvent::ServiceResolved(info)) => {
                        let mut txt = HashMap::new();
                        for prop in info.get_properties().iter() {
                            txt.insert(
                                prop.key().to_string(),
                                prop.val_str().to_string(),
                            );
                        }
                        let ip = info
                            .get_addresses()
                            .iter()
                            .next()
                            .map(|a| a.to_string())
                            .unwrap_or_default();
                        found.push(Discovered {
                            hostname: info.get_hostname().to_string(),
                            ip,
                            port: info.get_port(),
                            txt,
                        });
                    }
                    Ok(_) => {}
                    Err(_) => break,
                }
            }
            let _ = daemon.shutdown();
            Ok(found)
        }
    }
}

pub use imp::{Advertiser, Browser};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn txt_has_required_fields() {
        let txt = build_txt(9943);
        let map: HashMap<String, String> = txt.into_iter().collect();
        assert_eq!(map.get("version").map(String::as_str), Some("1"));
        assert_eq!(map.get("codecs").map(String::as_str), Some("hevc,h264"));
        assert_eq!(map.get("transport").map(String::as_str), Some("udp"));
        assert_eq!(map.get("port").map(String::as_str), Some("9943"));
    }
}
