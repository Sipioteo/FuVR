// SPDX-License-Identifier: Apache-2.0

use rand::{Rng, RngCore, SeedableRng};
use rand::rngs::StdRng;
use transport_core::fec::{FecConfig, FecEncoder};

fn reassemble(data_shards: usize, shards: Vec<Option<Vec<u8>>>, original_len: usize) -> Vec<u8> {
    let mut out = Vec::with_capacity(original_len);
    for s in shards.into_iter().take(data_shards) {
        out.extend_from_slice(&s.expect("data shard missing after reconstruct"));
    }
    out.truncate(original_len);
    out
}

#[test]
fn round_trip_10k_frames_with_5pct_loss() {
    let cfg = FecConfig::default();
    let enc = FecEncoder::new(cfg).unwrap();
    let mut rng = StdRng::seed_from_u64(0xFEEDFACE);

    let frames = 10_000;
    let mut recovered = 0usize;
    let mut unrecoverable = 0usize;

    for _ in 0..frames {
        let len: usize = rng.gen_range(64..4096);
        let mut payload = vec![0u8; len];
        rng.fill_bytes(&mut payload);

        let shards = enc.encode(&payload).unwrap();
        let _total = shards.len();

        let mut received: Vec<Option<Vec<u8>>> =
            shards.into_iter().map(Some).collect();
        for s in received.iter_mut() {
            if rng.gen::<f32>() < 0.05 {
                *s = None;
            }
        }
        let lost = received.iter().filter(|s| s.is_none()).count();
        if lost > cfg.parity_shards {
            unrecoverable += 1;
            continue;
        }
        enc.reconstruct(&mut received).unwrap();
        let out = reassemble(cfg.data_shards, received, payload.len());
        assert_eq!(out, payload);
        recovered += 1;
    }

    assert!(recovered > frames * 9 / 10, "too many unrecoverable frames: {}", unrecoverable);
}
