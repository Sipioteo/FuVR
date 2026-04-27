// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <cstring>

#include "fuvr/path_registry.hpp"
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
extern XrResult xrEnumerateInstanceExtensionProperties_impl(
    const char*, uint32_t, uint32_t*, XrExtensionProperties*) noexcept;
extern XrResult xrCreateActionSpace_impl(XrSession,
                                          const XrActionSpaceCreateInfo*,
                                          XrSpace*) noexcept;
extern XrResult xrCreateReferenceSpace_impl(XrSession,
                                             const XrReferenceSpaceCreateInfo*,
                                             XrSpace*) noexcept;
extern XrResult xrLocateSpace_impl(XrSpace, XrSpace, XrTime,
                                    XrSpaceLocation*) noexcept;
extern XrResult xrStringToPath_impl(XrInstance, const char*, XrPath*) noexcept;
}

TEST(EyeGaze, ExtensionAdvertised) {
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
    if (std::strcmp(p.extensionName, "XR_EXT_eye_gaze_interaction") == 0) {
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

TEST(EyeGaze, ActionSpaceLocateReturnsInvalid) {
  XrInstanceCreateInfo ici{};
  ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
  XrInstance inst = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateInstance_impl(&ici, &inst), XR_SUCCESS);
  XrSessionCreateInfo sci{};
  sci.type = XR_TYPE_SESSION_CREATE_INFO;
  XrSession sess = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateSession_impl(inst, &sci, &sess), XR_SUCCESS);

  XrPath gazePath = XR_NULL_PATH;
  ASSERT_EQ(fr::xrStringToPath_impl(inst, "/user/eyes_ext", &gazePath),
            XR_SUCCESS);

  XrPosef ident{};
  ident.orientation.w = 1.0f;
  XrActionSpaceCreateInfo ai{};
  ai.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
  ai.subactionPath = gazePath;
  ai.poseInActionSpace = ident;
  XrSpace gazeSpace = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateActionSpace_impl(sess, &ai, &gazeSpace), XR_SUCCESS);

  XrReferenceSpaceCreateInfo li{};
  li.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
  li.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  li.poseInReferenceSpace = ident;
  XrSpace local = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateReferenceSpace_impl(sess, &li, &local), XR_SUCCESS);

  XrSpaceLocation loc{};
  loc.type = XR_TYPE_SPACE_LOCATION;
  ASSERT_EQ(fr::xrLocateSpace_impl(gazeSpace, local, 0, &loc), XR_SUCCESS);
  EXPECT_EQ(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT, 0u);

  fr::xrDestroySession_impl(sess);
  fr::xrDestroyInstance_impl(inst);
}
