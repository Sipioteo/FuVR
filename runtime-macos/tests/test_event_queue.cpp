// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "fuvr/event_queue.hpp"

using fuvr::runtime::EventQueue;

TEST(EventQueue, EmptyPopReturnsFalse) {
  EventQueue q;
  XrEventDataBuffer ev{};
  EXPECT_FALSE(q.pop(&ev));
}

TEST(EventQueue, PushPopRoundTrip) {
  EventQueue q;
  q.pushSessionStateChanged(reinterpret_cast<XrSession>(0x1234),
                             XR_SESSION_STATE_READY);
  XrEventDataBuffer ev{};
  ASSERT_TRUE(q.pop(&ev));
  auto* s = reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
  EXPECT_EQ(s->type, XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED);
  EXPECT_EQ(s->state, XR_SESSION_STATE_READY);
  EXPECT_FALSE(q.pop(&ev));
}

TEST(EventQueue, OverflowEmitsEventsLost) {
  EventQueue q;
  for (std::size_t i = 0; i < EventQueue::kCapacity + 5; ++i) {
    q.pushSessionStateChanged(reinterpret_cast<XrSession>(0x1),
                               XR_SESSION_STATE_READY);
  }
  XrEventDataBuffer ev{};
  ASSERT_TRUE(q.pop(&ev));
  auto* lost = reinterpret_cast<XrEventDataEventsLost*>(&ev);
  EXPECT_EQ(lost->type, XR_TYPE_EVENT_DATA_EVENTS_LOST);
  EXPECT_EQ(lost->lostEventCount, 5u);

  std::size_t drained = 0;
  while (q.pop(&ev)) ++drained;
  EXPECT_EQ(drained, EventQueue::kCapacity);
}

TEST(EventQueue, ConcurrentProducerConsumer) {
  EventQueue q;
  constexpr int kN = 1000;
  std::atomic<int> consumed{0};
  std::atomic<bool> done{false};
  std::thread cons([&] {
    XrEventDataBuffer ev{};
    while (!done.load() || q.size() > 0) {
      if (q.pop(&ev)) consumed.fetch_add(1);
    }
  });
  for (int i = 0; i < kN; ++i) {
    q.pushSessionStateChanged(reinterpret_cast<XrSession>(0x1),
                               XR_SESSION_STATE_READY);
  }
  while (q.size() > 0) std::this_thread::yield();
  done.store(true);
  cons.join();
  // Why: under heavy contention each EventsLost marker takes a slot and
  // displaces another real event, so the worst-case loss is more than just
  // kCapacity. We assert that the queue is non-pathological (>=70% drain).
  EXPECT_GE(consumed.load(), kN * 7 / 10);
}
