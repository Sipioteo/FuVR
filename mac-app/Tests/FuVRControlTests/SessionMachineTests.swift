// SPDX-License-Identifier: Apache-2.0
import XCTest
@testable import FuVRControl

final class SessionMachineTests: XCTestCase {
    typealias S = SessionState
    typealias E = SessionEvent

    // MARK: - Pure transitions

    func testHappyPathInstallFlow() {
        var s: S = .waiting
        s = SessionMachine.transition(from: s, event: .headsetAppeared(serial: "QQQ", model: "Quest 3"))
        XCTAssertEqual(s, .connected(serial: "QQQ", model: "Quest 3"))

        s = SessionMachine.transition(from: s, event: .installStarted(serial: "QQQ"))
        XCTAssertEqual(s, .installing(serial: "QQQ", progress: 0))

        s = SessionMachine.transition(from: s, event: .installProgress(0.5))
        XCTAssertEqual(s, .installing(serial: "QQQ", progress: 0.5))

        s = SessionMachine.transition(from: s, event: .installCompleted(serial: "QQQ"))
        XCTAssertEqual(s, .launching(serial: "QQQ"))

        s = SessionMachine.transition(from: s, event: .streamingHandshake(serial: "QQQ", sessionId: 42))
        XCTAssertEqual(s, .streaming(serial: "QQQ", sessionId: 42))
    }

    func testAlreadyInstalledSkipsInstalling() {
        var s: S = .connected(serial: "X", model: "Quest")
        s = SessionMachine.transition(from: s, event: .packageAlreadyPresent(serial: "X"))
        XCTAssertEqual(s, .launching(serial: "X"))
    }

    // MARK: - Resilience Rule 2: disconnect always reverts to waiting

    func testHeadsetDisappearedFromAnyStateGoesToWaiting() {
        let states: [S] = [
            .connected(serial: "X", model: nil),
            .installing(serial: "X", progress: 0.3),
            .launching(serial: "X"),
            .streaming(serial: "X", sessionId: nil),
            .error("boom"),
        ]
        for s in states {
            XCTAssertEqual(SessionMachine.transition(from: s, event: .headsetDisappeared),
                           .waiting,
                           "From \(s)")
        }
    }

    func testUserResetGoesToWaiting() {
        let result = SessionMachine.transition(from: .streaming(serial: "X", sessionId: 99),
                                               event: .userReset)
        XCTAssertEqual(result, .waiting)
    }

    // MARK: - Streaming end falls back to connected (still attached)

    func testStreamingEndedFallsBackToConnected() {
        let s = SessionMachine.transition(from: .streaming(serial: "X", sessionId: 1),
                                          event: .streamingEnded)
        XCTAssertEqual(s, .connected(serial: "X", model: nil))
    }

    // MARK: - Error path

    func testInstallFailedYieldsError() {
        let s = SessionMachine.transition(from: .installing(serial: "X", progress: 0.1),
                                          event: .installFailed("disk full"))
        if case .error(let m) = s { XCTAssertEqual(m, "disk full") }
        else { XCTFail("Expected .error, got \(s)") }
    }

    // MARK: - Listener notification

    func testListenersFireOnEveryTransition() {
        let m = SessionMachine()
        var observed: [SessionState] = []
        m.addListener { observed.append($0) }
        // Initial replay = .waiting
        XCTAssertEqual(observed, [.waiting])
        m.handle(.headsetAppeared(serial: "X", model: "Quest 3"))
        m.handle(.installStarted(serial: "X"))
        m.handle(.installCompleted(serial: "X"))
        // Expect: waiting, connected, installing(0), launching
        XCTAssertEqual(observed.count, 4)
        XCTAssertEqual(observed.last, .launching(serial: "X"))
    }

    func testIdempotentEventsDoNotDoubleFire() {
        let m = SessionMachine()
        var hits = 0
        m.addListener { _ in hits += 1 }
        // Initial replay = 1
        m.handle(.installProgress(0.5))   // ignored from .waiting
        XCTAssertEqual(hits, 1)
    }
}
