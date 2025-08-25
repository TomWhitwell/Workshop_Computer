#ifndef BRAIDS_DRIVERS_SWITCH_H_
#define BRAIDS_DRIVERS_SWITCH_H_

#include "stmlib/stmlib.h"

#define SWITCH_DOWN_BOUNDRY 1000
#define SWITCH_UP_BOUNDRY 3000

namespace braids {

class Switch {
 public:
  Switch() { }
  ~Switch() { }
  
  void Init();
  void Debounce(uint16_t switch_val);
  
  inline bool released() const {
    return switch_state_ == 0x7f;
  }
  
  inline bool just_pressed() const {
    return switch_state_ == 0x80;
  }

  inline bool pressed() const {
    return switch_state_ == 0x00;
  }

  bool buffer_filled() {
    return init_count_ >= 8;
  }
   
 private:
  uint8_t init_count_ = 0;
  uint8_t switch_state_;
  
  DISALLOW_COPY_AND_ASSIGN(Switch);
};

}  // namespace braids

#endif  // BRAIDS_DRIVERS_SWITCH_H_
