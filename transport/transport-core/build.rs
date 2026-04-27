// SPDX-License-Identifier: Apache-2.0

fn main() {
    let proto = "../../proto/fuvr.capnp";
    println!("cargo:rerun-if-changed={}", proto);
    capnpc::CompilerCommand::new()
        .src_prefix("../../proto")
        .file(proto)
        .run()
        .expect("capnp schema compilation failed");
}
