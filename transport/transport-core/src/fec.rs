// SPDX-License-Identifier: Apache-2.0

use reed_solomon_erasure::galois_8::ReedSolomon;
use thiserror::Error;

#[derive(Debug, Clone, Copy)]
pub struct FecConfig {
    pub data_shards: usize,
    pub parity_shards: usize,
}

impl Default for FecConfig {
    fn default() -> Self {
        Self { data_shards: 10, parity_shards: 4 }
    }
}

#[derive(Debug, Error)]
pub enum FecError {
    #[error("rs error: {0}")]
    Rs(#[from] reed_solomon_erasure::Error),
    #[error("input shape mismatch")]
    Shape,
}

pub struct FecEncoder {
    cfg: FecConfig,
    rs: ReedSolomon,
}

impl FecEncoder {
    pub fn new(cfg: FecConfig) -> Result<Self, FecError> {
        let rs = ReedSolomon::new(cfg.data_shards, cfg.parity_shards)?;
        Ok(Self { cfg, rs })
    }

    pub fn config(&self) -> FecConfig {
        self.cfg
    }

    /// Splits `payload` into `data_shards` equal-sized data shards (zero-padded
    /// in the last one) plus `parity_shards` computed parity shards.
    /// All shards have the same length.
    pub fn encode(&self, payload: &[u8]) -> Result<Vec<Vec<u8>>, FecError> {
        let n = self.cfg.data_shards;
        let shard_len = payload.len().div_ceil(n).max(1);
        let mut shards: Vec<Vec<u8>> = (0..n + self.cfg.parity_shards)
            .map(|_| vec![0u8; shard_len])
            .collect();
        for (i, chunk) in payload.chunks(shard_len).enumerate() {
            shards[i][..chunk.len()].copy_from_slice(chunk);
        }
        self.rs.encode(&mut shards)?;
        Ok(shards)
    }

    /// Reconstructs the data shards in-place. `present` marks which shards are
    /// non-`None` in the input.
    pub fn reconstruct(&self, shards: &mut [Option<Vec<u8>>]) -> Result<(), FecError> {
        if shards.len() != self.cfg.data_shards + self.cfg.parity_shards {
            return Err(FecError::Shape);
        }
        self.rs.reconstruct(shards)?;
        Ok(())
    }
}
