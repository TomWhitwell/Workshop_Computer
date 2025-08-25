#ifndef BRAIDS_UI_H_
#define BRAIDS_UI_H_

#include "braids/drivers/display.h"
#include "braids/drivers/switch.h"

#include "braids/settings.h"

namespace braids {

enum UIMode {
  UNKNOWN,
  CALIBRATION1,
  CALIBRATION2,
  PLAY
};

class Ui {
 public:
  Ui() { }
  ~Ui() { }
  
  void Init();
  void Poll(uint32_t now, uint16_t switch_val);
  UIMode GetMode();
  void SetMode(UIMode mode);

 private:
  void IncrementShape();
  void UpdateDisplay(uint32_t now);
  
  Display display_;
  Switch switch_;
  UIMode mode_;

  DISALLOW_COPY_AND_ASSIGN(Ui);
};

}  // namespace braids

#endif // BRAIDS_UI_H_
