#pragma once

#include "buzzrito_preset.h"

// #eJztltuO4jgQht/F12lU5wOvMlqhANlpNNAgCH3QqN995bDdQwhq7a7mcn3nuBy7vqr6yz/LftW3z12ZP2B
// TfnRvy317XC++t3232HbP3bbMsSlt37erH2UOM2rK6Xzq281TnYU05dhtu/bU1SlnU1aP++N68bTYta/D1vV
// x8zwsqjVl3W3bt8Wpe1qX+QPMIj4+9ZvdYET08eXPZZ2b1gOeu+Py710wc/n8dLFxa8rpcbPbdcc6lWjKS9e
// vj2+L3ea1zKEp+3P/4QzMJJtyaNevww2AiEKVmdNMRGlYe6t2AOiJyAQW6hKCTVmdj2X+s5wOx64dLgOUpEJ
// moSkYidqU79vNenAGBCjTgpw5BIStKcv9atHu9uenfnCXhVHCXdFThaQpL/vlcttd2UCGAAthICMi2KfN6dB
// 164tLmYrAjPW84ZxT+/LL5cw0dTMBc4/gGsPlr2UyF2IWAwvmxKY87Tenz/g/DCw0wSEqKQSn9Kas9rvlYt0
// d+seLDQKZSmIaW2qgvTflUObfbnilsYaKi7Ma0DUvztGIKa4kU0TVUGKryTLBhQCqyq6QYgr1J7e4FABNNFO
// SMpRHtHAIvEt4QBhD6C0tR8FkBE2nSLG7uMxY1C0iNJXpHi0RANdkwVAGf29GoESBGCwZGDkw5QtQPgY13EC
// EsqY1UhJCraMJKhIWIjNHFKXMKSmWaSaNxohNPbdCA0whA/cUvQdndGq46pSOjmmgh2tKuqGwC/9+FlxvYpA
// MkJFB94vsevwrNDALIAKJJElLd/8PYKo8imISY7KxSowpkTkhhoJSVSweS9Fo8KS0SJMDmSVZQdXulZalkLq
// CYprAHUY6PgXHlTUigjNAllRXT06PKr0jIjXqohGqbplKfgsjSVBZQkNqbK5ZXGpQBtyIyE7MeS01qMQQtYD
// dExVvcTBP/B+6RTCFAYiyCWjc02JIRKNEwioxpDdpAnSDoQpZFWWPrLG/lZMqFcHBCSJIoVz73zUIHCXBlY8
// EzigOCqnsMJVTZkwxSHIEYqWpzxXUHVUY+/RQrRKCAhFR0yT1N/tY2YsnKTuiMwPGWCKGgINFGKWwgIui6hc
// yYZNeXBPWrXZzIRORFJ4KxUVOsPZCYHbOdMR7dUCuoujORhlf1cFQ9wGZqmpm7qZyKw6Xy2kksTOlkgF43NY
// DQNVeD6ugklE03v9o/n+s/LPHSlMOm341gBS1HN4HtR+41O31PTxU1/Cf3Xlb5v7+Fw6FAa0=
// clang-format off
// these are generated from the website by pressing F2 and then F12 to get the console
#define K(x) (x*3)/4
const static buzzypreset default_presets[7] = {
	{122,163,980,411,2048,K(4103),1111,0,-42, 7*256},
	{184,163,-18,997,1392,K(4095),-331,0,-204, 7*256},
	{732,163,-18,1351,2047,K(4095/2),3285,0,919, 7*256},
	{1094,163,925,693,2048,K(4096/2),4151,/*715*/0,3785, 7*256},
	{-5,623,1351,-116,1675,81,K(5063),0,4096, 7*256},
	{0,849,1359,614,1433,-651,K(5063),0,-196, 7*256},
	{-2,163,0,-17,2059,4096,K(15),0,3, 7*256},
};
#undef K
typedef struct presets_and_crc {
  buzzypreset presets[7];
  uint32_t crc;
} presets_and_crc;
static_assert((sizeof(presets_and_crc)&15) == 0, "presets_and_crc size"); // must be a multiple of 16
static presets_and_crc bp;

// clang-format on
buzzypreset buzzy_xyinterpolate(int x, int y) {
  y = clampi(y, -4096, 4096);
  int ay = abs(y);
  int inny = ay / 2;
  x = clampi(x, -4096 + inny, 4096 - inny);
  int ax = abs(x);
  int u = ax - ay / 2, v = ay, uel = (x <= 0) ? 6 : 3, vel = (y <= 0) ? 2 : 4;
  if (ax * 2 <= ay) {
    u = ay / 2 - x;
    v = ay / 2 + x;
    uel = (y <= 0) ? 1 : 5;
  } else if (x <= 0) {
    vel = (y <= 0) ? 1 : 5;
  }
  int w = 4096 - u - v;
  // debug_log("x: %d, y: %d, u: %d, v: %d, w: %d, uel: %d, vel: %d\n", x, y, u, v, w, uel, vel);
  buzzypreset p;
  buzzypreset *presets = bp.presets;
  p.spread = clampi((presets[uel].spread * u + presets[vel].spread * v + presets[0].spread * w) >> 12, 0, 4096);
  p.glide = clampi((presets[uel].glide * u + presets[vel].glide * v + presets[0].glide * w) >> 12, 0, 4096);
  p.boc_amount = clampi((presets[uel].boc_amount * u + presets[vel].boc_amount * v + presets[0].boc_amount * w) >> 12, 0, 4096);
  p.wobble_amount =
      clampi((presets[uel].wobble_amount * u + presets[vel].wobble_amount * v + presets[0].wobble_amount * w) >> 12, 0, 4096);
  p.wobble_speed =
      clampi((presets[uel].wobble_speed * u + presets[vel].wobble_speed * v + presets[0].wobble_speed * w) >> 12, 0, 4096);
  p.saw_level = clampi((presets[uel].saw_level * u + presets[vel].saw_level * v + presets[0].saw_level * w) >> 12, 0, 4096);
  p.sub_level = clampi((presets[uel].sub_level * u + presets[vel].sub_level * v + presets[0].sub_level * w) >> 12, 0, 4096);
  p.noise_level = clampi((presets[uel].noise_level * u + presets[vel].noise_level * v + presets[0].noise_level * w) >> 12, 0, 4096);
  p.comb_depth = clampi((presets[uel].comb_depth * u + presets[vel].comb_depth * v + presets[0].comb_depth * w) >> 12, -4096, 4096);
  p.comb_mul = clampi((presets[uel].comb_mul * u + presets[vel].comb_mul * v + presets[0].comb_mul * w) >> 12, -4096, 4096);

  // debug_log("cd %d", p.comb_depth);
  return p;
}

typedef struct pinknoise {
  uint32_t seed;
  uint32_t step;
  int32_t sum;
  int16_t values[4];
} pinknoise;

typedef struct interp_noise {
  int32_t v[4];
  uint32_t t;
} interp_noise;

int32_t update_pinknoise_q18(pinknoise *p) {
  uint32_t r = p->seed = p->seed * 0x0019660d + 0x3c6ef35f;
  uint32_t idx = (p->step++);
  idx = __builtin_ctz(idx | 8);
  int16_t newval = r >> 16;
  p->sum -= p->values[idx];
  p->values[idx] = newval;
  return (p->sum += newval);
}

int32_t update_interp_noise_q13(interp_noise *n, pinknoise *p, int32_t dt) {
  n->t += dt;
  while (n->t >= 65536) {
    n->t -= 65536;
    n->v[0] = n->v[1];
    n->v[1] = n->v[2];
    n->v[2] = n->v[3];
    int newv = (update_pinknoise_q18(p) >> 3); // + (p->seed & 32767) - 16384;
    n->v[3] += (newv - n->v[3]) >> 2;          // lowpass filter :)
  }
  int32_t a0 = (-n->v[0] + 3 * n->v[1] - 3 * n->v[2] + n->v[3]) >> 1;
  int32_t a1 = (2 * n->v[0] - 5 * n->v[1] + 4 * n->v[2] - n->v[3]) >> 1;
  int32_t a2 = (n->v[2] - n->v[0]) >> 1;
  int32_t a3 = n->v[1];
  int32_t t = n->t >> 2;
  return (((((a0 * t >> 14) + a1) * t >> 14) + a2) * t >> 14) + a3;
}

pinknoise pink_l = {23, 0, 0, {0}}, pink_r = {72, 0, 0, {0}};

typedef struct saw_osc {
  uint32_t t;
  uint32_t delta_t;
  int32_t ddelta_t;
  uint32_t prevsample;
  pinknoise pink;
  interp_noise noise_interp;
} saw_osc;

static inline int saw_osc_get_wobble_q16(saw_osc *saw, int wobble_speed) {
  return update_interp_noise_q13(&saw->noise_interp, &saw->pink, wobble_speed);
}

static inline int32_t saw_osc_next(saw_osc *o) {
  o->delta_t += o->ddelta_t;
  o->t += o->delta_t;
  uint32_t newsample = o->t;
  if (o->t < o->delta_t) { // edge! polyblep it.
    uint32_t fractime = mini(65535, o->t / (o->delta_t >> 16));
    o->prevsample -= (fractime * fractime) >> 1;
    fractime = 65535 - fractime;
    newsample += (fractime * fractime) >> 1;
  }
  int32_t sawtooth = (int32_t)(o->prevsample >> 14) - 131072;
  o->prevsample = newsample;
  return sawtooth;
}

typedef struct tri_osc {
  int32_t t;
  uint32_t delta_t;
  int32_t ddelta_t;
  int32_t lpf;
} tri_osc;

static inline int32_t tri_osc_next(tri_osc *o) {
  o->delta_t += o->ddelta_t;
  o->t += o->delta_t;
  int32_t triangle = abs(o->t >> 13) - 131072; // watch out! abs wraps if you try to put -32768 -> 32768
  o->lpf += (triangle - o->lpf) >> 3; // smooth out triangle wave a bit, to make it more 'siney' and remove last bit of aliasing
  return o->lpf;
}

// glide here
static inline int32_t calc_ddelta_t(int32_t delta_t, int32_t new_delta_t, int glide) {
  int d = new_delta_t - delta_t;
  glide += 50;
  if (glide > 4096)
    glide = 4096;
  return ((int64_t)(d)*glide) >> (BLOCK_SIZE_SH + 12);
  // return ((int)((new_delta_t - delta_t) >> (BLOCK_SIZE_SH + 12)) * glide) >> (0);
}

typedef struct biquad {
  int32_t state1, state2; // these are stored with 14 extra bits
} biquad;
// coefficients are 14 bit fixed point
static inline int do_biquad(int input, biquad *bq, int b0, int b1, int b2, int a1, int a2) {
  int output = (input * b0 + bq->state1 + (1 << 13)) / (1 << 14);
  bq->state1 = (input * b1 + bq->state2 - a1 * output);
  bq->state2 = (input * b2 - a2 * output);
  return output;
}

static int16_t delay_buf_l[2048];
static int16_t delay_buf_r[2048];
static int32_t delay_pos = 0;

uint32_t process_buzzrito(int16_t *audiobuf, int pitch_mv, int z_q16, int spread, int glide, int wobble_amount, int boc_amount,
                          int wobble_speed, int saw_level, int sub_level, int noise_level, int comb_depth, int comb_mul,
                          int chord_n_max
#if WASM
                          ,
                          float *debug_out
#endif
) {
  int ncd = comb_depth < 0;
  comb_depth = 4096 - abs(comb_depth);
  // comb_depth = comb_depth * comb_depth >> 12; // 4th power is too much i think.
  comb_depth = comb_depth * comb_depth >> 12;
  comb_depth = 4096 - comb_depth;
  if (ncd)
    comb_depth = -comb_depth;

  saw_level = saw_level * saw_level >> 12;
  sub_level = sub_level * sub_level >> 12;

  // note change detector
  if (chord_n_max > 4)
    chord_n_max = 4;
  static int pitch_history[16];
  static int notes_history[4];
  static int notes_sorted[4];
  static int num_notes_history = 0;
  static int pitch_history_index = 0;
  pitch_history[pitch_history_index++] = pitch_mv;
  pitch_history_index &= 15;
  if (chord_n_max <= 1) {
    num_notes_history = 1;
    notes_history[0] = pitch_mv;
    notes_sorted[0] = pitch_mv;
  } else {
    const static int CLOSEST_NOTE_REPLACEMENT_MODE = 0;
    int mn = pitch_history[0], mx = pitch_history[0], tot = pitch_history[0];
    for (int i = 1; i < 16; ++i) {
      mn = mini(mn, pitch_history[i]);
      mx = maxi(mx, pitch_history[i]);
      tot += pitch_history[i];
    }
    if (mx - mn < 30) { // pitch stable for 16 frames
      int avg = tot / 16;
      if (CLOSEST_NOTE_REPLACEMENT_MODE) {
        // replace the closest note in the history
        if (num_notes_history < chord_n_max) {
          notes_sorted[num_notes_history++] = avg;
        } else {
          int mind = 0x7fffffff, mini = 0;
          for (int i = 0; i < num_notes_history; ++i) {
            int d = abs(notes_sorted[i] - avg);
            if (d < mind) {
              mind = d;
              mini = i;
            }
          }
          notes_sorted[mini] = avg;
        }
      } else {
        // replaces the oldest note, but with 'push to front' logic
        if (num_notes_history == 0 || abs(avg - notes_history[0]) > 30) {
          // new note detected! add it to the front of the history
          int new_notes_history[4] = {avg};
          int num_new_notes_history = 1;
          // #define PUSH_TO_FRONT_MODE // if defined, we basically prevent duplicate notes in the history by pulling dups to the
          // front
          for (int i = 0; i < num_notes_history && num_new_notes_history < chord_n_max; ++i)
#ifdef PUSH_TO_FRONT_MODE
            if (abs(notes_history[i] - avg) > 30) // we only append notes that are not too close to the new note
#endif
              new_notes_history[num_new_notes_history++] = notes_history[i];
          memcpy(notes_history, new_notes_history, num_new_notes_history * sizeof(int));
          num_notes_history = num_new_notes_history;
        } else { // no new note detected, just update the most recent note
          notes_history[0] = avg;
        }
        // now sort the notes
        memcpy(notes_sorted, notes_history, num_notes_history * sizeof(int));
        for (int i = 0; i < num_notes_history; ++i)
          for (int j = i + 1; j < num_notes_history; ++j)
            if (notes_sorted[i] > notes_sorted[j]) {
              int t = notes_sorted[i];
              notes_sorted[i] = notes_sorted[j];
              notes_sorted[j] = t;
            }
      } // algorithm choice
    } // pitch stable
  } // chord sorting logic

  static tri_osc tri;
  static saw_osc saw[16];
  static int inited = 0;
  if (!inited) {
    inited = 1;
    for (int i = 0; i < 16; ++i)
      saw[i].pink.seed = i + 1;
  }

  pitch_mv = notes_sorted[0];

  //  const static int c0_q16 = 0;            // (int)(2038.636 * (1 << 19));
  //  const static int octave_q5 = 1000 * 32; // (int)(-33.3818 * 12 * 256);
  const static int middle_c_offset_q19 = (int)(23.4806373824f * (1 << 19));

  static interp_noise boc_noise;
  static pinknoise boc_pink;
  int boc_wobble = update_interp_noise_q13(&boc_noise, &boc_pink, wobble_speed) * boc_amount >> 10;
  int pitch_octaves_q19 = (pitch_mv << 17) / 250;
  int pitch_log_q19 = middle_c_offset_q19 + pitch_octaves_q19;
  pitch_log_q19 += boc_wobble;
  uint32_t new_delta_t = exp2_table(pitch_log_q19);
  // log2(1.5) is a fifth is 0.5849625007 , times 1<<19 is 306688.8195780935
  // vs 7 semitones which is (7/12)*(1<<19) 305,834.6666666667
  int comb_shift = comb_mul * ((1 << 11) / 12);
  int new_delay_time_q8 = exp2_table((40 << 19) - pitch_log_q19 - comb_shift);
  // debug_log("%g\n", (float)new_delta_t / (float)new_delay_time_q8);
  int high_rolloff = exp2_table((49 << 18) - pitch_log_q19 / 2); // 1/sqrt(f) rolloff
  // debug_log("p_q19 = %d delay_q8 = %d high rolloff = %d new delta t = %d\n", pitch_log_q19, new_delay_time_q8,
  //           high_rolloff, new_delta_t);
  if (high_rolloff > 4096)
    high_rolloff = 4096;
  static int z_q16_smooth = 0;
  z_q16_smooth += make_lpf_delta(z_q16, z_q16_smooth, 1);
  z_q16 = z_q16_smooth * high_rolloff >> 12;
  while (new_delay_time_q8 > 2046 * 256)
    new_delay_time_q8 >>= 1;
  static int delay_time_q8 = 16 * 256;
  delay_time_q8 += make_lpf_delta(new_delay_time_q8, delay_time_q8, 4);
  glide = 4096 - glide;
  glide = glide * glide >> 12;
  glide = glide * glide >> 13;
  tri.ddelta_t = calc_ddelta_t(tri.delta_t, new_delta_t >> 1, glide);
  if (z_q16 <= 1) {
    tri.delta_t = new_delta_t >> 1;
    tri.ddelta_t = 0;
  }
  int cidx = 0;
  for (int i = 0; i < 16; ++i) {
    int pitch_octaves_q19 = (notes_sorted[cidx] << 17) / 250;
    cidx++;
    if (cidx == num_notes_history)
      cidx = 0;

    int saw_pitch_log_q19 = middle_c_offset_q19 + pitch_octaves_q19;
    saw_pitch_log_q19 += (i - 8) * spread;
    saw_pitch_log_q19 += boc_wobble / 2;
    saw_pitch_log_q19 += saw_osc_get_wobble_q16(&saw[i], wobble_speed) * wobble_amount >> 9;
    uint32_t new_saw_delta_t = exp2_table(saw_pitch_log_q19);

    saw[i].ddelta_t = calc_ddelta_t(saw[i].delta_t, new_saw_delta_t, glide);
    glide = glide * 255 >> 8;
  }

  // float w0 = tri.delta_t / (65536.f * 65536.f);
  // float fcos_w0 = cosf(w0);
  // float falpha = sqrtf(1 - fcos_w0 * fcos_w0) * 0.3535533906; // 1 / (2 * sqrt(2))
  // const static int one = 1 << 28;
  // int cs = (int)(fcos_w0 * one);
  // int alpha = (int)(falpha * one);
  // int b1 = -(one + cs);
  // int b0 = -b1/2;
  // int b2 = b0;
  // int a0 = (one + alpha) >> 14;
  // int a1 = -2 * cs;
  // int a2 = one - alpha;
  // a1 /= a0;
  // a2 /= a0;
  // b0 /= a0;
  // b1 /= a0;
  // b2 /= a0;

#undef INTERP
#undef INTERP_STEP
#define INTERP(name)                                                                                                               \
  static int name##_prev = 0;                                                                                                      \
  int name##_delta = name - name##_prev;                                                                                           \
  name = name##_prev;                                                                                                              \
  name##_prev += name##_delta;                                                                                                     \
  name##_delta = (name##_delta + BLOCK_SIZE / 2) >> (BLOCK_SIZE_SH);
#define INTERP_STEP(name) name += name##_delta;
  INTERP(z_q16);
  INTERP(sub_level);
  INTERP(saw_level);
  INTERP(noise_level);
  INTERP(comb_depth);
  for (int i = 0; i < BLOCK_SIZE; ++i) {
    INTERP_STEP(z_q16);
    INTERP_STEP(sub_level);
    INTERP_STEP(saw_level);
    INTERP_STEP(noise_level);
    INTERP_STEP(comb_depth);

    int tri_samp = tri_osc_next(&tri);
    tri_samp = tri_samp * sub_level >> 14;
    int l_samp = 0;
    int r_samp = 0;
    for (int i = 0; i < 16; i += 4) {
      l_samp += saw_osc_next(&saw[i]);
      r_samp += saw_osc_next(&saw[i + 1]);
      l_samp += saw_osc_next(&saw[i + 2]);
      r_samp += saw_osc_next(&saw[i + 3]);
    }

    l_samp >>= 3;
    r_samp >>= 3;
    l_samp = l_samp * saw_level >> 14;
    r_samp = r_samp * saw_level >> 14;

    l_samp += tri_samp;
    r_samp += tri_samp;

    int noise_l = update_pinknoise_q18(&pink_l);
    int noise_r = update_pinknoise_q18(&pink_r);
    l_samp += noise_l * noise_level >> 15;
    r_samp += noise_r * noise_level >> 15;

    //  clean up the low end
    static int l_dc, r_dc;
    l_dc += make_lpf_delta((l_samp << 8), l_dc, 8);
    r_dc += make_lpf_delta((r_samp << 8), r_dc, 8);
    l_samp -= l_dc / 256;
    r_samp -= r_dc / 256;

    // comb filter
    int readpos = delay_pos - (delay_time_q8 >> 8);
    int32_t delay_l0 = delay_buf_l[readpos & 2047];
    int32_t delay_r0 = delay_buf_r[readpos & 2047];
    readpos--;
    int32_t delay_l1 = delay_buf_l[readpos & 2047];
    int32_t delay_r1 = delay_buf_r[readpos & 2047];
    int f = (delay_time_q8 & 255);
    int32_t delay_l_raw = delay_l0 + ((delay_l1 - delay_l0) * f >> 8);
    int32_t delay_r_raw = delay_r0 + ((delay_r1 - delay_r0) * f >> 8);
    static int32_t delay_l, delay_r;
    delay_l += make_lpf_delta(delay_l_raw, delay_l, 1);
    delay_r += make_lpf_delta(delay_r_raw, delay_r, 1);
    l_samp += delay_l * comb_depth >> 12;
    r_samp += delay_r * comb_depth >> 12;

    l_samp = soft_clip(l_samp);
    r_samp = soft_clip(r_samp);

    delay_buf_l[delay_pos] = l_samp;
    delay_buf_r[delay_pos] = r_samp;
    delay_pos = (delay_pos + 1) & 2047;

    int vol_mul = ((z_q16 >> 4) + 2048) >> 1;
    l_samp = l_samp * vol_mul >> 12;
    r_samp = r_samp * vol_mul >> 12;

    int gate_mul = mini(4090, maxi(0, z_q16 >> 2));
    gate_mul = gate_mul * gate_mul >> 12;
    static int l_lpf1 = 0, l_lpf2 = 0, r_lpf1 = 0, r_lpf2 = 0;
    l_lpf1 += (((l_samp << 3) - l_lpf1) * gate_mul) / (1 << 12);
    l_lpf2 += (((l_lpf1)-l_lpf2) * gate_mul) / (1 << 12);
    r_lpf1 += (((r_samp << 3) - r_lpf1) * gate_mul) / (1 << 12);
    r_lpf2 += (((r_lpf1)-r_lpf2) * gate_mul) / (1 << 12);
    // l_samp = l_samp * gate_mul >> 12;
    // r_samp = r_samp * gate_mul >> 12;
    l_samp = (l_lpf2 / 8);
    r_samp = (r_lpf2 / 8);

    //  clean up the low end part 2
    static int l_dc2, r_dc2;
    l_dc2 += make_lpf_delta((l_samp << 8), l_dc2, 8);
    r_dc2 += make_lpf_delta((r_samp << 8), r_dc2, 8);
    l_samp -= l_dc2 >> 8;
    r_samp -= r_dc2 >> 8;

    if (l_samp < -32768)
      l_samp = -32768;
    if (l_samp > 32767)
      l_samp = 32767;
    if (r_samp < -32768)
      r_samp = -32768;
    if (r_samp > 32767)
      r_samp = 32767;

    // debug_log("%d\n", l_samp);

    audiobuf[i * 2 + 0] = (l_samp);
    audiobuf[i * 2 + 1] = (r_samp);
  }
  return 0;
}

uint32_t process_buzzrito_xy(int16_t *audiobuf, int pitch_mv, int z_q16, int padx, int pady, int chord_n_max
#if WASM
                             ,
                             float *debug_out
#endif
) {
  buzzypreset bp = buzzy_xyinterpolate(padx, pady);
  // bp = presets[0];
  return process_buzzrito(audiobuf, pitch_mv, z_q16, bp.spread, bp.glide, bp.wobble_amount, bp.boc_amount, bp.wobble_speed,
                          bp.saw_level, bp.sub_level, bp.noise_level, bp.comb_depth, bp.comb_mul, chord_n_max
#if WASM
                          ,
                          debug_out
#endif
  );
}
