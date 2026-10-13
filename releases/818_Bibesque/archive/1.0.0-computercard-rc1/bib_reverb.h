
static short reverbbuf[32768] = {};
static int reverbpos = 0;
static int woblfo1[2] = {32768 * 1024, 0};
static int woblfo2[2] = {32768 * 1024, 0};
static int shimmerpos1 = 2000;
static int shimmerpos2 = 1000;
static int shimmerfade_q13 = 1 << 13;
const static int dshimmerfade = (1 << 13) / 2048;
static int shimmer_am_q12 = 0;
static int reverb_fb_val = 0;
static int damp1 = 0;
static int damp2 = 0;
static int reverbdc = 0;
///////////////////////////////////////////

static inline void update_lfo(int *state, int freq) { // quadrature oscillator magic
  state[0] -= (state[1] * freq) >> 19;
  state[1] += (state[0] * freq) >> 19;
}

static inline int read_buf_interp(const short *buf, int pos_q8) {
  int pos = (pos_q8 >> 8) & 32767;
  int frac = pos_q8 & 255;
  int p0 = buf[pos];
  int p1 = buf[(pos + 1) & 32767];
  return p0 + (((p1 - p0) * frac) >> 8);
}

// soft clipper - y4 = y4 - (y4^3)/6;
static inline int softcube(int x) {
  x /= 2;
  int x2 = (x * x) >> 15;
  x = (x2 * x) >> 15;
  return (x * 4) / 3;
}
#define SATURATE16(x) ((x > 32767) ? 32767 : (x < -32768) ? -32768 : x)
#define SATURATE16_SOFT(x) (x - softcube(x))
#ifdef WASMxxx
#define LOG_CLIP(x)                                                                                                    \
  if (x < -32768 || x >= 32767)                                                                                        \
    debug_log("clip %d\n", x);
#else
#define LOG_CLIP(x)
#endif
#define AP(len)                                                                                                        \
  {                                                                                                                    \
    int j = (i + len) & 32767;                                                                                         \
    int d = reverbbuf[j];                                                                                              \
    acc -= d >> 1;                                                                                                     \
    LOG_CLIP(acc);                                                                                                     \
    reverbbuf[i] = SATURATE16(acc);                                                                                    \
    acc = (acc >> 1) + d;                                                                                              \
    i = j;                                                                                                             \
  }

#define AP_WOBBLE(len, lfo_q25)                                                                                        \
  {                                                                                                                    \
    int wobpos_q8 = (lfo_q25 + (1 << 25) + (1 << 14)) >> 12;                                                           \
    int j = ((i + len) << 8) - wobpos_q8;                                                                              \
    int d = read_buf_interp(reverbbuf, j);                                                                             \
    acc -= d >> 1;                                                                                                     \
    LOG_CLIP(acc);                                                                                                     \
    reverbbuf[i] = SATURATE16(acc);                                                                                    \
    acc = (acc >> 1) + d;                                                                                              \
    i = (i + len) & 32767;                                                                                             \
  }

#define DELAY(len)                                                                                                     \
  {                                                                                                                    \
    int jhalf = (i + len / 2) & 32767;                                                                                 \
    int j = (i + len + 2048) & 32767;                                                                                  \
    LOG_CLIP(acc);                                                                                                     \
    reverbbuf[i] = SATURATE16_SOFT(acc);                                                                               \
    acc = reverbbuf[jhalf];                                                                                            \
    i = j;                                                                                                             \
  }

#define DELAY_WOBBLE(len, lfo_q25)                                                                                     \
  {                                                                                                                    \
    int wobpos_q8 = (lfo_q25 + (1 << 25) + (1 << 13)) >> 12;                                                           \
    int j = ((i + (len) / 2) << 8) - wobpos_q8;                                                                        \
    LOG_CLIP(acc);                                                                                                     \
    reverbbuf[i] = SATURATE16_SOFT(acc);                                                                               \
    acc = read_buf_interp(reverbbuf, j);                                                                               \
    i = (i + (len)) & 32767;                                                                                           \
  }

// #undef AP_WOBBLE
// #define AP_WOBBLE(len, wobpos) AP(len)
// #undef DELAY_WOBBLE
// #define DELAY_WOBBLE(len, wobpos) DELAY(len)

#define DECAY() acc = SAFEMUL(acc, reverb_decay_q12, 12);
#define DAMP(dampvar)                                                                                                  \
  dampvar += (((acc << 8) - dampvar) * 5) >> 3;                                                                        \
  acc = dampvar >> 8;
#define TAP(pos) reverbbuf[(i + pos) & 32767]

#define SHIMMERUPDATE()                                                                                                \
  shimmerfade_q13 -= dshimmerfade;                                                                                     \
  if (shimmerfade_q13 <= 0) {                                                                                          \
    shimmerfade_q13 += (1 << 13);                                                                                      \
    shimmerpos1 = shimmerpos2;                                                                                         \
    shimmerpos2 = (rand() & 2047) + 4096;                                                                              \
    /*dshimmerfade = ((1 << 13) / 2048);*/                                                                             \
  }                                                                                                                    \
  shimmerpos1--;                                                                                                       \
  shimmerpos2--;

// static int theshimmer;

#ifdef WASM
int safemul(const char *file, int line, int a, int b, int sh) {
  int64_t r = (int64_t)a * b;
  if (r <= (int)0x80000000 || r >= 0x7fffffff) {
    debug_log("%s(%d): overflow %d %d sh %d\n", file, line, a, b, sh);
  }
  return r >> sh;
}
#define SAFEMUL(a, b, sh) safemul(__FILE__, __LINE__, a, b, sh)
#else
#define SAFEMUL(a, b, sh) ((a) * (b) >> (sh))
#endif
#define SHIMMERTAP()                                                                                                   \
  {                                                                                                                    \
    /*if (shimmerpos1 >= 4096 + 2048 || shimmerpos2 >= 4096 + 2048 || shimmerpos1 < 0 || shimmerpos2 < 0) */           \
    /*  debug_log("shimmer oiut of bounds %d, %d\n", shimmerpos1, shimmerpos2);                           */           \
    int shim1 = TAP(shimmerpos1) + TAP(shimmerpos1 + 1);                                                               \
    int shim2 = TAP(shimmerpos2) + TAP(shimmerpos2 + 1);                                                               \
    shim1 = shim2 + SAFEMUL((shim1 - shim2), shimmerfade_q13, 13);                                                     \
    acc += SAFEMUL(shim1, shimmer_am_q12, 13); /*theshimmer += acc;*/                                                  \
  }
// #undef SHIMMERTAP
// #define SHIMMERTAP()

static inline void do_reverb(int reverbinl, int reverbinr, int reverb_decay_q12, int *wetl, int *wetr) {
  update_lfo(woblfo1, 23);
  update_lfo(woblfo2, 72);
  int acc = (reverbinl + reverbinr);
  int i = reverbpos;
  // dattorro / griesinger reverb
  // 142 + 107 + 379 + 277 = 905 - pre-AP
  // 672+16 + 4453 = 5089 - tank1a
  // 1800 + 3720 = 5520 - tank1b
  // 908+16 + 4217 = 5125 - tank2a
  // 2656 + 3163 = 5819 - tankb
  // total: 905 + 5089 + 5520 + 5125 + 5819 = 22458
  AP(142);
  acc += (reverbinl - reverbinr);
  AP(107);
  AP(379);
  AP(277); // , -woblfo1[0]);
  int tank_input = acc;
  // tank 1
  acc = tank_input + reverb_fb_val;
  AP_WOBBLE(672, woblfo1[0]);

  SHIMMERUPDATE();

  *wetl += acc;
  AP(95);
  *wetr += acc;

  SHIMMERTAP();
  DELAY(4453);
  DAMP(damp1);
  DECAY();
  acc += reverbinl >> 1;
  AP(1800);
  *wetl += acc;
  DELAY_WOBBLE(3720, woblfo2[0]);
  *wetr += acc;

  DECAY();
  //  wetr += acc;
  //  tank 2
  acc = tank_input + acc;
  AP_WOBBLE(908, woblfo1[1]);
  SHIMMERTAP();
  DELAY(4217);
  DAMP(damp2);
  // DECAY();
  acc += reverbinr >> 1;
  AP(2656);
  // wetl -= acc;
  //   acc += reverbinr >> 5;
  *wetr += acc;
  DELAY_WOBBLE(3163, woblfo2[1]);
  *wetl += acc;
  DECAY();
  //   wetr -= acc;

  // int amp = reverb_limiter.amp_q30 >> 16;
  // acc = SAFEMUL(amp, acc, 14);
  // int accsig = acc; // (acc * 3) / 2;
  // update_limiter(&reverb_limiter, accsig, accsig);
  // acc = (saturate((acc * 3) / 2) * 2) / 3;
  // final highpass to stop it getting too muddy
  reverbdc += ((acc << 8) - reverbdc) >> 4;
  acc -= reverbdc >> 8;
  static int reverb_limiter = 0;
  int level = abs(acc);
  level *= 256; // tune the threshold
  static int limit_hold = 0;
  static int limit_level = 32768 * 64;
  if (level > limit_level) {
    limit_level += (level - limit_level + 63) >> 6;
    limit_hold = 1000;
  } else {
    if (limit_hold > 0)
      limit_hold--;
    else {
      if (limit_level > 32768 * 64)
        limit_level -= 160; // -16 takes about 3 seconds...
      if (limit_level < 32768 * 64)
        limit_level = 32768 * 64;
    }
  }
  // static int ev = 0;
  // ev++;
  // if (ev == 1000) {
  //   if (limit_level > 32768 * 64)
  //     debug_log("limit_level %d\n", limit_level / 64);
  //   ev = 0;
  // }
  acc = (acc * 16384) / (limit_level / 128);

  if (level > reverb_limiter) {
    reverb_limiter = level;
  }

  if (level)
    reverb_fb_val = acc;
  // debug_log("reverb memory consumption %d\n", (i - reverbpos) & 32767); // currently around 20k

  reverbpos = (reverbpos - 1) & 32767;
}
