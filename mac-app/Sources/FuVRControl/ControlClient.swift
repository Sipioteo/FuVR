// SPDX-License-Identifier: Apache-2.0
import Foundation
import Network
import FuVRCapnp

public enum ControlClientState: Equatable, Sendable {
    case idle
    case connecting
    case connected
    case failed(String)
}

public protocol ControlClientDelegate: AnyObject {
    func controlClient(_ client: ControlClient, didChangeState state: ControlClientState)
    func controlClient(_ client: ControlClient, didReceive payload: ControlPayload)
}

/// Pass-4 Cap'n Proto control client. The wire format is length-prefixed
/// packed Cap'n Proto envelopes from `proto/fuvrd.capnp`. The public Swift
/// API is unchanged from pass 1 — `connect`/`disconnect`/`send` plus a
/// delegate; the delegate sees the same `ControlPayload` enum it always saw.
public final class ControlClient {
    public weak var delegate: ControlClientDelegate?
    private let queue = DispatchQueue(label: "fuvr.control.client")
    private var connection: NWConnection?
    private var receiveBuffer = Data()
    private var seq: UInt64 = 0
    private var lastSentConfig: SessionConfig?

    private(set) public var state: ControlClientState = .idle {
        didSet { delegate?.controlClient(self, didChangeState: state) }
    }

    public init() {}

    public func connect(socketPath: String) {
        disconnect()
        state = .connecting
        let endpoint = NWEndpoint.unix(path: socketPath)
        let params = NWParameters.tcp
        let conn = NWConnection(to: endpoint, using: params)
        connection = conn
        conn.stateUpdateHandler = { [weak self] s in
            guard let self else { return }
            switch s {
            case .ready:
                self.state = .connected
                // Subscribe to streams immediately on connect.
                self.sendEnvelope(.streamMetrics)
                self.sendEnvelope(.streamLogs)
                self.scheduleReceive()
            case .failed(let err):
                self.state = .failed(err.localizedDescription)
            case .cancelled:
                self.state = .idle
            default: break
            }
        }
        conn.start(queue: queue)
    }

    public func disconnect() {
        connection?.cancel()
        connection = nil
        receiveBuffer.removeAll()
        if case .connected = state { state = .idle }
    }

    public func send(_ payload: ControlPayload) {
        if case .helloFromMac(let cfg) = payload {
            lastSentConfig = cfg
        }
        guard let env = ControlBridge.encodeOutgoing(payload, seq: nextSeq()) else { return }
        sendFrame(env)
    }

    private func sendEnvelope(_ body: CapnpEnvelope) {
        sendFrame(CapnpFramedEnvelope(seq: nextSeq(), body: body))
    }

    private func sendFrame(_ env: CapnpFramedEnvelope) {
        guard let conn = connection else { return }
        let data = CapnpCodec.encode(env)
        conn.send(content: data, completion: .contentProcessed { [weak self] err in
            if let err, let self {
                self.state = .failed("send: \(err.localizedDescription)")
            }
        })
    }

    private func nextSeq() -> UInt64 {
        seq &+= 1
        return seq
    }

    private func scheduleReceive() {
        connection?.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) { [weak self] data, _, isComplete, error in
            guard let self else { return }
            if let data, !data.isEmpty {
                self.receiveBuffer.append(data)
                self.drainBuffer()
            }
            if let error {
                self.state = .failed("recv: \(error.localizedDescription)")
                return
            }
            if isComplete {
                self.disconnect()
                return
            }
            self.scheduleReceive()
        }
    }

    private func drainBuffer() {
        while let packed = CapnpFrame.extract(from: &receiveBuffer) {
            do {
                let env = try CapnpCodec.decode(packed)
                if let payload = ControlBridge.decodeIncoming(env, sessionConfig: lastSentConfig) {
                    delegate?.controlClient(self, didReceive: payload)
                }
            } catch {
                delegate?.controlClient(self, didReceive: .error("decode: \(error)"))
            }
        }
    }
}
