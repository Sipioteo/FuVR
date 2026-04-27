// SPDX-License-Identifier: Apache-2.0

use std::sync::atomic::{AtomicU64, Ordering};

#[derive(Debug, Default)]
pub struct SequenceNumber {
    inner: AtomicU64,
}

impl SequenceNumber {
    pub fn new(start: u64) -> Self {
        Self { inner: AtomicU64::new(start) }
    }

    pub fn next(&self) -> u64 {
        self.inner.fetch_add(1, Ordering::Relaxed)
    }

    pub fn current(&self) -> u64 {
        self.inner.load(Ordering::Relaxed)
    }
}
