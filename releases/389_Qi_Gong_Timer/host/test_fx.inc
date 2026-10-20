// =============================================================================
//  test_fx.inc : stage 2 tests (effects) and audio demos.  Included by test_render.cpp.
//  AI-assisted (Claude / Anthropic).
// =============================================================================

// ---- stand-in sounds for the player's Workshop patch (host only, floats are fine here) ----
// "chime" = a struck, slightly inharmonic high tone (oscillator 1 -> filter -> slope);
// "bell"  = a low gong-like tone with several decaying partials (oscillator 2 -> ... -> slope).
static double Strike(int kind, double t)
{
	if (t < 0) return 0;
	static const double cf[3] = {1.0, 2.756, 5.404}, ca[3] = {1.0, 0.35, 0.12}, cd[3] = {2.2, 1.0, 0.5};
	static const double bf[5] = {1.0, 2.01, 2.76, 4.07, 5.43}, ba[5] = {1.0, 0.55, 0.40, 0.22, 0.12}, bd[5] = {8.0, 5.0, 3.5, 2.4, 1.6};
	double att = std::min(1.0, t / (kind == 0 ? 0.003 : 0.006)), s = 0;
	if (kind == 0) for (int i = 0; i < 3; ++i) s += ca[i] * std::exp(-t / cd[i]) * std::sin(6.283185307 * 880.0 * cf[i] * t);
	else for (int i = 0; i < 5; ++i) s += ba[i] * std::exp(-t / bd[i]) * std::sin(6.283185307 * 147.0 * bf[i] * t + i);
	return att * s * (kind == 0 ? 1050.0 : 900.0);
}
struct Hit { double t; int kind; };
static std::vector<int32_t> Render(const std::vector<Hit>& hits, int kind, double seconds)
{
	std::vector<int32_t> v((size_t)(seconds * kSampleRate), 0);
	for (size_t n = 0; n < v.size(); ++n)
	{
		double t = (double)n / kSampleRate, s = 0;
		for (auto& h : hits) if (h.kind == kind && t >= h.t && t - h.t < 40) s += Strike(kind, t - h.t);
		v[n] = (int32_t)std::lrint(std::max(-2047.0, std::min(2047.0, s)));
	}
	return v;
}

static void WriteWav(const char* path, const std::vector<float>& mono)
{
	FILE* f = std::fopen(path, "wb"); if (!f) return;
	uint32_t n = (uint32_t)mono.size(), bytes = n * 2, sr = 48000, br = 96000, c16 = 16, chunk = 36 + bytes;
	uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
	std::fwrite("RIFF", 1, 4, f); std::fwrite(&chunk, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f); std::fwrite(&c16, 4, 1, f);
	std::fwrite(&pcm, 2, 1, f); std::fwrite(&ch, 2, 1, f); std::fwrite(&sr, 4, 1, f); std::fwrite(&br, 4, 1, f);
	std::fwrite(&align, 2, 1, f); std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&bytes, 4, 1, f);
	for (float x : mono) { int16_t s = (int16_t)std::lrint(std::max(-32767.0f, std::min(32767.0f, x))); std::fwrite(&s, 2, 1, f); }
	std::fclose(f);
}

// ---- unit-level runner: one FxChannel with fixed parameters (exactly as the engine drives it) ----
static std::vector<float> RunChannel(const FxParams& p, const std::vector<int32_t>& in, size_t total, int32_t* peak = nullptr, int32_t* clamps = nullptr, int32_t* knee = nullptr)
{
	Sim::ClearFxMemory();
	InitTables(); InitFxTables();                                      // (normally done by Engine::Init)
	FxChannel ch; ch.Init(0, p);
	ch.SetKeeper(false);                                               // (these measure the effects; the keeper only opens on pulses)
	std::vector<float> wet(total);
	for (size_t n = 0; n < total; ++n)
	{
		if ((n & 31) == 6) ch.Wander();
		int32_t x = n < in.size() ? in[n] : 0;
		wet[n] = (float)ch.Process(x, (n & 1) == 0, p) / (float)(1 << kFxQ);   // Q6 -> 12-bit units
	}
	if (peak) *peak = ch.peak_;
	if (clamps) *clamps = ch.clampHits_;
	if (knee) *knee = ch.kneeHits_;
	return wet;
}
static double Rms(const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b && i < v.size(); ++i) s += (double)v[i] * (double)v[i]; return std::sqrt(s / (double)std::max<size_t>(1, b - a)); }
static double RmsI(const std::vector<int32_t>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b && i < v.size(); ++i) s += (double)v[i] * v[i]; return std::sqrt(s / (double)std::max<size_t>(1, b - a)); }
static double Db(double r) { return 20.0 * std::log10(std::max(r, 1e-9)); }
// RT60 from the Schroeder backward-integrated energy (T30: -5 dB to -35 dB, doubled)
static double Rt60(const std::vector<float>& w)
{
	std::vector<double> e(w.size() + 1, 0.0);
	for (size_t i = w.size(); i-- > 0;) e[i] = e[i + 1] + (double)w[i] * (double)w[i];
	if (e[0] <= 0) return 0;
	double t5 = -1, t35 = -1;
	for (size_t i = 0; i < w.size(); ++i)
	{
		double db = 10.0 * std::log10(std::max(e[i] / e[0], 1e-30));
		if (t5 < 0 && db <= -5) t5 = (double)i / kSampleRate;
		if (t35 < 0 && db <= -35) { t35 = (double)i / kSampleRate; break; }
	}
	return (t5 < 0 || t35 < 0) ? -1 : 2.0 * (t35 - t5);
}
static double LastNonZero(const std::vector<float>& w) { for (size_t i = w.size(); i-- > 0;) if (w[i] != 0.0f) return (double)i / kSampleRate; return 0; }
// brightness proxy: fs/(2 pi) * rms(diff)/rms(x)  (a spectral-centroid-like frequency)
static double Brightness(const std::vector<float>& w, size_t a, size_t b)
{
	double sx = 0, sd = 0;
	for (size_t i = a + 1; i < b && i < w.size(); ++i) { sx += (double)w[i] * (double)w[i]; double d = (double)w[i] - (double)w[i - 1]; sd += d * d; }
	return sx > 0 ? 48000.0 / 6.283185307 * std::sqrt(sd / sx) : 0;
}

// ---- engine-level helper: set an effect page's X and Y the way a person does (soft pickup needs a pass-through) ----
static void SetFx(Sim& s, Page pg, int32_t xv, int32_t yv)
{
	s.SwitchTo(1, 50);
	s.Turn(s.in.main, pg == PG_REVERB ? 150 : pg == PG_MIX ? 2048 : 3950, 300, 100);
	s.Turn(s.in.x, 0, 200); s.Turn(s.in.x, 4095, 400); s.Turn(s.in.x, xv, 300, 50);
	s.Turn(s.in.y, 0, 200); s.Turn(s.in.y, 4095, 400); s.Turn(s.in.y, yv, 300, 50);
	s.Turn(s.in.main, pg == PG_MIX ? 1200 : 2048, 200, 50);          // park Main between pages
}

// ----------------------------------------------------------------------------------------------------------
static void TestFxDry()
{
	std::printf("[fxdry] both mix knobs at zero (keeper off): the output is EXACTLY the input\n");
	Sim s; s.Boot();
	s.eng.SetKeeper(false, false);                                     // (this test is about the effect mix; the keeper has its own tests)
	SetFx(s, PG_MIX, 0, 0);
	s.RunSec(0.2);
	std::vector<Hit> h = {{0.2, 0}, {2.0, 0}, {3.0, 1}};
	auto a = Render(h, 0, 8), b = Render(h, 1, 8);
	size_t diff = 0;
	for (size_t n = 0; n < a.size(); ++n) { s.in.audio1 = a[n]; s.in.audio2 = b[n]; s.Step(); diff += (s.out.audio1 != a[n]) + (s.out.audio2 != b[n]); }
	CHECK(diff == 0 && s.eng.FxNow().revGain == 0 && s.eng.FxNow().delGain == 0, "dry must be exact (%zu samples differ)", diff);
	std::printf("    8 s of chime + bell: %zu samples differ  %s\n", diff, diff == 0 ? "OK" : "WRONG");
}

static void TestFxReverb()
{
	InitTables(); InitFxTables();
	std::printf("[fxreverb] impulse responses: decay time (RT60) per X position, darkness per Y, tails reach exact silence\n");
	// "interrupted noise": 100 ms of full-scale noise, then measure the decay of the tail that follows
	std::vector<int32_t> imp((size_t)(kSampleRate / 10)); { uint32_t r = 7; for (auto& v : imp) { r = r * 1664525u + 1013904223u; v = (int32_t)(r >> 20) - 2048; } }
	auto tail = [](const std::vector<float>& w) { return std::vector<float>(w.begin() + kSampleRate / 10, w.end()); };
	auto low = [](const std::vector<float>& w) {                    // < ~300 Hz (two one-poles): how long the body of the sound rings
		std::vector<float> o(w.size()); double a = 0, b = 0, c = 1.0 - std::exp(-6.283185307 * 300.0 / 48000.0);
		for (size_t i = 0; i < w.size(); ++i) { a += c * ((double)w[i] - a); b += c * (a - b); o[i] = (float)b; }
		return o; };
	bool ok = true;
	std::printf("    %-22s %8s %9s %9s %13s %12s\n", "setting", "target", "RT60 all", "RT60 low", "exact zero", "brightness");
	const int32_t xs[5] = {0, 1024, 2048, 3072, 4095};
	for (int32_t x : xs)
	{
		FxParams p = FxTargets(x, (4095 - kDefRevBright), 4095, 0, 2048, 0);
		double target = (double)kRevTimeMinS * std::pow((double)kRevTimeMaxS / (double)kRevTimeMinS, x / 4095.0);
		auto w = RunChannel(p, imp, (size_t)((target * 3.0 + 8.0) * kSampleRate));
		auto tw = tail(w);
		double rt = Rt60(tw), rl = Rt60(low(tw)), z = LastNonZero(w);
		bool silent = z < (double)w.size() / kSampleRate - 1.0;
		ok = ok && silent && rt > 0;
		char lab[64]; std::snprintf(lab, sizeof lab, "time X=%d, dark def.", x);
		std::printf("    %-22s %7.1fs %8.1fs %8.1fs %11.1fs %9.0f Hz  %s\n", lab, target, rt, rl, z, Brightness(tw, 14400, 62400), silent ? "OK" : "NOT SILENT");
	}
	const int32_t ys[3] = {0, 2048, 4095};
	for (int32_t y : ys)
	{
		FxParams p = FxTargets(2600, y, 4095, 0, 2048, 0);
		auto w = RunChannel(p, imp, (size_t)(40 * kSampleRate));
		auto tw = tail(w);
		double z = LastNonZero(w);
		char lab[64]; std::snprintf(lab, sizeof lab, "time def., dark Y=%d", y);
		std::printf("    %-22s %8s %8.1fs %8.1fs %11.1fs %9.0f Hz\n", lab, "", Rt60(tw), Rt60(low(tw)), z, Brightness(tw, 14400, 62400));
		ok = ok && z < 39.0;
	}
	CHECK(ok, "reverb impulse responses");
}

static void TestFxDelay()
{
	InitTables(); InitFxTables();
	std::printf("[fxdelay] echo times, repeats, and silence at maximum feedback\n");
	std::vector<int32_t> imp = {2000, 2000};
	bool ok = true;
	const int32_t xs[3] = {0, 2048, 4095};
	for (int32_t x : xs)
	{
		FxParams p = FxTargets(2048, 2048, 0, 4095, x, 4095);
		double ms = (double)kDelMinMs * std::pow((double)kDelMaxMs / (double)kDelMinMs, x / 4095.0);
		auto w = RunChannel(p, imp, (size_t)(90 * kSampleRate));
		size_t first = 0; float pk = 0;                                 // first echo = the biggest peak in the first window
		for (size_t i = 0; i < (size_t)(ms * 48 * 1.5); ++i) if (std::fabs(w[i]) > pk) { pk = std::fabs(w[i]); first = i; }
		size_t second = 0; float pk2 = 0;
		for (size_t i = first + (size_t)(ms * 24); i < first + (size_t)(ms * 72) && i < w.size(); ++i) if (std::fabs(w[i]) > pk2) { pk2 = std::fabs(w[i]); second = i; }
		double z = LastNonZero(w);
		double err = (double)first / 48.0 - ms;
		bool good = std::fabs(err) < 1.5 && z < 89.0 && pk2 < pk;
		ok = ok && good;
		std::printf("    X=%4d: echo at %7.1f ms (target %6.1f), 2nd at %7.1f ms, 2nd/1st %5.1f dB (max feedback 93%% + darkening), silent after %5.1f s  %s\n",
			x, first / 48.0, ms, second / 48.0, Db(pk2 / pk), z, good ? "OK" : "WRONG");
	}
	CHECK(ok, "delay");
}

static void TestFxStable()
{
	InitTables(); InitFxTables();
	std::printf("[fxstable] worst case: longest, brightest reverb + 93%% feedback, full-scale tones/noise, then silence\n");
	FxParams p = FxTargets(4095, 0, 4095, 4095, 2048, 4095);
	bool ok = true;
	const double freqs[6] = {55, 147, 440, 880, 2500, 7000};
	for (double f : freqs)
	{
		std::vector<int32_t> in((size_t)(15 * kSampleRate));
		for (size_t n = 0; n < in.size(); ++n) in[n] = (int32_t)(2047.0 * std::sin(6.283185307 * f * (double)n / kSampleRate));
		int32_t peak = 0, clamps = 0, knee = 0;
		auto w = RunChannel(p, in, (size_t)(140 * kSampleRate), &peak, &clamps, &knee);
		double z = LastNonZero(w);
		bool good = z < 139.0 && clamps == 0 && peak > kFdnClamp / 100;     // (peak > 1%: the reverb really received the tone)
		ok = ok && good;
		std::printf("    %5.0f Hz full scale 15 s: reverb peak %4.1f%% of its limit, clamps %d; delay soft-saturated on %6d ticks; silent %5.1f s after  %s\n",
			f, 100.0 * peak / kFdnClamp, clamps, knee, z - 15.0, good ? "OK" : "WRONG");
	}
	{
		std::vector<int32_t> in((size_t)(15 * kSampleRate)); uint32_t r = 1;
		for (auto& v : in) { r = r * 1664525u + 1013904223u; v = (int32_t)(r >> 20) - 2048; }
		int32_t peak = 0, clamps = 0, knee = 0;
		auto w = RunChannel(p, in, (size_t)(100 * kSampleRate), &peak, &clamps, &knee);
		double z = LastNonZero(w);
		ok = ok && z < 99.0 && clamps == 0;
		std::printf("    white noise full scale 15 s: reverb peak %4.1f%% of limit, clamps %d; delay soft-saturated on %d ticks; silent %5.1f s after  %s\n", 100.0 * peak / kFdnClamp, clamps, knee, z - 15.0, (z < 99 && clamps == 0) ? "OK" : "WRONG");
	}
	{
		// DC: a constant full-scale offset into the effects (the inputs pass DC) with the longest reverb
		std::vector<int32_t> in((size_t)(20 * kSampleRate), 2047);
		int32_t peak = 0, clamps = 0, knee = 0;
		auto w = RunChannel(p, in, (size_t)(140 * kSampleRate), &peak, &clamps, &knee);
		double z = LastNonZero(w);
		double late = 0;                                               // the slow part: 1-second averages of the wet output
		for (size_t s0 = 2 * kSampleRate; s0 + kSampleRate <= (size_t)(20 * kSampleRate); s0 += kSampleRate / 4)
		{ double m = 0; for (size_t i = s0; i < s0 + kSampleRate; ++i) m += (double)w[i]; late = std::max(late, std::fabs(m / kSampleRate)); }
		ok = ok && clamps == 0 && z < 139.0 && late < 2.0;
		std::printf("    DC step: largest 1-second average of the wet output 2-20 s after the step (sub-audio drift): %.2f (12-bit units)  %s\n", late, late < 2.0 ? "OK" : "WRONG");
		std::printf("    DC offset full scale 20 s: reverb peak %4.1f%% of limit, clamps %d; silent %5.1f s after  %s\n", 100.0 * peak / kFdnClamp, clamps, z - 20.0, (clamps == 0 && z < 79) ? "OK" : "WRONG");
	}
	// through the whole engine: the output limiter keeps everything inside 12 bits
	Sim s; s.Boot(); s.eng.SetKeeper(false, false);                    // (these tests are about the effects, fed without pulses)
	SetFx(s, PG_REVERB, 4095, 4095); SetFx(s, PG_DELAY, 2048, 4095); SetFx(s, PG_MIX, 4095, 4095);   // (longest, brightest)
	int32_t mx = 0;
	for (int n = 0; n < 15 * kSampleRate; ++n) { s.in.audio1 = s.in.audio2 = (n / 54) % 2 ? 2047 : -2047; s.Step(); mx = std::max<int32_t>(mx, std::abs(s.out.audio1)); }
	CHECK(ok && s.audioOk && mx <= 2047, "stability");
	std::printf("    whole engine, full-scale square into both inputs, everything at maximum: output peak %d of 2047 (soft limiter)  %s\n", mx, s.audioOk ? "OK" : "WRONG");
}

static void TestFxChannels()
{
	std::printf("[fxchannels] the two chains never mix\n");
	Sim s; s.Boot(); s.eng.SetKeeper(false, false);
	SetFx(s, PG_MIX, 4095, 4095);
	auto a = Render({{0.1, 0}, {1.0, 0}}, 0, 10);
	size_t leak = 0;
	for (size_t n = 0; n < a.size(); ++n) { s.in.audio1 = a[n]; s.in.audio2 = 0; s.Step(); leak += s.out.audio2 != 0; }
	Sim u; u.Boot(); u.eng.SetKeeper(false, false);
	SetFx(u, PG_MIX, 4095, 4095);
	auto b = Render({{0.1, 1}}, 1, 10);
	size_t leak2 = 0;
	for (size_t n = 0; n < b.size(); ++n) { u.in.audio2 = b[n]; u.in.audio1 = 0; u.Step(); leak2 += u.out.audio1 != 0; }
	CHECK(leak == 0 && leak2 == 0, "channels leak");
	std::printf("    chime only into In 1: Out 2 non-zero samples %zu;  bell only into In 2: Out 1 non-zero samples %zu  %s\n", leak, leak2, (leak + leak2) == 0 ? "OK" : "WRONG");
}

static void TestFxPages()
{
	std::printf("[fxpages] page zones, soft pickup, and no effect changes from set-up or dead zones\n");
	Sim s; s.Boot();
	s.in.main = 150; s.in.x = 500; s.in.y = 3900; s.RunSec(0.5);     // reverb page; X/Y far from the stored values
	FxParams before = s.eng.FxNow();
	s.Turn(s.in.x, 900, 300, 100); s.Turn(s.in.y, 3500, 300, 100);    // moves that do not reach the stored values
	bool held = s.eng.FxNow().revFb == before.revFb && s.eng.FxNow().revDamp == before.revDamp && !s.eng.FxSetting(FK_REV_TIME).engaged;
	s.Turn(s.in.x, 3500, 600, 100);                                      // passes through the stored time (2600): picks up
	bool caught = s.eng.FxSetting(FK_REV_TIME).engaged && s.eng.FxSetting(FK_REV_TIME).value == 3500;
	// dead zone between pages
	s.Turn(s.in.main, 1200, 300, 100);
	Page dead = s.eng.GetPage();
	int32_t v0 = s.eng.FxSetting(FK_REV_TIME).value;
	s.Turn(s.in.x, 100, 400, 100); s.Turn(s.in.y, 100, 400, 100);
	bool deadOk = dead == PG_NONE && s.eng.FxSetting(FK_REV_TIME).value == v0;
	// set-up mode (Z up) never touches the effects
	int32_t snap[FK_COUNT]; for (int i = 0; i < FK_COUNT; ++i) snap[i] = s.eng.FxSetting(i).value;
	s.SwitchTo(2, 100);
	s.Turn(s.in.main, 0, 300); s.Turn(s.in.x, 4095, 300); s.Turn(s.in.y, 4095, 300); s.Turn(s.in.main, 4095, 300, 100);
	bool setupOk = true; for (int i = 0; i < FK_COUNT; ++i) setupOk = setupOk && s.eng.FxSetting(i).value == snap[i];
	// a tap on Z down does NOT re-arm an engaged pickup
	s.SwitchTo(1, 100); s.Turn(s.in.main, 3950, 300, 100);            // delay page
	s.Turn(s.in.x, 0, 300); s.Turn(s.in.x, 4095, 300); s.Turn(s.in.x, 2000, 300, 100);   // pick up the echo time
	s.Tap(); s.RunSec(0.2);
	s.Turn(s.in.x, 2100, 200, 100);
	bool tapOk = s.eng.FxSetting(FK_DEL_TIME).value == 2100;
	// page signs, by row in the order of Main's travel: reverb = top, mix = middle, delay = bottom (and only that row)
	bool ledOk = true;
	const int32_t pos[3] = {150, 2048, 3950};
	for (int p = 0; p < 3; ++p)
	{
		s.Turn(s.in.main, 1000 + 1000 * p, 200, 50);                    // (a dead zone first, so the page is entered fresh)
		s.Turn(s.in.main, pos[p], 200, 0); s.RunSec(0.3);
		for (int i = 0; i < 6; ++i) ledOk = ledOk && ((i / 2 == p) ? s.out.led[i] > 1000 : s.out.led[i] == 0);
	}
	bool ok = held && caught && deadOk && setupOk && tapOk && ledOk;
	CHECK(ok, "fx pages: held %d caught %d dead %d setup %d tap %d led %d", held, caught, deadOk, setupOk, tapOk, ledOk);
	std::printf("    not yet passed through: held; passed through: follows; dead zone: no change; set-up: no change; after a tap: still follows; page signs: reverb top row, mix middle row, delay bottom row  %s\n",
		ok ? "OK" : "WRONG");
}

static void TestFxClicks()
{
	std::printf("[fxclicks] turning every effect knob while a steady tone plays: no clicks\n");
	auto run = [](bool sweep) {
		Sim s; s.Boot(); s.eng.SetKeeper(false, false);
		SetFx(s, PG_DELAY, 2048, 2400); SetFx(s, PG_MIX, 3000, 3000); SetFx(s, PG_REVERB, 2600, 2600);
		s.in.main = 150; s.RunSec(0.5);
		double worst = 0; double prev1 = 0, prev2 = 0;
		int N = 24 * kSampleRate;
		for (int n = 0; n < N; ++n)
		{
			s.in.audio1 = (int32_t)(900.0 * std::sin(6.283185307 * 220.0 * n / kSampleRate));
			if (sweep)
			{
				double ph = (double)n / N;
				int seg = (int)(ph * 3);
				s.in.main = seg == 0 ? 150 : seg == 1 ? 2048 : 3950;
				double lfo = 0.5 - 0.5 * std::cos(6.283185307 * ph * 6);
				s.in.x = (int32_t)(lfo * 4095); s.in.y = (int32_t)((1 - lfo) * 4095);
			}
			s.Step();
			double y = s.out.audio1;
			if (n > 2) worst = std::max(worst, std::fabs(y - 2 * prev1 + prev2));
			prev2 = prev1; prev1 = y;
		}
		return worst;
	};
	double base = run(false), sw = run(true);
	CHECK(sw < base * 3.0 + 20, "clicks: %.0f vs %.0f", sw, base);
	std::printf("    largest sample-to-sample 'kink' (2nd difference): still %.0f, while sweeping all six knobs %.0f (a click would be hundreds+)  %s\n",
		base, sw, sw < base * 3.0 + 20 ? "OK" : "CLICKS?");
}

static void TestFxLevels()
{
	InitTables(); InitFxTables();
	std::printf("[fxlevels] how loud the reverb is next to the dry bell (RMS over 12 s)\n");
	auto bell = Render({{0.05, 1}}, 1, 12);
	auto chime = Render({{0.05, 0}}, 0, 12);
	double dB = RmsI(bell, 0, bell.size()), dC = RmsI(chime, 0, chime.size());
	const int32_t mixes[4] = {4095, 3000, kDefRevMix, 1200};
	for (int32_t m : mixes)
	{
		FxParams p = FxTargets(kDefRevTime, (4095 - kDefRevBright), m, 0, 2048, 0);
		auto wb = RunChannel(p, bell, bell.size());
		auto wc = RunChannel(p, chime, chime.size());
		std::printf("    reverb mix X=%4d%s: bell reverb %+5.1f dB, chime reverb %+5.1f dB relative to the dry sound\n",
			m, m == kDefRevMix ? " (default)" : "          ", Db(Rms(wb, 0, wb.size()) / dB), Db(Rms(wc, 0, wc.size()) / dC));
	}
	const int32_t times[5] = {0, 1024, 2048, 3072, 4095};
	auto bellL = Render({{0.05, 1}}, 1, 40);
	double dBw = RmsI(bellL, 0, 3 * kSampleRate);
	for (int32_t tt : times)
	{
		FxParams p = FxTargets(tt, (4095 - kDefRevBright), 4095, 0, 2048, 0);
		auto wb = RunChannel(p, bellL, bellL.size());
		std::printf("    reverb time X=%4d, full mix: reverb while the bell rings (0-3 s) %+5.1f dB; whole tail energy (40 s) %+5.1f dB\n",
			tt, Db(Rms(wb, 0, 3 * kSampleRate) / dBw), Db(Rms(wb, 0, wb.size()) / RmsI(bellL, 0, bellL.size())));
	}
}

// ---- demos ----
static void Demo(const char* name, std::vector<Hit> hits, double secs, void (*setup)(Sim&))
{
	Sim s; s.Boot(); s.eng.SetKeeper(false, false);                     // (hand-placed hits, no pulses: keeper off for these demos)
	if (setup) setup(s);
	auto a = Render(hits, 0, secs), b = Render(hits, 1, secs);
	std::vector<float> mono(a.size());
	for (size_t n = 0; n < a.size(); ++n) { s.in.audio1 = a[n]; s.in.audio2 = b[n]; s.Step(); mono[n] = 0.7f * 16.0f * (float)(s.out.audio1 + s.out.audio2); }
	char p[256]; std::snprintf(p, sizeof p, "out/%s.wav", name); WriteWav(p, mono);
	std::printf("    wrote %s (%.0f s)\n", p, secs);
}
static void SetDry(Sim& s) { SetFx(s, PG_MIX, 0, 0); }
static void SetLongDark(Sim& s) { SetFx(s, PG_REVERB, 4095, 4095 - 3300); SetFx(s, PG_MIX, 3300, 0); }   // (Y = brightness: low = dark)
static void SetDelay(Sim& s) { SetFx(s, PG_DELAY, 2900, 2300); SetFx(s, PG_MIX, kDefRevMix, 2700); }

static void Demos()
{
	std::printf("[demo] rendering audio demos (stand-in chime and bell through the real engine)\n");
	std::vector<Hit> seq = {{0.5, 0}, {6.5, 0}, {12.5, 0}, {18.5, 1}};
	Demo("demo1a_dry", seq, 34, SetDry);
	Demo("demo1b_default", seq, 34, nullptr);
	Demo("demo2_long_dark", seq, 45, SetLongDark);
	Demo("demo3_delay", seq, 38, SetDelay);
	// a real (short) session: the timer's own pulses trigger the stand-in chime and bell
	Sim s; s.Boot(); SetUp(s, 1, 0, 2); s.SwitchTo(1, 100);
	s.Tap();
	std::vector<float> mono; std::vector<Hit> live;
	size_t seen1 = 0, seen2 = 0;
	double t0 = (double)s.t / kSampleRate;
	for (int n = 0; n < 150 * kSampleRate; ++n)
	{
		double t = (double)s.t / kSampleRate;
		double a = 0, b = 0;
		for (auto& h : live) { if (h.kind == 0) a += Strike(0, t - h.t); else b += Strike(1, t - h.t); }
		s.in.audio1 = (int32_t)std::lrint(std::max(-2047.0, std::min(2047.0, a)));
		s.in.audio2 = (int32_t)std::lrint(std::max(-2047.0, std::min(2047.0, b)));
		s.Step();
		if (s.rise1.size() > seen1) { live.push_back({(double)s.rise1.back() / kSampleRate, 0}); seen1 = s.rise1.size(); }
		if (s.rise2.size() > seen2) { live.push_back({(double)s.rise2.back() / kSampleRate, 1}); seen2 = s.rise2.size(); }
		mono.push_back(0.7f * 16.0f * (float)(s.out.audio1 + s.out.audio2));
	}
	WriteWav("out/demo4_session_1min_x2.wav", mono);
	std::printf("    wrote out/demo4_session_1min_x2.wav (150 s; pulses at");
	for (auto& h : live) std::printf(" %.1f s", h.t - t0);
	std::printf(")\n");
}

static void TestMainZones()
{
	std::printf("[mainzones] where Main selects each effect page (slow sweep up, then down; owner, 19 Oct: only near the ends)\n");
	const char* names[4] = {"nothing", "REVERB", "MIX", "DELAY"};
	bool ok = true;
	for (int dir = 0; dir < 2; ++dir)
	{
		Sim s; s.Boot(); s.in.main = dir ? 4095 : 0; s.RunSec(0.3);
		int last = -1, from = 0;
		std::printf("    %s:", dir ? "turning anticlockwise" : "turning clockwise    ");
		for (int k = 0; k <= 4095; k += 4)
		{
			int v = dir ? 4095 - k : k;
			s.in.main = v; s.Run(40);
			int pg = (int)s.eng.GetPage();
			if (pg != last) { if (last >= 0) std::printf(" %s %d-%d,", names[last], dir ? v : from, dir ? from : v); last = pg; from = v; }
		}
		std::printf(" %s to the end\n", names[last]);
		// requirements: reverb only in the first ~8%, mix only in a ~8% window at 12 o'clock, delay only in the last ~8%
		for (int v : {500, 900, 1300, 1700, 2400, 2800, 3200, 3600})
		{
			Sim u; u.Boot(); u.in.main = dir ? 4095 : 0; u.RunSec(0.3); u.Turn(u.in.main, v, 300, 100);
			ok = ok && u.eng.GetPage() == PG_NONE;
		}
		for (int v : {150, 2048, 3950})
		{
			Sim u; u.Boot(); u.in.main = dir ? 4095 : 0; u.RunSec(0.3); u.Turn(u.in.main, v, 300, 100);
			ok = ok && u.eng.GetPage() == (v < 1000 ? PG_REVERB : v > 3000 ? PG_DELAY : PG_MIX);
		}
	}
	CHECK(ok, "Main zones");
}
