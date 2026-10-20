// =============================================================================
//  test_keeper.inc : a PC MODEL of the owner's patch, closing the loop card -> modules -> card, and the
//  silence keeper's tests and demos.  Included by test_render.cpp.  AI-assisted (Claude / Anthropic).
//
//  THE MODEL (host only, doubles are fine here; after Robopera's §8, adapted to the owner's patch):
//    Pulse Out -> Slope (Falling mode: jumps up on the rising edge, then falls) -> filter FM;
//    square oscillator -> 4-pole low-pass, cutoff 20 Hz + 5 kHz x env^2 -> Audio In.
//    Plus what the hardware adds: a LEAK of the oscillator past the closed filter, faint 50 Hz mains hum and hiss.
//  It shows timing, levels and silence honestly; it does NOT sound like the real modules.
// =============================================================================

struct ModelVoice
{
	double freq = 880, decay = 0.5, level = 1100;      // pitch, Slope fall time constant (s), peak level (12-bit units)
	double leak = 3.0, hum = 1.5, hiss = 1.5;           // what leaks between notes (12-bit units): ~ -55 dBFS together
	double ph = 0, env = 0, lp[4] = {0, 0, 0, 0}, t = 0;
	bool last = false;
	uint32_t rng = 12345;
	double Step(bool pulse)
	{
		if (pulse && !last) env = 1.0;                    // Falling mode: the rising edge starts the fall from the top
		last = pulse;
		env *= std::exp(-1.0 / (decay * 48000.0));
		ph += freq / 48000.0; if (ph >= 1) ph -= 1;
		double sq = ph < 0.5 ? 1.0 : -1.0;
		double fc = 20.0 + 5000.0 * env * env, c = 1.0 - std::exp(-6.283185307 * fc / 48000.0);
		double x = sq;
		for (int k = 0; k < 4; ++k) { lp[k] += c * (x - lp[k]); x = lp[k]; }
		rng = rng * 1664525u + 1013904223u;
		double noise = ((double)(rng >> 8) / 8388608.0 - 1.0) * hiss;
		t += 1.0 / 48000.0;
		return level * x + leak * sq + hum * std::sin(6.283185307 * 50.0 * t) + noise;
	}
};

// One run of a real session (the card's own timer) with the model patched in.  Records everything per sample.
struct LoopRun
{
	std::vector<int16_t> in1, out1, in2, out2;
	std::vector<float> env1;
	std::vector<uint64_t> alerts1;                       // sample index of each Pulse 1 / 2 rising edge
	uint64_t t0 = 0;
};
static LoopRun RunLoop(int keeper, double leakMul, int minutes, int intervals, double tailSec, int32_t revMix = kDefRevMix)
{
	// keeper: 0 = off, 1 = on (effect send), 2 = on (send + dry)
	Sim s; s.Boot();
	s.eng.SetKeeper(keeper != 0, keeper == 2);
	if (revMix != kDefRevMix) SetFx(s, PG_MIX, revMix, 0);
	SetUp(s, minutes, 0, intervals); s.SwitchTo(1, 100);
	ModelVoice chime, bell;
	bell.freq = 147; bell.decay = 2.0; bell.level = 1300; bell.rng = 999;
	chime.leak *= leakMul; chime.hum *= leakMul; chime.hiss *= leakMul;
	bell.leak *= leakMul; bell.hum *= leakMul; bell.hiss *= leakMul;
	LoopRun r;
	s.Tap(); r.t0 = s.t;
	size_t N = (size_t)((10.0 + 60.0 * minutes * intervals + tailSec) * kSampleRate);
	bool p1 = false, p2 = false;
	for (size_t n = 0; n < N; ++n)
	{
		double a = chime.Step(p1), b = bell.Step(p2);   // the modules hear last sample's pulses
		s.in.audio1 = (int32_t)std::lrint(std::max(-2047.0, std::min(2047.0, a)));
		s.in.audio2 = (int32_t)std::lrint(std::max(-2047.0, std::min(2047.0, b)));
		s.Step();
		if (s.out.pulse1 && !p1) r.alerts1.push_back(n);
		p1 = s.out.pulse1; p2 = s.out.pulse2;
		r.in1.push_back((int16_t)s.in.audio1); r.out1.push_back(s.out.audio1);
		r.in2.push_back((int16_t)s.in.audio2); r.out2.push_back(s.out.audio2);
		r.env1.push_back((float)chime.env);
	}
	return r;
}
// RMS (12-bit units) of out - in ("wet"), or of out, over samples chosen by a predicate
template <typename F> static double RmsWhere(const LoopRun& r, bool wetOnly, F pick)
{
	double s = 0; size_t c = 0;
	for (size_t n = 0; n < r.out1.size(); ++n) if (pick(n)) { double v = wetOnly ? (double)(r.out1[n] - r.in1[n]) : (double)r.out1[n]; s += v * v; ++c; }
	return c ? std::sqrt(s / (double)c) : 0.0;
}
static double DbFs(double rms) { return rms > 0 ? 20.0 * std::log10(rms / 2048.0) : -999.0; }

static void TestKeeper()
{
	std::printf("[keeper] the model patch (square -> filter, Falling Slope from the card's pulses, leak + hum + hiss), 1:00 x 3 session\n");
	// windows on channel 1: "quiet" = the chime has died away (model env < 1e-3) AND at least 25 s since its last alert
	// (so the reverb's own tail of the chime is long gone: what remains is only what the leak keeps alive);
	// "during" = the chime is sounding (env > 0.05).  The first 0.5 s is skipped (start-up).
	auto quiet = [](const LoopRun& r) {
		return [&r](size_t n) {
			if (n < 24000 || r.env1[n] >= 1e-3f) return false;
			uint64_t last = 0; bool any = false; for (auto a : r.alerts1) if (a <= n) { last = a; any = true; }
			return any && n - last > (uint64_t)(25 * kSampleRate);
		}; };
	auto during = [](const LoopRun& r) { return [&r](size_t n) { return n >= 24000 && r.env1[n] > 0.05f; }; };

	std::printf("    %-34s %16s %16s %18s %16s\n", "", "wet when quiet", "Out 1 when quiet", "wet during chime", "change vs off");
	bool ok = true;
	for (double mul : {1.0, 4.0})
	{
		LoopRun off = RunLoop(0, mul, 1, 3, 40), on = RunLoop(1, mul, 1, 3, 40), dry = RunLoop(2, mul, 1, 3, 40);
		double wOff = RmsWhere(off, true, during(off));
		struct Row { const char* name; LoopRun* r; };
		Row rows[3] = {{"keeper OFF", &off}, {"keeper on (effect send)", &on}, {"keeper on (send + dry)", &dry}};
		for (auto& row : rows)
		{
			double wq = RmsWhere(*row.r, true, quiet(*row.r)), oq = RmsWhere(*row.r, false, quiet(*row.r)), wd = RmsWhere(*row.r, true, during(*row.r));
			char lab[64]; std::snprintf(lab, sizeof lab, "%s, leak x%.0f", row.name, mul);
			std::printf("    %-34s %11.1f dBFS %11.1f dBFS %13.1f dBFS %13.2f dB\n", lab, DbFs(wq), DbFs(oq), DbFs(wd), DbFs(wd) - DbFs(wOff));
		}
		// the requirements
		double wqOn = RmsWhere(on, true, quiet(on)), oqDry = RmsWhere(dry, false, quiet(dry));
		double dOn = std::fabs(DbFs(RmsWhere(on, true, during(on))) - DbFs(wOff));
		bool good = wqOn == 0.0 && oqDry == 0.0 && dOn < 0.5 && RmsWhere(off, true, quiet(off)) > 0;
		ok = ok && good;
	}
	CHECK(ok, "keeper: silence between alerts / notes unchanged");

	// dry gating must leave the sounds themselves bit-exact: compare Out 1 with and without dry gating during loud parts
	{
		LoopRun on = RunLoop(1, 1.0, 1, 2, 5), dry = RunLoop(2, 1.0, 1, 2, 5);
		// Where does dry gating start to act on a fading chime?  Track the chime's level as a 10 ms RMS of the input
		// (minus the model's leak/hum/hiss, which we know is ~3.5 units) and find the loudest moment at which any
		// output sample differs from the send-only keeper.
		size_t loud = 0, loudDiff = 0;
		double worstDb = -999;
		const size_t W = 480;
		for (size_t n = 24000 + W; n < on.out1.size(); ++n)
		{
			double e = 0; for (size_t k = n - W; k < n; ++k) e += (double)on.in1[k] * on.in1[k];
			double lev = DbFs(std::sqrt(e / W));
			bool differs = on.out1[n] != dry.out1[n];
			if (on.env1[n] > 0.2f) { ++loud; loudDiff += differs; }
			if (differs && lev > worstDb) worstDb = lev;
			n += 15;                                         // (sample the timeline sparsely: this is a slow measurement)
		}
		CHECK(loudDiff == 0 && worstDb < -36.0, "dry gating: %zu loud samples changed; acts at %.1f dBFS", loudDiff, worstDb);
		std::printf("    dry gating: chime clearly sounding (Slope > 0.2): %zu of %zu samples changed (must be 0); it only starts acting once the chime\n"
		            "    has faded to %.1f dBFS (10 ms RMS of the input; the leak alone is about -56 dBFS)  %s\n",
			loudDiff, loud, worstDb, (loudDiff == 0 && worstDb < -36.0) ? "OK" : "WRONG");
	}
	std::printf("    (quiet = chime faded and >25 s after its alert; 'change vs off' = how much the keeper alters the effect during the chime)\n");
}

static void TestKeeperHand()
{
	std::printf("[keeperhand] sounds the card did not trigger (the Slope's Blip button) stay SILENT, stopped or not (owner, 19 Oct)\n");
	bool ok = true;
	for (int running = 0; running < 2; ++running)
	{
		Sim s; s.Boot();
		if (running) { SetUp(s, 2, 0, 1); s.Tap(); s.RunSec(20); }     // in the middle of a 2-minute interval
		ModelVoice v; v.rng = 77;
		size_t nonzero = 0, sounding = 0;
		for (int n = 0; n < 40 * kSampleRate; ++n)
		{
			bool blip = (n % (10 * kSampleRate)) < 4800 && n > kSampleRate * 2;
			s.in.audio1 = (int32_t)std::lrint(v.Step(blip)); s.in.audio2 = 0;
			s.Step();
			if (v.env > 0.05) { ++sounding; nonzero += s.out.audio1 != 0; }
		}
		ok = ok && nonzero == 0 && s.rise1.size() <= 2;
		std::printf("    %s: hand-triggered chimes (-7 dBFS peaks): %zu of %zu sounding samples reach Out 1  %s\n",
			running ? "during a session, between alerts" : "stopped", nonzero, sounding, nonzero == 0 ? "OK" : "WRONG");
	}
	CHECK(ok, "hand-triggered sounds must stay silent");
	std::printf("    (to hear the voices while tuning: preview mode, whose pulses open the keeper)\n");
}

static void KeeperDemos()
{
	std::printf("[keeperdemo] 1:00 x 2 sessions with the model patch (leak x1), default effects\n");
	for (int k = 0; k < 3; ++k)
	{
		LoopRun r = RunLoop(k, 1.0, 1, 2, 30, 3300);
		std::vector<float> mono(r.out1.size());
		for (size_t n = 0; n < mono.size(); ++n) mono[n] = 0.7f * 16.0f * (float)(r.out1[n] + r.out2[n]);
		const char* names[3] = {"out/demo5a_model_keeper_off.wav", "out/demo5b_model_keeper_send.wav", "out/demo5c_model_keeper_send_and_dry.wav"};
		const char* p = names[k];
		WriteWav(p, mono);
		std::printf("    wrote %s (reverb mix raised to 3300 so the effect is easy to hear)\n", p);
	}
}

// ---- demos of the simplified design (8 Oct): clicks, the double chime, preview mode, all with the model patch ----
struct LoopSim
{
	Sim s; ModelVoice chime, bell; bool p1 = false, p2 = false; std::vector<float> mono;
	LoopSim() { s.Boot(); bell.freq = 147; bell.decay = 2.0; bell.level = 1300; bell.rng = 999; }
	void Step()
	{
		double a = chime.Step(p1 || blip), b = bell.Step(p2);
		s.in.audio1 = (int32_t)std::lrint(std::max(-2047.0, std::min(2047.0, a)));
		s.in.audio2 = (int32_t)std::lrint(std::max(-2047.0, std::min(2047.0, b)));
		s.Step();
		p1 = s.out.pulse1; p2 = s.out.pulse2;
		mono.push_back(0.7f * 16.0f * (float)(s.out.audio1 + s.out.audio2));
	}
	void Run(double sec) { for (int i = 0; i < (int)(sec * kSampleRate); ++i) Step(); }
	void Turn(int32_t& k, int32_t to, double sec) { int32_t from = k; int n = (int)(sec * kSampleRate); for (int i = 1; i <= n; ++i) { k = from + (int32_t)((int64_t)(to - from) * i / n); Step(); } }
	void Press(int ms) { s.in.sw = 0; Run(ms / 1000.0); s.in.sw = 1; }
	bool blip = false;
};
static void SimplifiedDemos()
{
	std::printf("[demo] release demos, with the model patch, current code (only the card's own pulses strike the voices)\n");
	{
		LoopSim L;                                          // a preview chime for scale, then the set-up clicks
		L.Run(0.5);
		for (int k = 0; k < 3; ++k) { L.Press(100); L.Run(0.15); }
		L.Run(1.2); L.Press(150); L.Run(3.0);               // preview: one chime, then out (before its bell)
		L.s.in.sw = 2; L.Run(0.5);
		L.Turn(L.s.in.main, NotchCentre(5, 36) + 120, 0.3); L.Turn(L.s.in.main, NotchCentre(13, 36), 2.5); L.Run(1.0);   // intervals
		L.Turn(L.s.in.x, NotchCentre(5, 37) + 120, 0.3); L.Turn(L.s.in.x, NotchCentre(10, 37), 2.0); L.Run(1.0);          // minutes
		L.Turn(L.s.in.y, 100, 0.3); L.Turn(L.s.in.y, NotchCentre(3, 7), 1.5); L.Run(1.5);                                  // seconds
		WriteWav("out/release_setup_clicks.wav", L.mono);
		std::printf("    wrote out/release_setup_clicks.wav (%.0f s)\n", L.mono.size() / 48000.0);
	}
	{
		LoopSim L; SetUp(L.s, 0, 1, 3); L.s.SwitchTo(1, 100);   // 0:10 x 3
		L.mono.clear();
		L.Press(150); L.Run(55.0);
		WriteWav("out/release_session_demo.wav", L.mono);
		std::printf("    wrote out/release_session_demo.wav (%.0f s): 0:10 x 3, default effects\n", L.mono.size() / 48000.0);
	}
	{
		LoopSim L; L.mono.clear();
		for (int k = 0; k < 3; ++k) { L.Press(100); L.Run(0.15); }
		L.Run(8.5); L.Press(150); L.Run(6.0);
		WriteWav("out/release_preview_mode.wav", L.mono);
		std::printf("    wrote out/release_preview_mode.wav (%.0f s)\n", L.mono.size() / 48000.0);
	}
}

// ---- the owner's report (19 Oct): "a very low whine / drone between chimes, slowly creeping up in pitch, another creeping
// down, looping across the interval" (the card's CV ramps were patched into the oscillators' pitch; the CV outs have since been
// removed).  Kept as a keeper regression test: ANY leak that grows during a session (a pitch knob turned up, a patch change)
// must stay silent between chimes.  Model: the chime oscillator sweeps up 5 octaves over each 2-minute interval and back,
// the bell oscillator over the whole session; the leak past a closed filter grows with pitch.
static LoopRun RunCvPitchLoop(int minutes, int intervals, double tailSec)
{
	Sim s; s.Boot();
	SetUp(s, minutes, 0, intervals); s.SwitchTo(1, 100);
	ModelVoice chime, bell;
	chime.freq = 110; bell.freq = 55; bell.decay = 2.0; bell.level = 1300; bell.rng = 999;
	LoopRun r;
	s.Tap(); r.t0 = s.t;
	size_t N = (size_t)((10.0 + 60.0 * minutes * intervals + tailSec) * kSampleRate);
	bool p1 = false, p2 = false;
	for (size_t n = 0; n < N; ++n)
	{
		double tt = (double)n / kSampleRate - 10.0, iv = 60.0 * minutes;            // (seconds into the session; interval length)
		double o2 = tt < 0 ? 0 : 5.0 * std::fmod(tt, iv) / iv, o1 = tt < 0 ? 0 : 5.0 * tt / (iv * intervals);   // octaves
		chime.freq = 110.0 * std::pow(2.0, o2); bell.freq = 55.0 * std::pow(2.0, o1);
		chime.leak = 3.0 * chime.freq / 110.0; bell.leak = 3.0 * bell.freq / 55.0;   // leaks grow with pitch
		double a = chime.Step(p1), b = bell.Step(p2);
		s.in.audio1 = (int32_t)std::lrint(std::max(-2047.0, std::min(2047.0, a)));
		s.in.audio2 = (int32_t)std::lrint(std::max(-2047.0, std::min(2047.0, b)));
		s.Step();
		if (s.out.pulse1 && !p1) r.alerts1.push_back(n);
		p1 = s.out.pulse1; p2 = s.out.pulse2;
		r.in1.push_back((int16_t)s.in.audio1); r.out1.push_back(s.out.audio1);
		r.in2.push_back((int16_t)s.in.audio2); r.out2.push_back(s.out.audio2);
		r.env1.push_back((float)chime.env);
	}
	return r;
}
static void TestCvWhine()
{
	std::printf("[cvwhine] a leak that grows during a session (oscillators sweeping 5 octaves): is the quiet between chimes silent?\n");
	LoopRun r = RunCvPitchLoop(2, 2, 20);
	// quiet windows on Out 1: the chime has faded (env < 1e-3) and 25 s since its last alert
	auto quiet = [&r](size_t n) {
		if (n < 24000 || r.env1[n] >= 1e-3f) return false;
		uint64_t last = 0; bool any = false; for (auto a : r.alerts1) if (a <= n) { last = a; any = true; }
		return any && n - last > (uint64_t)(25 * kSampleRate); };
	double in = 0, out = 0; size_t c = 0, nonzero = 0;
	for (size_t n = 0; n < r.out1.size(); ++n) if (quiet(n)) { in += (double)r.in1[n] * r.in1[n]; out += (double)r.out1[n] * r.out1[n]; ++c; nonzero += r.out1[n] != 0; }
	double din = c ? DbFs(std::sqrt(in / c)) : -999, dout = c && out > 0 ? DbFs(std::sqrt(out / c)) : -999;
	CHECK(nonzero == 0, "the quiet between chimes is not silent (Out 1 %.1f dBFS, %zu non-zero samples)", dout, nonzero);
	std::printf("    leak arriving at In 1 between chimes: %.1f dBFS; Out 1 between chimes: %s (%zu of %zu samples non-zero)  %s\n",
		din, dout <= -999 ? "exact silence" : (std::to_string(dout).substr(0, 6) + " dBFS").c_str(), nonzero, c, nonzero == 0 ? "OK" : "WRONG");
}

// ---- owner's report (19 Oct): "after exiting preview mode, a hum/thumping... the oscillators and the 'beats' of the FM leaking
// through... only occasionally, and went away entirely during intervals."  The card must be silent before, between and after
// sessions.  Model: a stopped card (also: just after preview mode), the leak of two oscillators beating (FM), 3x the usual level.
static void TestStoppedSilence()
{
	std::printf("[stoppedsilence] stopped card, a beating (FM) leak on both inputs: the outputs must stay silent\n");
	bool ok = true;
	for (int afterPreview = 0; afterPreview < 2; ++afterPreview)
	{
		Sim s; s.Boot();
		double t = 0;
		auto leak = [&t]() {                                       // two leaking tones beating at ~0.7 Hz, swelling and fading
			double a = 9.0 * std::sin(6.283185307 * 147.0 * t) + 9.0 * std::sin(6.283185307 * 147.7 * t);
			return (int32_t)std::lrint(a + 3.0 * std::sin(6.283185307 * 50.0 * t)); };
		if (afterPreview)
		{
			for (int k = 0; k < 3; ++k) { s.Press(100); s.Run(150 * kMs); }
			for (int n = 0; n < 6 * kSampleRate; ++n) { t += 1.0 / kSampleRate; s.in.audio1 = s.in.audio2 = leak(); s.Step(); }
			s.Tap();
		}
		size_t nonzero = 0, total = 0;
		for (int n = 0; n < 60 * kSampleRate; ++n)
		{
			t += 1.0 / kSampleRate; s.in.audio1 = s.in.audio2 = leak(); s.Step();
			if (n > 25 * kSampleRate) { ++total; nonzero += (s.out.audio1 != 0) + (s.out.audio2 != 0); }   // (after any preview tails)
		}
		ok = ok && nonzero == 0;
		std::printf("    %s: %zu of %zu output samples non-zero over the last 35 s (the leak is about -40 dBFS, beating)  %s\n",
			afterPreview ? "just after preview mode" : "stopped since power-up", nonzero, 2 * total, nonzero == 0 ? "OK" : "WRONG");
	}
	CHECK(ok, "the stopped card is not silent");
}
