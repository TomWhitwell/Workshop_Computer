#pragma once

#include "knots/src/core/shared_state.h"

namespace mtws {

class EngineInterface {
 public:
  virtual ~EngineInterface() = default;

  virtual void Init() {}
  virtual void OnSelected() {}
  virtual void ControlTick(const GlobalControlFrame& global, EngineControlFrame& frame) = 0;
  virtual void RenderSample(const EngineControlFrame& frame, int32_t& out1, int32_t& out2) = 0;
};

}  // namespace mtws
