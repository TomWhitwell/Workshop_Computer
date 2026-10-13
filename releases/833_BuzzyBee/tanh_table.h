#include <stdint.h>
// the table has gradient 512 at the origin
// so we need 9 bits of fractional part
// it goes from 128 to 896
extern const int16_t tanh_table[768 + 1];
extern const uint32_t exp_table[1024 + 1];
extern const int16_t svf_a1[1024 + 1];
extern const int16_t svf_a2[1024 + 1];
extern const int16_t svf_a3[1024 + 1];

static inline int soft_clip(int s_q15) {
  if (s_q15 <= -384 * 512)
    return -32767;
  if (s_q15 >= 384 * 512)
    return 32767;
  int p = (s_q15 + 384 * 512) >> 9;
  int t0 = tanh_table[p];
  int t1 = tanh_table[p + 1];
  return t0 + (((t1 - t0) * (s_q15 & 511)) >> 9);
}

static inline int exp2_table(int s_q19) {
  int frac = s_q19 & ((1 << 19) - 1);
  int shift = 29 - (s_q19 >> 19);
  if (shift >= 30)
    return 0;
  int p = frac >> 9;
  int t0 = exp_table[p];
  int t1 = exp_table[p + 1];
  return (t0 + (((t1 - t0) >> 9) * (frac & 511))) >> shift;
}
