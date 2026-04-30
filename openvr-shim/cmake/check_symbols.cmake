# SPDX-License-Identifier: Apache-2.0
# Sanity-check: ensure the produced dylib exports the C entry points games
# look up via dlsym(), and that it is a universal binary (x86_64 + arm64).

if(NOT DYLIB)
  message(FATAL_ERROR "DYLIB not provided")
endif()

# 1) Required exports.
execute_process(
  COMMAND nm -gU "${DYLIB}"
  OUTPUT_VARIABLE NM_OUT
  RESULT_VARIABLE NM_RC
)
if(NOT NM_RC EQUAL 0)
  message(FATAL_ERROR "nm failed on ${DYLIB}")
endif()

set(REQUIRED_SYMBOLS
  "_VR_InitInternal2"
  "_VR_ShutdownInternal"
  "_VR_GetGenericInterface"
  "_VR_IsHmdPresent"
  "_VR_IsRuntimeInstalled"
  "_VR_GetVRInitErrorAsEnglishDescription"
  "_VR_GetVRInitErrorAsSymbol"
)
foreach(SYM IN LISTS REQUIRED_SYMBOLS)
  string(FIND "${NM_OUT}" "${SYM}" POS)
  if(POS EQUAL -1)
    message(FATAL_ERROR "missing required export: ${SYM}\n${NM_OUT}")
  endif()
endforeach()

# 2) Universal binary check.
execute_process(
  COMMAND lipo -archs "${DYLIB}"
  OUTPUT_VARIABLE ARCHS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(FIND "${ARCHS}" "x86_64" HAS_X64)
string(FIND "${ARCHS}" "arm64"  HAS_A64)
if(HAS_X64 EQUAL -1 OR HAS_A64 EQUAL -1)
  message(FATAL_ERROR "expected universal binary (x86_64+arm64), got: ${ARCHS}")
endif()

message(STATUS "openvr_api shim OK — archs: ${ARCHS}")
