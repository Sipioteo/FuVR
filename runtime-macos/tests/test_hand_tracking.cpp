// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <cstring>

#include "fuvr/runtime.hpp"

namespace fr = fuvr::runtime;

namespace fuvr::runtime {
extern XrResult xrCreateInstance_impl(const XrInstanceCreateInfo*,
                                       XrInstance*) noexcept;
extern XrResult xrDestroyInstance_impl(XrInstance) noexcept;
extern XrResult xrCreateSession_impl(XrInstance, const XrSessionCreateInfo*,
                                      XrSession*) noexcept;
extern XrResult xrDestroySession_impl(XrSession) noexcept;
extern XrResult xrEnumerateInstanceExtensionProperties_impl(
    const char*, uint32_t, uint32_t*, XrExtensionProperties*) noexcept;
extern XrResult xrCreateHandTrackerEXT_impl(XrSession, const void*,
                                             uint64_t*) noexcept;
extern XrResult xrDestroyHandTrackerEXT_impl(uint64_t) noexcept;
extern XrResult xrLocateHandJointsEXT_impl(uint64_t, const void*,
                                            void*) noexcept;
}

namespace {

struct CIWire {
  XrStructureType type;
  const void* next;
  uint32_t hand;
  uint32_t handJointSet;
};

struct LocWire {
  XrStructureType type;
  const void* next;
  XrSpace baseSpace;
  XrTime time;
};

struct JointLoc {
  uint64_t locationFlags;
  XrPosef pose;
  float radius;
};

struct LocsOut {
  XrStructureType type;
  void* next;
  XrBool32 isActive;
  uint32_t jointCount;
  JointLoc* jointLocations;
};

}  // namespace

TEST(HandTracking, ExtensionAdvertised) {
  uint32_t total = 0;
  ASSERT_EQ(fr::xrEnumerateInstanceExtensionProperties_impl(
                nullptr, 0, &total, nullptr),
            XR_SUCCESS);
  std::vector<XrExtensionProperties> props(total);
  for (auto& p : props) p.type = XR_TYPE_EXTENSION_PROPERTIES;
  ASSERT_EQ(fr::xrEnumerateInstanceExtensionProperties_impl(
                nullptr, total, &total, props.data()),
            XR_SUCCESS);
  bool found = false;
  for (auto& p : props) {
    if (std::strcmp(p.extensionName, "XR_EXT_hand_tracking") == 0) found = true;
  }
  EXPECT_TRUE(found);
}

TEST(HandTracking, CreateLocateDestroy) {
  XrInstanceCreateInfo ici{};
  ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
  XrInstance inst = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateInstance_impl(&ici, &inst), XR_SUCCESS);
  XrSessionCreateInfo sci{};
  sci.type = XR_TYPE_SESSION_CREATE_INFO;
  XrSession sess = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateSession_impl(inst, &sci, &sess), XR_SUCCESS);

  CIWire ci{};
  ci.hand = 1;
  uint64_t tracker = 0;
  ASSERT_EQ(fr::xrCreateHandTrackerEXT_impl(sess, &ci, &tracker), XR_SUCCESS);
  EXPECT_NE(tracker, 0u);

  LocWire loc{};
  std::vector<JointLoc> joints(26);
  LocsOut out{};
  out.jointCount = 26;
  out.jointLocations = joints.data();
  ASSERT_EQ(fr::xrLocateHandJointsEXT_impl(tracker, &loc, &out), XR_SUCCESS);
  EXPECT_EQ(out.isActive, XR_FALSE);
  for (auto& j : joints) {
    EXPECT_EQ(j.locationFlags, 0u);
    EXPECT_FLOAT_EQ(j.pose.orientation.w, 1.0f);
  }

  EXPECT_EQ(fr::xrDestroyHandTrackerEXT_impl(tracker), XR_SUCCESS);
  EXPECT_EQ(fr::xrDestroyHandTrackerEXT_impl(tracker), XR_ERROR_HANDLE_INVALID);
  fr::xrDestroySession_impl(sess);
  fr::xrDestroyInstance_impl(inst);
}
