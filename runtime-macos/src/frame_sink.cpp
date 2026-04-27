// SPDX-License-Identifier: Apache-2.0
#include "fuvr/frame_sink.hpp"

namespace fuvr::runtime {

std::unique_ptr<FrameSink> makeDefaultFrameSink() {
  return std::unique_ptr<FrameSink>(new NullFrameSink());
}

}  // namespace fuvr::runtime
