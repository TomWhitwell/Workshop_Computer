#define UNITY_GAIN_FACTOR_Q8 320 // boost 4v -> 5v
// #ifndef _DEBUG
#define ENABLE_REVERB
#define ENABLE_DELAY
// #endif

#include "bib_reverb.h"

static int num_delay_taps = 1;
static uint8_t enable_line_in = 1;
static uint16_t delay_tap_times_q12[8] = {4096};
static uint16_t delay_tap_levels_q12[8] = {4096};

static short delay_buf[32768][2];
static int delaypos_q8 = 0;

static int audio_level_in = 0;
// following https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
typedef struct svf_state {
  int ic1eq, ic2eq; // 24 bit
} svf_state;
static svf_state svf_l, svf_r;

typedef struct decimator2x {
  int r1, r2, r3, r4, r5;
} decimator2x;

static inline int half(int x) { return x / 2; }

static inline int soft_clip2(int x) {
  x = (x + 32768) & (65536 * 2 - 1);
  if (x >= 65536)
    x = 65536 * 2 - 1 - x;
  return x - 32768;
}

static inline int decimate2x(decimator2x *state, int x0, int x1) {
  // Exact integer values produced by the original constant expressions.
  const static int h0 = 4096;
  const static int h1 = 2461;
  const static int h3 = -521;
  const static int h5 = 106;
  int h5x0 = h5 * x0 >> 13;
  int h3x0 = h3 * x0 >> 13;
  int h1x0 = h1 * x0 >> 13;
  int R6 = state->r5 + h5x0;
  state->r5 = state->r4 + h3x0;
  state->r4 = state->r3 + h1x0;
  state->r3 = state->r2 + h1x0 + ((h0 * x1) >> 13);
  state->r2 = state->r1 + h3x0;
  state->r1 = h5x0;
  return R6;
}

// g= tan(pi * fc / fs)
// k = 1/Q = 2-2*res; could choose k=1 for now
// a1 = 1/(1+g*(g+k))
// a2 = g*a1
// a3 = g*a2
static inline int svf_step(svf_state *s, int inp, int a1, int a2, int a3, int return_hp, int blendy) {
  // compute lowpass
  int v0 = clampi(inp / 2, -32768, 32767);
  int v3 = v0 - s->ic2eq;
  int v1 = (a1 * s->ic1eq + a2 * v3 + 16384) >> 15;
  int v2 = s->ic2eq + ((a2 * s->ic1eq + a3 * v3 + 16384) >> 15);
  s->ic1eq = clampi(2 * v1 - s->ic1eq, -32768, 32767);
  s->ic2eq = clampi(2 * v2 - s->ic2eq, -32768, 32767);
  // low is v2; high is v0-k*v1-v2
  const static int k = 1;
  int out = return_hp ? (v0 - k * v1 - v2) : v2;
  out *= 2;
  return inp + (((out - inp) * blendy) >> 9);
}

int process_bib(int16_t *audiobuf, int drive, int delay_send, int delay_time, int delay_fb, int reverb_send, int reverb_fb,
                int shimmer, int wetdry_mix, int out_level, int dj, int slider_x, int slider_p, int slider_held, int wavefold) {

  // int max_bass_scale = shimmer;
  // shimmer = 0;
  if (shimmer < 0)
    shimmer = 0;

  static int prev_slider_x = 0;
  int sp = maxi(0, slider_p - 1024);
  int new_slider_speed = (slider_x - prev_slider_x) * (sp / 8);
  prev_slider_x = slider_x;
  // if (!slider_held)
  //   new_slider_speed = 0;
  static int slider_speed = 0;
  slider_speed += make_lpf_delta(new_slider_speed, slider_speed, 7);
  debug_log("ss %d sp %d\n", slider_speed, slider_p);
  // TODO if (slider_tap.p_down_count < HOLD_TIME)
  // { slider_speed = 0; }
  // debug_log("sss %d\n", slider_speed_smoothed);

  // delay_fb = delay_fb * delay_fb >> 12;
  //  drive will be 1<<11 for half way up = unity
  //  int drive_q11 = drive;
  //  int drive_q12 = drive_q11 * drive_q11 >> 10; // 22-9 = 13
  //  if (drive_q12 > (1 << 12))
  //    drive_q12 = (drive_q12 >> 2) * drive_q12 >> 10; // for the top half, lets go BIG
  //  drive_q12 = drive_q12 * drive_q12 >> 12;
  int clamped_drive = maxi(256, drive);
  int drive_q12 = exp2_table(((clamped_drive - 2048) * 1500) + (12 << 19));
  if (drive < 256)
    drive_q12 = (drive_q12 * drive) >> 8; // taper to full 0 (-inf db) at low drive levels
  int orig_level = maxi(0, -wetdry_mix);
  orig_level = orig_level * orig_level >> 12;
  int dry_level = (4096 - orig_level);
  int dry_scale = maxi(0, 4096 - wetdry_mix);
  int wet_level = mini(4096, maxi(0, wetdry_mix + 4096));
  dry_level = dry_level * dry_scale >> 12;
  // lets do out_level at the end!

  shimmer_am_q12 = (shimmer * 800) / (reverb_fb + 1024);
  int reverb_decay = 4096 - reverb_fb;
  reverb_decay = reverb_decay * reverb_decay >> 12;
  reverb_decay = reverb_decay * reverb_decay >> 12;
  reverb_decay = 4096 - reverb_decay;

  int pingpong = delay_send < 0;
#define PINGPONG_IS_DIFFERENT_DELAY_LENGTH
  reverb_send = reverb_send * reverb_send >> 13;
  delay_send = delay_send * delay_send >> 12;

  int delay_time_scale_q4 = delay_time; // exp2_table((delay_time * 700) + (28 << 18)) + 8;
  static int delay_time_scale_q4_smooth = 1000;
  delay_time_scale_q4_smooth += make_lpf_delta(delay_time_scale_q4, delay_time_scale_q4_smooth, 7);
  delay_time_scale_q4 = delay_time_scale_q4_smooth;
  // debug_log("delay_time_scale %d\n", delay_time_scale_q4 >> 4);
  int tape_speed_scale_q8 = 256;
  if (delay_time_scale_q4 > 32766 * 16) {
    // if delay too long, slow down tape to compensate
    tape_speed_scale_q8 = 32766 * 16 * 256 / delay_time_scale_q4;
    delay_time_scale_q4 = 32766 * 16;
  }

  int tape_stop = slider_p - 2048;
  if (!slider_held)
    tape_stop = 0;
  if (abs(slider_speed) > 1000) {
    tape_stop = 0; // disable tape stop if swiping
    //  debug_log("disable swipe\n");
  }
  if (tape_stop < 0)
    tape_stop = 0;
  if (tape_stop > 1024)
    tape_stop = 1024;
  int tape_speed_q16 = (1024 - tape_stop) * tape_speed_scale_q8 >> 2;
  if (tape_speed_q16 < 0)
    tape_speed_q16 = 0;
  tape_speed_q16 = (tape_speed_q16 * exp2_table((slider_speed * 4) + (10 << 19)) + 512) >> 10;
  tape_speed_q16 = mini(65536 * 2, tape_speed_q16);
  // Workshop's latched Delay-Up page supplies a stable absolute tape rate.
  // The original slider path above remains intact for normal Bib processing.
  if (slider_held == 2)
    tape_speed_q16 = clampi(slider_x, 0, 65536 * 2);
  static int tape_speed_smooth_q16 = 65536;
  tape_speed_smooth_q16 += make_lpf_delta(tape_speed_q16, tape_speed_smooth_q16, 7);

  // when tape slows down, reduce amount we write to it
  delay_send *= (tape_speed_smooth_q16 / tape_speed_scale_q8) >> 8;

  audio_level_in = (audio_level_in * 240) >> 8; // decay the input VU meter

  ///// adapt delay_fb for delay time
  delay_fb = 4096 - delay_fb;
  delay_fb = delay_fb * delay_fb >> 12;
  delay_fb = 4096 - delay_fb;
  if (delay_fb < 0)
    delay_fb = 0;
  if (delay_fb > 4094)
    delay_fb = 4094;
  // rotate by pi*4/5, ie 144 degrees
  const static int rotate_sin_q12 = 2408;
  const static int rotate_cos_q12 = -3314;
  int delay_fb_cos = rotate_cos_q12 * delay_fb >> 12;
  int delay_fb_sin = rotate_sin_q12 * delay_fb >> 12;

// interpolate drive_q12, delay_fb_*, dry_level, wet_level, orig_level, reverb_send, delay_send
// also need to interpolate delay_time_q8, tape_speed_q8
#undef INTERP
#undef INTERP_STEP
#define INTERP(name)                                                                                                               \
  static int name##_prev = 0;                                                                                                      \
  int name##_delta = name - name##_prev;                                                                                           \
  name = name##_prev;                                                                                                              \
  name##_prev += name##_delta;                                                                                                     \
  name##_delta = (name##_delta + BLOCK_SIZE / 4) >> (BLOCK_SIZE_SH - 1);
#define INTERP_STEP(name) name += name##_delta;
  INTERP(drive_q12);
  INTERP(reverb_send);
  INTERP(delay_send);
  INTERP(dry_level);
  INTERP(wet_level);
  INTERP(orig_level);
  INTERP(delay_fb_cos);
  INTERP(delay_fb_sin);

  int rv = 0;
  for (int samp = 0; samp < BLOCK_SIZE * 2; samp += 4) {
    INTERP_STEP(drive_q12);
    INTERP_STEP(reverb_send);
    INTERP_STEP(delay_send);
    INTERP_STEP(dry_level);
    INTERP_STEP(wet_level);
    INTERP_STEP(orig_level);
    INTERP_STEP(delay_fb_cos);
    INTERP_STEP(delay_fb_sin);

    int orig_l0 = audiobuf[samp + 0];
    int orig_r0 = audiobuf[samp + 1];
    int orig_l1 = audiobuf[samp + 2];
    int orig_r1 = audiobuf[samp + 3];

    audio_level_in = maxi(audio_level_in, maxi(maxi(abs(orig_l0), abs(orig_r0)), maxi(abs(orig_l1), abs(orig_r1))));

    // we leave a little head room after soft clip for the wet signal
    int dry_l0, dry_r0, dry_l1, dry_r1, dry_lm1, dry_rm1, dry_l5, dry_r5, dry_lm05, dry_rm05;
    static int orig_lm1 = 0, orig_rm1 = 0; // previous sample, for upsampling
    static decimator2x ldec, rdec, ldec2, rdec2;

    if (wavefold) {
      if (drive_q12 >= 32768) {
        // insane drive levels! lets not overflow eh...
        int drive_q8 = drive_q12 >> 4;
        dry_lm05 = (soft_clip2(half(orig_lm1 + orig_l0) * drive_q8 >> 8) /*+ bl0*/) * 3 >> 2;
        dry_rm05 = (soft_clip2(half(orig_rm1 + orig_r0) * drive_q8 >> 8) /*+ br0*/) * 3 >> 2;
        dry_l0 = (soft_clip2(orig_l0 * drive_q8 >> 8) /*+ bl0*/) * 3 >> 2;
        dry_r0 = (soft_clip2(orig_r0 * drive_q8 >> 8) /*+ br0*/) * 3 >> 2;
        dry_l5 = (soft_clip2(half(orig_l0 + orig_l1) * drive_q8 >> 8) /*+ bl0*/) * 3 >> 2;
        dry_r5 = (soft_clip2(half(orig_r0 + orig_r1) * drive_q8 >> 8) /*+ br0*/) * 3 >> 2;
        dry_l1 = (soft_clip2(orig_l1 * drive_q8 >> 8) /*+ bl1*/) * 3 >> 2;
        dry_r1 = (soft_clip2(orig_r1 * drive_q8 >> 8) /*+ br1*/) * 3 >> 2;
      } else {
        dry_lm05 = (soft_clip2(half(orig_lm1 + orig_l0) * drive_q12 >> 12) /*+ bl0*/) * 3 >> 2;
        dry_rm05 = (soft_clip2(half(orig_rm1 + orig_r0) * drive_q12 >> 12) /*+ br0*/) * 3 >> 2;
        dry_l0 = (soft_clip2(orig_l0 * drive_q12 >> 12) /*+ bl0*/) * 3 >> 2;
        dry_r0 = (soft_clip2(orig_r0 * drive_q12 >> 12) /*+ br0*/) * 3 >> 2;
        dry_l5 = (soft_clip2(half(orig_l0 + orig_l1) * drive_q12 >> 12) /*+ bl0*/) * 3 >> 2;
        dry_r5 = (soft_clip2(half(orig_r0 + orig_r1) * drive_q12 >> 12) /*+ br0*/) * 3 >> 2;
        dry_l1 = (soft_clip2(orig_l1 * drive_q12 >> 12) /*+ bl1*/) * 3 >> 2;
        dry_r1 = (soft_clip2(orig_r1 * drive_q12 >> 12) /*+ br1*/) * 3 >> 2;
      }
    } else {
      if (drive_q12 >= 32768) {
        // insane drive levels! lets not overflow eh...
        int drive_q8 = drive_q12 >> 4;
        dry_lm05 = (soft_clip(half(orig_lm1 + orig_l0) * drive_q8 >> 8) /*+ bl0*/) * 3 >> 2;
        dry_rm05 = (soft_clip(half(orig_rm1 + orig_r0) * drive_q8 >> 8) /*+ br0*/) * 3 >> 2;
        dry_l0 = (soft_clip(orig_l0 * drive_q8 >> 8) /*+ bl0*/) * 3 >> 2;
        dry_r0 = (soft_clip(orig_r0 * drive_q8 >> 8) /*+ br0*/) * 3 >> 2;
        dry_l5 = (soft_clip(half(orig_l0 + orig_l1) * drive_q8 >> 8) /*+ bl0*/) * 3 >> 2;
        dry_r5 = (soft_clip(half(orig_r0 + orig_r1) * drive_q8 >> 8) /*+ br0*/) * 3 >> 2;
        dry_l1 = (soft_clip(orig_l1 * drive_q8 >> 8) /*+ bl1*/) * 3 >> 2;
        dry_r1 = (soft_clip(orig_r1 * drive_q8 >> 8) /*+ br1*/) * 3 >> 2;
      } else {
        dry_lm05 = (soft_clip(half(orig_lm1 + orig_l0) * drive_q12 >> 12) /*+ bl0*/) * 3 >> 2;
        dry_rm05 = (soft_clip(half(orig_rm1 + orig_r0) * drive_q12 >> 12) /*+ br0*/) * 3 >> 2;
        dry_l0 = (soft_clip(orig_l0 * drive_q12 >> 12) /*+ bl0*/) * 3 >> 2;
        dry_r0 = (soft_clip(orig_r0 * drive_q12 >> 12) /*+ br0*/) * 3 >> 2;
        dry_l5 = (soft_clip(half(orig_l0 + orig_l1) * drive_q12 >> 12) /*+ bl0*/) * 3 >> 2;
        dry_r5 = (soft_clip(half(orig_r0 + orig_r1) * drive_q12 >> 12) /*+ br0*/) * 3 >> 2;
        dry_l1 = (soft_clip(orig_l1 * drive_q12 >> 12) /*+ bl1*/) * 3 >> 2;
        dry_r1 = (soft_clip(orig_r1 * drive_q12 >> 12) /*+ br1*/) * 3 >> 2;
      }
    }

    orig_lm1 = orig_l1;
    orig_rm1 = orig_r1;

    dry_l0 = decimate2x(&ldec, dry_lm05, dry_l0);
    dry_l1 = decimate2x(&ldec, dry_l5, dry_l1);
    dry_r0 = decimate2x(&rdec, dry_rm05, dry_r0);
    dry_r1 = decimate2x(&rdec, dry_r5, dry_r1);
    int dry_l = decimate2x(&ldec2, dry_l0, dry_l1);
    int dry_r = decimate2x(&rdec2, dry_r0, dry_r1);

    int delay_return_l = 0, delay_return_r = 0;
    int delay_send_l, delay_send_r, delay_tap_l, delay_tap_r;

    // int totlevel = 1;
    static int delay_time_scale_q4_smooth2 = 1000;
    delay_time_scale_q4_smooth2 += make_lpf_delta(delay_time_scale_q4, delay_time_scale_q4_smooth2, 7);
    // if (samp == 0)
    //   debug_log("%d <- %d <- %d\n", delay_time_scale_q4_smooth2, delay_time_scale_q4, delay_time);
#ifdef ENABLE_DELAY
    for (int i = 0; i < num_delay_taps; i++) {
      int delay_time_q8 =
          (delay_time_scale_q4_smooth2 * delay_tap_times_q12[i] + 16) >> 5; // scaled so that 12+12-1=23 bits, = 15+8
      if (delay_time_q8 < 512)
        delay_time_q8 = 512;
      if (delay_time_q8 > 32767 * 256)
        delay_time_q8 = 32767 * 256;
      int readpos_q8 = (delaypos_q8 - delay_time_q8) & (32768 * 256 - 1);
      int readpos = readpos_q8 >> 8;
      int readpos_frac = readpos_q8 & 255;
      int dr_l0 = delay_buf[readpos][0], dr_l1 = delay_buf[(readpos + 1) & 32767][0];
      delay_tap_l = dr_l0 + ((dr_l1 - dr_l0) * readpos_frac >> 8);
      delay_return_l += delay_tap_l * delay_tap_levels_q12[i];
#ifdef PINGPONG_IS_DIFFERENT_DELAY_LENGTH
      if (pingpong) {
        int delay_time_q8_pingpong = (delay_time_q8 * 3) >> 2;
        readpos_q8 = (delaypos_q8 - delay_time_q8_pingpong) & (32768 * 256 - 1);
        readpos = readpos_q8 >> 8;
        readpos_frac = readpos_q8 & 255;
      }
#endif
      int dr_r0 = delay_buf[readpos][1], dr_r1 = delay_buf[(readpos + 1) & 32767][1];
      delay_tap_r = dr_r0 + ((dr_r1 - dr_r0) * readpos_frac >> 8);
      delay_return_r += delay_tap_r * delay_tap_levels_q12[i];
    }
    /*if (all_tap_delay || 1) {
      // all delay taps! TODO divide by total
      delay_send_l = delay_return_l / totlevel;
      delay_send_r = delay_return_r / totlevel;
    } else */
    {
      // take the last tap as the delay send
      delay_send_l = delay_tap_l;
      delay_send_r = delay_tap_r;
    }
    delay_return_l /= 1 << 11; // *2 - because we are gonna halve the write level!
    delay_return_r /= 1 << 11;

    // rotate the stereo image
    int delay_write_l = (delay_send_l * delay_fb_cos - delay_send_r * delay_fb_sin);
    int delay_write_r = (delay_send_l * delay_fb_sin + delay_send_r * delay_fb_cos);
#ifndef PINGPONG_IS_DIFFERENT_DELAY_LENGTH
    // halve the write level!
    if (pingpong) {
      delay_write_l += (dry_l + dry_r) * delay_send / 2;
    } else
#endif
    {
      delay_write_l += dry_l * delay_send;
      delay_write_r += dry_r * delay_send;
    }
    delay_write_l /= 1 << 12;
    delay_write_r /= 1 << 12;
    static int l_dc, r_dc;
    l_dc += make_lpf_delta((delay_write_l << 8), l_dc, 11);
    r_dc += make_lpf_delta((delay_write_r << 8), r_dc, 11);
    delay_write_l -= l_dc / 256;
    delay_write_r -= r_dc / 256;

    ///////////////////
    // partial tape speed writer

    static int write_accum_l = 0, write_accum_r = 0;

    static int tape_speed_smooth_q16_2 = 65536;
    tape_speed_smooth_q16_2 += make_lpf_delta(tape_speed_smooth_q16, tape_speed_smooth_q16_2, 7);
    int tape_speed_q8 = (tape_speed_smooth_q16_2 + 128) / 256;
    // debug_log("tape -> %d - %d\n", tape_speed_q8, slider_speed);

    int old_write_pos = delaypos_q8;
    int old_delaypos_int = (delaypos_q8 >> 8) & 32767;
    delaypos_q8 += tape_speed_q8;
    int final_delaypos_int = (delaypos_q8 >> 8) & 32767;
    while (old_delaypos_int != final_delaypos_int) {
      // finish off the current sample
      int amount = 256 - (old_write_pos & 255);
      int wl = write_accum_l + amount * delay_write_l;
      int wr = write_accum_r + amount * delay_write_r;
      write_accum_l = 0;
      write_accum_r = 0;
      delay_buf[old_delaypos_int][0] = soft_clip(wl / 256);
      delay_buf[old_delaypos_int][1] = soft_clip(wr / 256);
      old_delaypos_int = (old_delaypos_int + 1) & 32767;
      old_write_pos += amount;
    }
    // now write the partial sample
    int amount = delaypos_q8 - old_write_pos;
    // assert(amount >= 0 && amount < 256);
    write_accum_l += amount * delay_write_l;
    write_accum_r += amount * delay_write_r;
    delaypos_q8 &= (32768 * 256 - 1);
#endif
    int wetl = delay_return_l;
    int wetr = delay_return_r;
#ifdef ENABLE_REVERB
    //////////////////////////// ok reverb time
    do_reverb((dry_l + wetl) * reverb_send >> 14, (dry_r + wetr) * reverb_send >> 14, reverb_decay, &wetl, &wetr);
#endif
    /// upsample and mix and clip
    static int wet_interpl = 0;
    static int wet_interpr = 0;
    int wet_l0 = (wetl + wet_interpl) >> 1;
    int wet_r0 = (wetr + wet_interpr) >> 1;
    int wet_l1 = wet_interpl = wetl;
    int wet_r1 = wet_interpr = wetr;

    int l0 = (dry_l0 * dry_level + orig_l0 * orig_level + wet_l0 * wet_level) >> 12;
    int r0 = (dry_r0 * dry_level + orig_r0 * orig_level + wet_r0 * wet_level) >> 12;
    int l1 = (dry_l1 * dry_level + orig_l1 * orig_level + wet_l1 * wet_level) >> 12;
    int r1 = (dry_r1 * dry_level + orig_r1 * orig_level + wet_r1 * wet_level) >> 12;
    // ok final limiter
    int level = maxi(abs(l0), maxi(abs(r0), maxi(abs(l1), maxi(32767, abs(r1)))));
    level *= 64;
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

    if (dj) {
      // dj
      if (dj > 0) {
        int blendy = clampi(dj, 0, 512);
        int fc, a1, a2, a3;
        fc = clampi(dj / 4, 0, 1024);
        a1 = svf_a1[fc];
        a2 = svf_a2[fc];
        a3 = svf_a3[fc];
        l0 = svf_step(&svf_l, l0, a1, a2, a3, 1, blendy);
        l1 = svf_step(&svf_l, l1, a1, a2, a3, 1, blendy);
        r0 = svf_step(&svf_r, r0, a1, a2, a3, 1, blendy);
        r1 = svf_step(&svf_r, r1, a1, a2, a3, 1, blendy);
      } else {
        int fc, a1, a2, a3;
        int blendy = clampi(-dj, 0, 512);
        fc = clampi((4096 + dj) / 4, 0, 1024);
        a1 = svf_a1[fc];
        a2 = svf_a2[fc];
        a3 = svf_a3[fc];
        l0 = svf_step(&svf_l, l0, a1, a2, a3, 0, blendy);
        l1 = svf_step(&svf_l, l1, a1, a2, a3, 0, blendy);
        r0 = svf_step(&svf_r, r0, a1, a2, a3, 0, blendy);
        r1 = svf_step(&svf_r, r1, a1, a2, a3, 0, blendy);
      }
    }
    int vol = (out_level * 2 * UNITY_GAIN_FACTOR_Q8 * (32768 / 256)) / (limit_level / 64);
    l0 = soft_clip(l0 * vol >> 12);
    r0 = soft_clip(r0 * vol >> 12);
    l1 = soft_clip(l1 * vol >> 12);
    r1 = soft_clip(r1 * vol >> 12);

    audiobuf[samp + 0] = l0;
    audiobuf[samp + 1] = r0;
    audiobuf[samp + 2] = l1;
    audiobuf[samp + 3] = r1;

    rv = vol;
  }
  return rv;
}
