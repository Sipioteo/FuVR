// SPDX-License-Identifier: Apache-2.0

//! Control-channel sub-types layered ON TOP of the frozen Cap'n Proto
//! `ControlMessage` schema.
//!
//! Pass 4 introduces two new upstream control signals — `BitrateAdjustRequest`
//! and `KeyframeRequest` — but the Cap'n Proto wire schema is FROZEN at
//! schema id `@0xb1f5d4f7c2a830e5` and cannot grow new union arms until the
//! next major bump. As a workaround we piggy-back on the existing
//! `ControlMessage.error` arm using textual prefixes:
//!
//! - `bitrate-req:<kbps>` — Quest asks Mac to drop video bitrate to `<kbps>`.
//! - `keyframe-req:` — Quest asks Mac to force the next encoded frame as IDR.
//!
//! This module exposes a small, allocation-free parser and serializer that
//! both ends use until the schema bump lands. See `transport/README.md` and
//! `transport/TODO.md` for migration notes.

use thiserror::Error;

/// Prefix for the bitrate-adjust workaround.
pub const BITRATE_REQ_PREFIX: &str = "bitrate-req:";
/// Prefix for the keyframe-request workaround.
pub const KEYFRAME_REQ_PREFIX: &str = "keyframe-req:";

/// Quest -> Mac: please target `proposed_kbps` for the video encoder.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct BitrateAdjustRequest {
    pub proposed_kbps: u32,
}

/// Quest -> Mac: please force an IDR / keyframe on the next encoded frame.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct KeyframeRequest;

/// Parsed view of a `ControlMessage.error` payload.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum UpstreamControlSignal {
    BitrateAdjust(BitrateAdjustRequest),
    Keyframe(KeyframeRequest),
    /// A real error string, not one of our piggy-backed signals.
    Error(String),
}

#[derive(Debug, Error, PartialEq, Eq)]
pub enum ControlParseError {
    #[error("malformed bitrate-req: missing kbps value")]
    BitrateMissing,
    #[error("malformed bitrate-req: invalid kbps integer `{0}`")]
    BitrateInvalid(String),
    #[error("malformed bitrate-req: kbps must be > 0")]
    BitrateZero,
    #[error("malformed keyframe-req: unexpected trailing data `{0}`")]
    KeyframeTrailing(String),
}

impl BitrateAdjustRequest {
    pub fn encode(&self) -> String {
        format!("{}{}", BITRATE_REQ_PREFIX, self.proposed_kbps)
    }
}

impl KeyframeRequest {
    pub fn encode(&self) -> String {
        KEYFRAME_REQ_PREFIX.to_string()
    }
}

/// Parse the body of a `ControlMessage.error` arm. Returns a typed signal
/// when the prefix matches our workaround scheme, or rejects malformed
/// piggy-backed lines. Anything that doesn't carry one of our prefixes is
/// returned verbatim as `UpstreamControlSignal::Error`.
pub fn parse_error_arm(text: &str) -> Result<UpstreamControlSignal, ControlParseError> {
    if let Some(rest) = text.strip_prefix(BITRATE_REQ_PREFIX) {
        let trimmed = rest.trim();
        if trimmed.is_empty() {
            return Err(ControlParseError::BitrateMissing);
        }
        let kbps: u32 = trimmed
            .parse()
            .map_err(|_| ControlParseError::BitrateInvalid(trimmed.to_string()))?;
        if kbps == 0 {
            return Err(ControlParseError::BitrateZero);
        }
        return Ok(UpstreamControlSignal::BitrateAdjust(BitrateAdjustRequest {
            proposed_kbps: kbps,
        }));
    }
    if let Some(rest) = text.strip_prefix(KEYFRAME_REQ_PREFIX) {
        let trimmed = rest.trim();
        if !trimmed.is_empty() {
            return Err(ControlParseError::KeyframeTrailing(trimmed.to_string()));
        }
        return Ok(UpstreamControlSignal::Keyframe(KeyframeRequest));
    }
    Ok(UpstreamControlSignal::Error(text.to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_bitrate_request() {
        let s = BitrateAdjustRequest { proposed_kbps: 12_000 }.encode();
        assert_eq!(s, "bitrate-req:12000");
        let out = parse_error_arm(&s).unwrap();
        assert_eq!(
            out,
            UpstreamControlSignal::BitrateAdjust(BitrateAdjustRequest { proposed_kbps: 12_000 })
        );
    }

    #[test]
    fn parses_keyframe_request() {
        let s = KeyframeRequest.encode();
        assert_eq!(s, "keyframe-req:");
        assert_eq!(
            parse_error_arm(&s).unwrap(),
            UpstreamControlSignal::Keyframe(KeyframeRequest)
        );
    }

    #[test]
    fn passes_through_real_errors() {
        let out = parse_error_arm("session-init failed: codec mismatch").unwrap();
        match out {
            UpstreamControlSignal::Error(s) => {
                assert!(s.contains("codec mismatch"));
            }
            other => panic!("expected Error variant, got {:?}", other),
        }
    }

    #[test]
    fn rejects_bitrate_missing_value() {
        let err = parse_error_arm("bitrate-req:").unwrap_err();
        assert_eq!(err, ControlParseError::BitrateMissing);
    }

    #[test]
    fn rejects_bitrate_non_integer() {
        let err = parse_error_arm("bitrate-req:abc").unwrap_err();
        assert_eq!(err, ControlParseError::BitrateInvalid("abc".to_string()));
    }

    #[test]
    fn rejects_bitrate_zero() {
        let err = parse_error_arm("bitrate-req:0").unwrap_err();
        assert_eq!(err, ControlParseError::BitrateZero);
    }

    #[test]
    fn rejects_keyframe_with_trailing_data() {
        let err = parse_error_arm("keyframe-req:soon").unwrap_err();
        assert_eq!(err, ControlParseError::KeyframeTrailing("soon".to_string()));
    }

    #[test]
    fn tolerates_whitespace_around_kbps() {
        let out = parse_error_arm("bitrate-req:  9500  ").unwrap();
        assert_eq!(
            out,
            UpstreamControlSignal::BitrateAdjust(BitrateAdjustRequest { proposed_kbps: 9500 })
        );
    }
}
