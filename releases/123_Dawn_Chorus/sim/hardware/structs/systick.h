#pragma once
#include <cstdint>
struct systick_fake { volatile uint32_t csr, rvr, cvr, calib; };
static systick_fake systick_fake_inst;
#define systick_hw (&systick_fake_inst)
