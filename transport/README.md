# FuVR Transport

Rust workspace implementing the FuVR transport layer (USB and UDP) plus a C ABI
shim consumed by the macOS encoder/runtime.

## Crates

- `transport-core` — `Channel`, `Direction`, `Transport` async trait, packed
  `FragmentHeader`, Reed-Solomon FEC scaffolding, sequence numbering, token-
  bucket pacing, and Cap'n Proto bindings generated from `proto/fuvr.capnp` via
  the `capnpc` build script.
- `transport-usb` — ADB-reverse tunnel. Mac side runs `adb reverse tcp:9943
  tcp:9943` and accepts loopback TCP. Quest side connects to `127.0.0.1:9943`.
  Frames on the socket are length-delimited: `[u32 BE total_len][u8 channel_id]
  [payload...]`, where `total_len` covers `channel_id + payload`.
- `transport-udp` — UDP datagram transport with MTU-aware fragmentation and
  Reed-Solomon FEC. ARQ-free, FEC-only, with a 200 ms timeout-based reassembly
  buffer. Default MTU payload is 1200 bytes; default FEC `(10, 4)`.
- `transport-cli` — `fuvr-transport` binary: `loopback-bench`, `dump`,
  `clock-sync`. Used to answer SPEC §5.M0 question 1.
- `transport-ffi` — C ABI cdylib (`libtransport_ffi.dylib`) with header at
  `transport-ffi/include/fuvr_transport.h`. The macOS encoder/runtime links
  against this.

## Wire format

The Cap'n Proto schema (`proto/fuvr.capnp`) is the single source of truth.
Video on the wire is `[capnp-encoded VideoFragmentHeader][raw codec bytes]` —
**not** a packed C struct. The packed `FragmentHeader` in `transport-core` is
provided for FFI/bench convenience; production paths use the Cap'n Proto
header so `renderedLeft`/`renderedRight` (needed for ATW) ride along.

Fragmentation/FEC is performed below the Cap'n Proto layer: the upper layer
hands the transport a fully serialized frame (header + payload), and the
transport splits it into FEC shards before placing them in datagrams (UDP) or
length-delimited frames (USB).

## MTU and FEC defaults

| Setting             | Default | Notes                                      |
|---------------------|---------|--------------------------------------------|
| UDP payload bytes   | 1200    | Conservative for residential MTU           |
| FEC `(data, parity)`| `(10, 4)` | ~28% overhead; tolerates 4 lost shards |

## Running the M0 spike

```bash
# Loopback UDP throughput at 90 Hz, 200 KB/frame for 5 s
cargo run --release -p transport-cli -- loopback-bench --mode udp --hz 90 --frame-bytes 200000 --seconds 5

# Same, over an ADB-reverse-style USB loopback (requires `adb reverse` set up)
cargo run --release -p transport-cli -- loopback-bench --mode usb --hz 90 --frame-bytes 200000 --seconds 5

# Round-trip latency on the control channel
cargo run --release -p transport-cli -- clock-sync --mode udp --pings 1000
```

## Building

`capnp` (the binary) must be on PATH for the build script to generate Rust
bindings. On macOS: `brew install capnp`.

```bash
cargo build --manifest-path transport/Cargo.toml
cargo test  --manifest-path transport/Cargo.toml
```
