// SPDX-License-Identifier: Apache-2.0

use std::time::{Duration, Instant};
use tokio::sync::Mutex;

pub struct TokenBucket {
    capacity: f64,
    rate_per_sec: f64,
    state: Mutex<BucketState>,
}

struct BucketState {
    tokens: f64,
    last: Instant,
}

impl TokenBucket {
    pub fn new(capacity: f64, rate_per_sec: f64) -> Self {
        Self {
            capacity,
            rate_per_sec,
            state: Mutex::new(BucketState { tokens: capacity, last: Instant::now() }),
        }
    }

    pub async fn acquire(&self, cost: f64) {
        loop {
            let wait = {
                let mut s = self.state.lock().await;
                let now = Instant::now();
                let elapsed = now.duration_since(s.last).as_secs_f64();
                s.tokens = (s.tokens + elapsed * self.rate_per_sec).min(self.capacity);
                s.last = now;
                if s.tokens >= cost {
                    s.tokens -= cost;
                    return;
                }
                let deficit = cost - s.tokens;
                Duration::from_secs_f64(deficit / self.rate_per_sec)
            };
            tokio::time::sleep(wait).await;
        }
    }
}
