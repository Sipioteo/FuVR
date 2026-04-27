// SPDX-License-Identifier: Apache-2.0

//! Inbound jitter / reorder buffer for video fragments.
//!
//! Reordering policy:
//! - At most `depth_frames` outstanding frames are buffered.
//! - Each frame has a release `deadline = first_seen + deadline_ms`.
//! - A frame is released when (a) all of its expected fragments have arrived
//!   or (b) the deadline expires. Released frames evict their entry, and any
//!   later fragment for that frame is dropped.
//! - Frames are released in ascending `frame_id` order. If a younger frame
//!   becomes ready first, we hold it until older frames are released or
//!   their deadlines expire.
//! - When `depth_frames` is full, accepting a new frame forces the oldest
//!   buffered frame out (released as-is).
//!
//! This is intentionally simple — no PLC, no smoothing. The downstream
//! decoder is responsible for handling partial/missing fragments.

use std::collections::BTreeMap;
use std::time::{Duration, Instant};

/// Configuration for the jitter buffer.
#[derive(Debug, Clone, Copy)]
pub struct JitterBufferConfig {
    /// Maximum number of distinct frames held before forced eviction.
    pub depth_frames: usize,
    /// Per-frame deadline in milliseconds before forced release.
    pub deadline_ms: u64,
}

impl Default for JitterBufferConfig {
    fn default() -> Self {
        Self { depth_frames: 4, deadline_ms: 8 }
    }
}

/// A single arriving fragment. `frame_id` is monotonic per frame; `index`
/// is `0..fragment_count`. If `fragment_count` is unknown (e.g. first
/// fragment seen), pass the value carried in the wire header.
#[derive(Debug, Clone)]
pub struct Fragment<P> {
    pub frame_id: u64,
    pub index: u32,
    pub fragment_count: u32,
    pub payload: P,
}

/// A frame released by the buffer. `complete` is true iff every fragment
/// arrived before the deadline.
#[derive(Debug, Clone)]
pub struct ReleasedFrame<P> {
    pub frame_id: u64,
    pub fragments: Vec<Option<P>>,
    pub complete: bool,
}

struct Entry<P> {
    fragments: Vec<Option<P>>,
    received: u32,
    fragment_count: u32,
    deadline: Instant,
}

pub struct JitterBuffer<P> {
    cfg: JitterBufferConfig,
    /// Highest `frame_id` already released. Late fragments for it are dropped.
    last_released: Option<u64>,
    pending: BTreeMap<u64, Entry<P>>,
}

impl<P> JitterBuffer<P> {
    pub fn new(cfg: JitterBufferConfig) -> Self {
        Self { cfg, last_released: None, pending: BTreeMap::new() }
    }

    /// Insert a fragment at logical time `now`. Returns any frames the
    /// insertion caused to be released, in ascending `frame_id` order.
    pub fn push(&mut self, frag: Fragment<P>, now: Instant) -> Vec<ReleasedFrame<P>> {
        // Drop fragments for frames already released.
        if let Some(last) = self.last_released {
            if frag.frame_id <= last {
                return self.drain_expired(now);
            }
        }

        let deadline = now + Duration::from_millis(self.cfg.deadline_ms);
        let entry = self.pending.entry(frag.frame_id).or_insert_with(|| Entry {
            fragments: vec_with_capacity_none(frag.fragment_count as usize),
            received: 0,
            fragment_count: frag.fragment_count,
            deadline,
        });
        // If a later fragment carries a larger fragment_count (corrupted or
        // resized), grow the buffer.
        if (frag.fragment_count as usize) > entry.fragments.len() {
            entry.fragments.resize_with(frag.fragment_count as usize, || None);
            entry.fragment_count = frag.fragment_count;
        }
        let idx = frag.index as usize;
        if idx < entry.fragments.len() && entry.fragments[idx].is_none() {
            entry.fragments[idx] = Some(frag.payload);
            entry.received += 1;
        }

        let mut released = self.drain_expired(now);

        // If depth exceeded, force out the oldest.
        while self.pending.len() > self.cfg.depth_frames {
            if let Some((&fid, _)) = self.pending.iter().next() {
                if let Some(rel) = self.try_release(fid, /*force=*/ true) {
                    released.push(rel);
                }
            } else {
                break;
            }
        }

        // Then release any complete frames that are now the oldest pending.
        loop {
            let oldest = self.pending.iter().next().map(|(&fid, e)| (fid, e.received >= e.fragment_count));
            match oldest {
                Some((fid, true)) => {
                    if let Some(rel) = self.try_release(fid, false) {
                        released.push(rel);
                    } else {
                        break;
                    }
                }
                _ => break,
            }
        }

        released.sort_by_key(|r| r.frame_id);
        released
    }

    /// Release any frames whose deadline has expired at `now`.
    pub fn drain_expired(&mut self, now: Instant) -> Vec<ReleasedFrame<P>> {
        let mut out = Vec::new();
        let expired: Vec<u64> = self
            .pending
            .iter()
            .filter(|(_, e)| e.deadline <= now)
            .map(|(&fid, _)| fid)
            .collect();
        for fid in expired {
            if let Some(rel) = self.try_release(fid, true) {
                out.push(rel);
            }
        }
        out
    }

    fn try_release(&mut self, frame_id: u64, force: bool) -> Option<ReleasedFrame<P>> {
        let entry = self.pending.remove(&frame_id)?;
        let complete = entry.received >= entry.fragment_count;
        if !complete && !force {
            // Should not be called with force=false on incomplete; restore.
            self.pending.insert(frame_id, entry);
            return None;
        }
        match self.last_released {
            Some(last) if frame_id <= last => return None,
            _ => self.last_released = Some(frame_id),
        }
        Some(ReleasedFrame { frame_id, fragments: entry.fragments, complete })
    }
}

fn vec_with_capacity_none<P>(n: usize) -> Vec<Option<P>> {
    let mut v = Vec::with_capacity(n);
    v.resize_with(n, || None);
    v
}

#[cfg(test)]
mod tests {
    use super::*;

    fn frag(frame_id: u64, index: u32, count: u32, val: u32) -> Fragment<u32> {
        Fragment { frame_id, index, fragment_count: count, payload: val }
    }

    #[test]
    fn out_of_order_within_frame_releases_complete() {
        let mut jb = JitterBuffer::<u32>::new(JitterBufferConfig::default());
        let t0 = Instant::now();
        let r1 = jb.push(frag(1, 1, 2, 11), t0);
        assert!(r1.is_empty());
        let r2 = jb.push(frag(1, 0, 2, 10), t0);
        assert_eq!(r2.len(), 1);
        assert_eq!(r2[0].frame_id, 1);
        assert!(r2[0].complete);
        assert_eq!(r2[0].fragments[0], Some(10));
        assert_eq!(r2[0].fragments[1], Some(11));
    }

    #[test]
    fn frames_released_in_order_even_when_younger_completes_first() {
        let mut jb = JitterBuffer::<u32>::new(JitterBufferConfig::default());
        let t0 = Instant::now();
        // Frame 1 only gets one of two fragments.
        let _ = jb.push(frag(1, 0, 2, 10), t0);
        // Frame 2 arrives complete first.
        let _ = jb.push(frag(2, 0, 1, 20), t0);
        let released_so_far = jb.push(frag(2, 0, 1, 20), t0);
        // Frame 2 must NOT have been released yet because frame 1 is older
        // and within deadline.
        assert!(released_so_far.iter().all(|r| r.frame_id != 2));

        // Past deadline -> frame 1 forced out (incomplete), frame 2 follows.
        let later = t0 + Duration::from_millis(20);
        let drained = jb.drain_expired(later);
        let ids: Vec<u64> = drained.iter().map(|r| r.frame_id).collect();
        assert_eq!(ids, vec![1, 2]);
        assert!(!drained[0].complete);
        assert!(drained[1].complete);
    }

    #[test]
    fn late_fragment_after_release_is_dropped() {
        let mut jb = JitterBuffer::<u32>::new(JitterBufferConfig::default());
        let t0 = Instant::now();
        let r1 = jb.push(frag(1, 0, 1, 10), t0);
        assert_eq!(r1.len(), 1);
        // Now a stray fragment for the already-released frame_id=1.
        let r2 = jb.push(frag(1, 0, 1, 99), t0);
        assert!(r2.is_empty());
    }

    #[test]
    fn depth_limit_evicts_oldest() {
        let cfg = JitterBufferConfig { depth_frames: 2, deadline_ms: 100 };
        let mut jb = JitterBuffer::<u32>::new(cfg);
        let t0 = Instant::now();
        // 3 incomplete frames in flight; depth=2 forces oldest out.
        let _ = jb.push(frag(1, 0, 2, 10), t0);
        let _ = jb.push(frag(2, 0, 2, 20), t0);
        let r = jb.push(frag(3, 0, 2, 30), t0);
        assert!(r.iter().any(|f| f.frame_id == 1));
    }
}
