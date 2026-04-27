# transport TODOs

- transport-usb: support multiple sequential peers (currently UsbServer accepts
  one peer then exits the accept loop).
- transport-udp: split shards larger than mtu_payload across multiple datagrams
  rather than erroring; today users must pick FEC params so shard_len <= MTU.
- transport-udp: tune REASSEMBLY_TIMEOUT per channel (video << pose).
- transport-core: token-bucket pacing is wired into the API but not yet
  applied automatically inside transports — call sites must `acquire()` before
  send_frame for now.
- transport-ffi: integrate cbindgen as a build step rather than committing
  the generated header by hand. cbindgen.toml is in place.
- Bench: add a packet-loss simulator for UDP loopback to exercise FEC under
  realistic Wi-Fi conditions.
- Replace the FFI's `block_on(send)` with a non-blocking send queue so the
  encoder thread never stalls on transport.
- Add Cap'n Proto helpers (encode/decode VideoFragmentHeader) in
  transport-core::wire to match the documented wire format.
