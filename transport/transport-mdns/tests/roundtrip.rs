// SPDX-License-Identifier: Apache-2.0

//! Round-trip register + browse smoke test.
//!
//! Network conditions on CI runners and corporate LANs vary wildly.
//! Multicast may be filtered, Bonjour may not be running, etc. We make
//! a best-effort attempt; if the local responder cannot see the service
//! within the timeout we skip rather than fail. The unit test in
//! `src/lib.rs` covers the TXT-shape invariant deterministically.

use std::time::Duration;
use transport_mdns::{Advertiser, Browser};

#[test]
fn register_and_browse_recovers_txt() {
    let name = format!("fuvr-test-{}", std::process::id());
    let mut adv = match Advertiser::register(&name, 19943) {
        Ok(a) => a,
        Err(e) => {
            eprintln!("skipping: register failed: {e}");
            return;
        }
    };

    let found = Browser::discover(Duration::from_secs(3)).unwrap_or_default();
    let _ = adv.deregister();

    let hit = found.iter().find(|d| d.port == 19943);
    if let Some(d) = hit {
        assert_eq!(d.txt.get("version").map(String::as_str), Some("1"));
        assert_eq!(d.txt.get("transport").map(String::as_str), Some("udp"));
        assert_eq!(d.txt.get("codecs").map(String::as_str), Some("hevc,h264"));
        assert_eq!(d.txt.get("port").map(String::as_str), Some("19943"));
    } else {
        eprintln!(
            "skipping: no service observed (multicast may be filtered); {} entries",
            found.len()
        );
    }
}
