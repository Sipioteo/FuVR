// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <cstring>

#include "fuvr/runtime.hpp"
#include "fuvr/spaces.hpp"

namespace fr = fuvr::runtime;

namespace fuvr::runtime {
extern XrResult xrCreateInstance_impl(const XrInstanceCreateInfo*,
                                       XrInstance*) noexcept;
extern XrResult xrDestroyInstance_impl(XrInstance) noexcept;
extern XrResult xrCreateSession_impl(XrInstance, const XrSessionCreateInfo*,
                                      XrSession*) noexcept;
extern XrResult xrDestroySession_impl(XrSession) noexcept;
extern XrResult xrCreateReferenceSpace_impl(XrSession,
                                             const XrReferenceSpaceCreateInfo*,
                                             XrSpace*) noexcept;
extern XrResult xrCreateActionSpace_impl(XrSession,
                                          const XrActionSpaceCreateInfo*,
                                          XrSpace*) noexcept;
extern XrResult xrDestroySpace_impl(XrSpace) noexcept;
extern XrResult xrLocateSpace_impl(XrSpace, XrSpace, XrTime,
                                    XrSpaceLocation*) noexcept;
}

namespace {

XrPosef identityPose() {
  XrPosef p{};
  p.orientation.w = 1.0f;
  return p;
}

struct InstSession {
  XrInstance instance{XR_NULL_HANDLE};
  XrSession session{XR_NULL_HANDLE};

  InstSession() {
    XrInstanceCreateInfo ici{};
    ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
    fr::xrCreateInstance_impl(&ici, &instance);
    XrSessionCreateInfo sci{};
    sci.type = XR_TYPE_SESSION_CREATE_INFO;
    fr::xrCreateSession_impl(instance, &sci, &session);
  }
  ~InstSession() {
    if (session != XR_NULL_HANDLE) fr::xrDestroySession_impl(session);
    if (instance != XR_NULL_HANDLE) fr::xrDestroyInstance_impl(instance);
  }
};

}  // namespace

TEST(Spaces, CreateAndDestroyLocal) {
  InstSession h;
  ASSERT_NE(h.session, XR_NULL_HANDLE);
  XrReferenceSpaceCreateInfo info{};
  info.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
  info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  info.poseInReferenceSpace = identityPose();
  XrSpace sp = XR_NULL_HANDLE;
  EXPECT_EQ(fr::xrCreateReferenceSpace_impl(h.session, &info, &sp), XR_SUCCESS);
  ASSERT_NE(sp, XR_NULL_HANDLE);
  EXPECT_NE(fr::lookupSpace(sp), nullptr);
  EXPECT_EQ(fr::xrDestroySpace_impl(sp), XR_SUCCESS);
  EXPECT_EQ(fr::lookupSpace(sp), nullptr);
  EXPECT_EQ(fr::xrDestroySpace_impl(sp), XR_ERROR_HANDLE_INVALID);
}

TEST(Spaces, UnsupportedRefType) {
  InstSession h;
  XrReferenceSpaceCreateInfo info{};
  info.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
  info.referenceSpaceType = static_cast<XrReferenceSpaceType>(999);
  info.poseInReferenceSpace = identityPose();
  XrSpace sp = XR_NULL_HANDLE;
  EXPECT_EQ(fr::xrCreateReferenceSpace_impl(h.session, &info, &sp),
            XR_ERROR_REFERENCE_SPACE_UNSUPPORTED);
}

TEST(Spaces, LocateViewRelativeToLocalUsesPredictor) {
  InstSession h;
  fr::Session* s = fr::lookupSession(h.session);
  ASSERT_NE(s, nullptr);
  fr::PoseSample sample{};
  sample.timestampNs = 1000;
  sample.leftEye.position = {0.5f, 1.6f, -0.2f};
  sample.leftEye.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
  sample.rightEye.position = {0.5f, 1.6f, -0.2f};
  sample.rightEye.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
  s->predictor.push(sample);

  XrReferenceSpaceCreateInfo li{};
  li.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
  li.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  li.poseInReferenceSpace = identityPose();
  XrSpace local = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateReferenceSpace_impl(h.session, &li, &local), XR_SUCCESS);

  XrReferenceSpaceCreateInfo vi = li;
  vi.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  XrSpace view = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateReferenceSpace_impl(h.session, &vi, &view), XR_SUCCESS);

  XrSpaceLocation loc{};
  loc.type = XR_TYPE_SPACE_LOCATION;
  ASSERT_EQ(fr::xrLocateSpace_impl(view, local, 1000, &loc), XR_SUCCESS);
  EXPECT_NE(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT, 0u);
  EXPECT_NEAR(loc.pose.position.x, 0.5f, 1e-5f);
  EXPECT_NEAR(loc.pose.position.y, 1.6f, 1e-5f);
  EXPECT_NEAR(loc.pose.position.z, -0.2f, 1e-5f);
}

TEST(Spaces, LocateStageReturnsIdentity) {
  InstSession h;
  XrReferenceSpaceCreateInfo li{};
  li.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
  li.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  li.poseInReferenceSpace = identityPose();
  XrSpace local = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateReferenceSpace_impl(h.session, &li, &local), XR_SUCCESS);

  XrReferenceSpaceCreateInfo si = li;
  si.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
  XrSpace stage = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateReferenceSpace_impl(h.session, &si, &stage), XR_SUCCESS);

  XrSpaceLocation loc{};
  loc.type = XR_TYPE_SPACE_LOCATION;
  ASSERT_EQ(fr::xrLocateSpace_impl(stage, local, 0, &loc), XR_SUCCESS);
  EXPECT_NEAR(loc.pose.position.x, 0.0f, 1e-5f);
  EXPECT_NEAR(loc.pose.position.y, 0.0f, 1e-5f);
  EXPECT_NEAR(loc.pose.position.z, 0.0f, 1e-5f);
  EXPECT_NEAR(loc.pose.orientation.w, 1.0f, 1e-5f);
}

TEST(Spaces, ActionSpaceLocateReturnsInvalid) {
  InstSession h;
  XrActionSpaceCreateInfo ai{};
  ai.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
  ai.poseInActionSpace = identityPose();
  XrSpace action = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateActionSpace_impl(h.session, &ai, &action), XR_SUCCESS);

  XrReferenceSpaceCreateInfo li{};
  li.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
  li.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  li.poseInReferenceSpace = identityPose();
  XrSpace local = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateReferenceSpace_impl(h.session, &li, &local), XR_SUCCESS);

  XrSpaceLocation loc{};
  loc.type = XR_TYPE_SPACE_LOCATION;
  ASSERT_EQ(fr::xrLocateSpace_impl(action, local, 0, &loc), XR_SUCCESS);
  EXPECT_EQ(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT, 0u);
  EXPECT_EQ(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT, 0u);
}
