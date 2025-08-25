#include "braids/ui.h"

namespace braids {

using namespace stmlib;

void Ui::Init() {
  switch_.Init();
  display_.Init();
  UpdateDisplay(0);
}

void Ui::Poll(uint32_t now, uint16_t switch_val) {
  switch_.Debounce(switch_val);

  if(switch_.buffer_filled()) {
    // check for button held on boot for calibration
    if (mode_ == UIMode::UNKNOWN) {
      if(switch_.pressed()) {
        mode_ = UIMode::CALIBRATION1;
      } else {
        mode_ = UIMode::PLAY;
      }
    }
    // button pressed down
    else if(switch_.just_pressed()) {
      if(mode_ < UIMode::PLAY) {
        mode_ = (UIMode)(mode_ + 1);
      }
      else {
        IncrementShape();
      }
    }
    UpdateDisplay(now);
  }
}

void Ui::UpdateDisplay(uint32_t now) {
  if(mode_ == UIMode::PLAY) {
    display_.SetBits(1 << settings.GetValue(SETTING_SELECTED_AVAILABLE_SHAPE));
  } 
  else if(mode_ == UIMode::CALIBRATION2) {
    display_.SetBits(1 << 1);
  } 
  else {
    int val = (now & 254) > 128 ? 1 : 0;
    display_.SetBits(val << (int)mode_ - 1);    
  }
}

void Ui::IncrementShape() {
  settings.IncrementSelectedShape();
}

UIMode Ui::GetMode() {
  return mode_;
}

void Ui::SetMode(UIMode mode) {
  mode_ = mode;
}
}  // namespace braids
