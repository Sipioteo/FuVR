// SPDX-License-Identifier: Apache-2.0

#include <android_native_app_glue.h>
#include <android/log.h>
#include <atomic>
#include <thread>

#include "openxr_session.hpp"
#include "transport_client.hpp"
#include "decoder_pipeline.hpp"
#include "pose_forwarder.hpp"
#include "compositor.hpp"
#include "protocol_router.hpp"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "fuvr", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "fuvr", __VA_ARGS__)

namespace {
struct AppState {
    std::atomic<bool> resumed{false};
    std::atomic<bool> destroyed{false};
    ANativeWindow* window{nullptr};
};

void handle_cmd(android_app* app, int32_t cmd) {
    auto* st = static_cast<AppState*>(app->userData);
    switch (cmd) {
        case APP_CMD_RESUME: st->resumed = true; break;
        case APP_CMD_PAUSE:  st->resumed = false; break;
        case APP_CMD_INIT_WINDOW: st->window = app->window; break;
        case APP_CMD_TERM_WINDOW: st->window = nullptr; break;
        case APP_CMD_DESTROY: st->destroyed = true; break;
        default: break;
    }
}
}

extern "C" void android_main(android_app* app) {
    AppState state;
    app->userData = &state;
    app->onAppCmd = handle_cmd;

    fuvr::OpenXrSession session;
    if (!session.create(app)) {
        LOGE("OpenXR init failed");
        return;
    }

    fuvr::TransportClient transport;

    fuvr::DecoderPipeline decoder;
    decoder.start(fuvr::DecoderPipeline::Codec::Hevc);

    fuvr::Compositor compositor(session);
    compositor.init();

    fuvr::ProtocolRouter router(transport, decoder, session);
    router.install();
    // Transport selection on Quest:
    //   We previously tried UDP-RNDIS first with a 2 s peer-wait, falling
    //   back to TCP. That blocked the main (OpenXR) thread for 2 s during
    //   init — which the Quest compositor watchdog interprets as an
    //   unresponsive app and kills.
    //
    //   Since macOS does NOT bind the Quest's RNDIS USB function class
    //   (confirmed empirically: `ifconfig | grep 192.168.42` returns
    //   nothing after toggling USB Tethering), the UDP path can never
    //   succeed on a Mac host today. Defaulting to TCP-over-`adb reverse`
    //   restores instant startup and matches the path the mac-app's
    //   SessionOrchestrator already wires up.
    //
    //   The UDP code in `transport_client` is preserved (the dual-path
    //   client stays functional) so a future macOS release with native
    //   RNDIS support — or a Linux host — only needs main.cpp to flip
    //   back to start_udp.
    transport.start("127.0.0.1", 9943);
    router.send_hello_from_quest();

    fuvr::PoseForwarder pose_forwarder(session, transport);
    pose_forwarder.start();

    while (!app->destroyRequested && !state.destroyed.load()) {
        int events;
        android_poll_source* source;
        while (ALooper_pollAll(state.resumed ? 0 : -1, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) break;
        }
        if (!state.resumed.load()) continue;

        session.poll_events();
        if (!session.is_running()) continue;

        session.begin_frame();
        auto buf = decoder.pop_latest();
        compositor.submit_frame(buf);
        session.end_frame(compositor);
        router.send_metrics_if_due();
        router.poll_adaptive_signals();
    }

    pose_forwarder.stop();
    decoder.stop();
    transport.stop();
    compositor.shutdown();
    session.destroy();
}
