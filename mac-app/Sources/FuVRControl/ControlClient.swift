// SPDX-License-Identifier: Apache-2.0
import Foundation
import Network

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

public final class ControlClient {
    public weak var delegate: ControlClientDelegate?
    private let queue = DispatchQueue(label: "fuvr.control.client")
    private var connection: NWConnection?
    private var receiveBuffer = Data()
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
        guard let conn = connection else { return }
        do {
            let data = try ControlCodec.encode(payload)
            conn.send(content: data, completion: .contentProcessed { [weak self] err in
                if let err, let self {
                    self.state = .failed("send: \(err.localizedDescription)")
                }
            })
        } catch {
            state = .failed("encode: \(error.localizedDescription)")
        }
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
        while let nl = receiveBuffer.firstIndex(of: 0x0A) {
            let line = receiveBuffer.subdata(in: receiveBuffer.startIndex..<nl)
            receiveBuffer.removeSubrange(receiveBuffer.startIndex...nl)
            guard !line.isEmpty else { continue }
            do {
                if let payload = try ControlCodec.decode(line) {
                    delegate?.controlClient(self, didReceive: payload)
                }
            } catch {
                delegate?.controlClient(self, didReceive: .error("decode: \(error.localizedDescription)"))
            }
        }
    }
}
