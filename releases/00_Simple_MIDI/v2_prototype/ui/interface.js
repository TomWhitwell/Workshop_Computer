////////////////////////////////////////////////////////////
// Generic WebMIDI / SysEx plumbing
// Works with any WebInterfaceComputerCard firmware

var midiOutput = null, midiInput = null;
var midiActive = false;

var firmwareConnected = false;
var dMessageTimer = null;
const D_MESSAGE_TIMEOUT = 300; // in ms - disconnect if no D message for this long

async function MIDISetup(interfaceName, onConnect = () => {}, onDisconnect = () => {})
{
	try
	{
		const midi = await navigator.requestMIDIAccess({ sysex: true });
		BindDevices(midi, interfaceName, onConnect, onDisconnect);
		midi.addEventListener('statechange', (e) => {
			BindDevices(midi, interfaceName, onConnect, onDisconnect);
		});
	}
	catch (error)
	{
		document.getElementById('connectedStatus').innerHTML = 'WebMIDI error ('+error+')';
	}
}

function BindDevices(midi, interfaceName, onConnect, onDisconnect)
{
	if (midiActive)
	{
		const foundInput = [...midi.inputs.values()].some(input =>
			input.name.includes(interfaceName) && input.state === "connected"
		);
		const foundOutput = [...midi.outputs.values()].some(output =>
			output.name.includes(interfaceName) && output.state === "connected"
		);
		if (!foundInput || !foundOutput)
		{
			midiActive = false;
			midiOutput = undefined;
			midiInput = undefined;
			onDisconnect();
		}
	}
	else
	{
		midiInput = [...midi.inputs.values()].find(input =>
			input.name.includes(interfaceName) && input.state === "connected");
		midiOutput = [...midi.outputs.values()].find(output =>
			output.name.includes(interfaceName) && output.state === "connected");

		if (midiInput && midiOutput)
		{
			midiInput.onmidimessage = HandleMIDI;
			midiActive = true;
			onConnect();
		}
		else
		{
			midiOutput = undefined;
			midiInput = undefined;
		}
	}
}

function HandleMIDI(event)
{
	const data = Array.from(event.data);
	if (data.length > 3
		&& data[0] === 0xF0
		&& data[1] === 0x7D
		&& data[data.length - 1] === 0xF7)
	{
		ProcessIncomingSysEx(data.slice(2, data.length - 1));
	}
}

function Assert7BitArray(arr, name)
{
	if (!Array.isArray(arr))
		throw new Error(`${name} must be an array`);
	arr.forEach((v, i) => {
		if (v < 0 || v > 0x7F)
			throw new Error(`${name}[${i}] is invalid (${v}). Must be integer 0–127.`);
	});
}

function SendSysEx(dataBytes)
{
	if (midiActive)
	{
		Assert7BitArray(dataBytes, 'dataBytes');
		midiOutput.send([0xF0, 0x7D, ...dataBytes, 0xF7]);
	}
}

////////////////////////////////////////////////////////////
// App-specific code

const NOTE_NAMES = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];

window.onload = function()
{
	updateCalUI();
	updateStartButtons();
	updateInitialInstruction();
	MIDISetup("MTMComputer", ConnectToComputer, DisconnectFromComputer);
}

let standaloneMode = false;

function showMenu()
{
	standaloneMode = false;
	stopWobble();
	sendCV(0, 0);
	sendCV(1, 0);
	sendInputMV(0);

	// Reset all calibration state
	calMode           = null;
	calState          = CAL.IDLE;
	calChannel        = 0;
	combinedPhase     = 0;
	calData           = [[], []];
	freqBuf           = [[], []];
	baseHz            = [0, 0];
	cvTestPhase       = 0;
	cvTestWarning     = '';
	freqStableSince   = null;
	calInputIndex     = 0;
	inputCalData      = [[], [], [], []];
	inputSweepCVVals  = [];
	inputSweepStepIdx = 0;
	inputSweepSettle  = 0;
	inputSweepBuf     = [];
	inputLastGoodRaw  = null;
	detectPhase       = 0;
	detectSettle      = 0;
	detectBuf         = [];
	detectLowADC      = null;
	liveTrackStep     = 0;
	liveTrackCount    = 0;
	liveTrackBuf      = [];
	liveTrackMeas     = [];
	liveTrackCVOrder  = [];
	liveTrackLastMeas = [];
	liveAlpha2        = null;
	calStepIndex      = 0;
	calCVValues       = [];
	calStepCount      = 0;
	calStepBuffer     = [];
	trimmerStep       = 0;
	trimmerCount      = 0;
	trimmerBuf        = [[], []];
	trimmerMeas       = [[], []];
	trimmerAlpha      = [null, null];
	trimmerOrder      = [];

	// Reset per-mode UI panels to initial state
	for (const m of Object.keys(CAL_STEP_INFO))
	{
		const stepsEl  = document.getElementById(`calSteps-${m}`);
		const progEl   = document.getElementById(`calProgress-${m}`);
		const graphEl  = document.getElementById(`graphAtRes-${m}`);
		const eepromEl = document.getElementById(`eepromPanel-${m}`);
		const nextEl   = document.getElementById(`calNextPanel-${m}`);
		const stEl     = document.getElementById(`standalonePanel-${m}`);
		const contEl   = document.getElementById(`continuePanel-${m}`);
		if (stepsEl)  stepsEl.innerHTML      = '';
		if (progEl)   progEl.textContent     = '';
		if (graphEl)  graphEl.style.display  = 'none';
		if (eepromEl) eepromEl.style.display = 'none';
		if (nextEl)   nextEl.style.display   = 'none';
		if (stEl)     stEl.style.display     = 'none';
		if (contEl)   contEl.style.display   = 'none';
	}

	// Reset inputs back button label
	const backBtnInputs = document.getElementById('backBtn-inputs');
	if (backBtnInputs) backBtnInputs.textContent = '← Cancel';

	// Restore start buttons (hidden when their calibration mode is started)
	for (const id of ['startBtn-combined', 'startBtn-trusted', 'startBtn-trimmer', 'startBtn-inputs'])
	{
		const btn = document.getElementById(id);
		if (btn) btn.style.display = '';
	}

	document.getElementById('menu').style.display = '';
	document.getElementById('calSection-combined').style.display = 'none';
	document.getElementById('calSection-trusted').style.display = 'none';
	document.getElementById('calSection-trimmer').style.display = 'none';
	document.getElementById('calSection-inputs').style.display = 'none';

	updateInitialInstruction();
	updateStartButtons();
}

function showSection(mode)
{
	document.getElementById('menu').style.display = 'none';
	document.getElementById('calSection-combined').style.display = 'none';
	document.getElementById('calSection-trusted').style.display = 'none';
	document.getElementById('calSection-trimmer').style.display = 'none';
	document.getElementById('calSection-inputs').style.display = 'none';
	document.getElementById(`calSection-${mode}`).style.display = 'flex';
	updateInitialInstruction();
}

function startStandalone()
{
	standaloneMode = true;
	showSection('combined');
}

function continueStandaloneToInputs()
{
	saveCalToEEPROM();
	showMenu();                                              // reset all state and panels
	document.getElementById('menu').style.display = 'none'; // don't show the menu
	showSection('inputs');
}

function ConnectToComputer()
{
	updateConnectionStatus();
}

function DisconnectFromComputer()
{
	if (dMessageTimer) { clearTimeout(dMessageTimer); dMessageTimer = null; }
	firmwareConnected = false;
	updateConnectionStatus();
}

function onDMessage()
{
	if (dMessageTimer) clearTimeout(dMessageTimer);
	dMessageTimer = setTimeout(() => {
		dMessageTimer = null;
		firmwareConnected = false;
		updateConnectionStatus();
	}, D_MESSAGE_TIMEOUT);
	if (!firmwareConnected)
	{
		firmwareConnected = true;
		updateConnectionStatus();
	}
}

function updateConnectionStatus()
{
	const el = document.getElementById('connectedStatus');
	if (firmwareConnected)
		el.innerHTML = '<span style="color:green">Connected</span>';
	else if (midiActive)
		el.innerHTML = '<span style="color:#aaa">Wrong card, or not<br>in calibration mode</span>';
	else
		el.innerHTML = '<span style="color:red">Disconnected</span>';
	updateStartButtons();
	updateInitialInstruction();
}

function updateInitialInstruction()
{
	if (calMode !== null) return;  // updateCalUI() owns the text once started
	let text;
	if (firmwareConnected)
		text = "Remove all patch cables from the Computer, then press <b>Start Calibration</b> to begin.";
	else if (midiActive)
		text = 'Wrong card connected &mdash; connect the Workshop System Computer.';
	else
		text = 'Connect the Workshop System Computer via USB<br>and reset the \'Simple MIDI\' card while holding the Z switch down.';
	for (const id of ['calInstruction-combined', 'calInstruction-trusted', 'calInstruction-trimmer', 'calInstruction-inputs'])
	{
		const el = document.getElementById(id);
		if (el) el.innerHTML = text;
	}
}

function updateStartButtons()
{
	document.querySelectorAll('button[onclick*="startCalibration"]').forEach(btn => {
		btn.disabled = !firmwareConnected;
	});
}

function ProcessIncomingSysEx(dataBytes)
{
	const str = String.fromCharCode(...dataBytes);

	// D|<freq0>|<freq1>|<audio0>|<audio1>|<cv0>|<cv1>|<sig0>|<sig1>  - combined 20ms update
	// freq0/freq1: average period in 256ths of a sample (0 = no signal detected)
	// audio0/audio1/cv0/cv1: float averages of raw ADC readings (-2048..2047 range)
	// sig0/sig1: 1 if audio channel has seen |sample| > 500 since last reset, else 0
	let m = str.match(/^D\|(-?[\d.]+)\|(-?[\d.]+)\|(-?[\d.]+)\|(-?[\d.]+)\|(-?[\d.]+)\|(-?[\d.]+)\|(\d)\|(\d)/);
	if (m)
	{
		onDMessage();
		const period0 = parseFloat(m[1]);
		const period1 = parseFloat(m[2]);
		if (period0 > 0) updateDisplay1((48000 * 256) / period0);
		if (period1 > 0) updateDisplay2((48000 * 256) / period1);
		inputSweepTick([parseFloat(m[3]), parseFloat(m[4]), parseFloat(m[5]), parseFloat(m[6])]);
		if (m[7] === '1') audio1SigSeen = true;
		if (m[8] === '1') audio2SigSeen = true;
		if (calState === CAL.WAIT_AUDIO1 || calState === CAL.WAIT_AUDIO2)
			handleConnection(conn1, conn2, conn3, conn4);
		return;
	}

	// K|<a1>|<a2>|<cv1>|<cv2>|  - jack connection status
	m = str.match(/^K\|(\d)\|(\d)\|(\d)\|(\d)\|/);
	if (m) { handleConnection(m[1] === '1', m[2] === '1', m[3] === '1', m[4] === '1'); return; }

	// S|  - EEPROM write confirmation from firmware
	m = str.match(/^S\|/);
	if (m)
	{
		const el = document.getElementById(`eepromStatus-${calMode || 'workshop'}`);
		if (el) el.textContent = 'Saved \u2714 Restart device to apply new calibration.';
		return;
	}
}

////////////////////////////////////////////////////////////
// Frequency helpers

function hzToNote(hz)
{
	const midi    = 12 * Math.log2(hz / 440) + 69;
	const rounded = Math.round(midi);
	const cents   = Math.round((midi - rounded) * 100);
	const octave  = Math.floor(rounded / 12) - 1;
	const name    = NOTE_NAMES[((rounded % 12) + 12) % 12];
	const centsStr = cents >= 0 ? `+${cents}` : `${cents}`;
	return `${name}${octave} ${centsStr} cents`;
}

function avg(arr) { return arr.reduce((a, b) => a + b, 0) / arr.length; }

////////////////////////////////////////////////////////////
// Calibration - shared constants

const CAL = {
	IDLE:         'IDLE',
	WAIT_AUDIO1:  'WAIT_AUDIO1',
	WAIT_AUDIO2:  'WAIT_AUDIO2',
	WAIT_FREQ:    'WAIT_FREQ',
	WAIT_CV1:     'WAIT_CV1',
	WAIT_CV2:     'WAIT_CV2',
	TUNING:       'TUNING',
	LIVE_TRACK:   'LIVE_TRACK',    // osctracking: fast continuous measurement while trimmer is adjusted
	WAIT_RECABLE: 'WAIT_RECABLE',  // combined: waiting for user to move CV cables between phases
	WAIT_INPUT:   'WAIT_INPUT',    // inputs mode: waiting for user to patch and press start
	SWEEP_INPUT:  'SWEEP_INPUT',   // inputs mode: actively sweeping CV and recording ADC
	DONE:         'DONE'
};

// Per-mode step info.
// match() returns true when that step is the currently active one.
// text may be a string or a function returning a string (for dynamic content).
const CAL_STEP_INFO = {
	workshop: [
		{ label: '1', text: 'Connect the <b>top oscillator</b> sine output to <b>Audio In 1</b>.',
		  match: () => calState === CAL.WAIT_AUDIO1 },
		{ label: '2', text: 'Connect the <b>bottom oscillator</b> sine output to <b>Audio In 2</b>.',
		  match: () => calState === CAL.WAIT_AUDIO2 },
		{ label: '3', text: 'Use the oscillator knobs to set both oscillators to around <b>261 Hz (C4)</b>. Waiting for both to be stable in range&hellip;',
		  match: () => calState === CAL.WAIT_FREQ },
		{ label: '4', text: 'Connect <b>CV Out 1</b> to the top oscillator pitch input&hellip;',
		  match: () => calState === CAL.WAIT_CV1 },
		{ label: '5', text: 'Connect <b>CV Out 2</b> to the bottom oscillator pitch input&hellip;',
		  match: () => calState === CAL.WAIT_CV2 },
		{ label: '6', text: 'Calibrating &mdash; do not adjust anything.',
		  match: () => calState === CAL.TUNING },
		{ label: '\u2713', text: 'Calibration complete.',
		  match: () => calState === CAL.DONE },
	],
	trimmer: [
		{ label: '1', text: 'Connect the <b>top oscillator</b> sine output to <b>Audio In 1</b>.',
		  match: () => calState === CAL.WAIT_AUDIO1 },
		{ label: '2', text: 'Connect the <b>bottom oscillator</b> sine output to <b>Audio In 2</b>.',
		  match: () => calState === CAL.WAIT_AUDIO2 },
		{ label: '3', text: 'Use the oscillator knobs to set both oscillators to around <b>261 Hz (C4)</b>. Make sure the FM knobs are fully anti-clockwise and no cables are connected to the pitch inputs. Waiting for both to be stable in range&hellip;',
		  match: () => calState === CAL.WAIT_FREQ },
		{ label: '4', text: 'Connect <b>CV Out 1</b> to the <b>top oscillator</b> pitch input&hellip;',
		  match: () => calState === CAL.WAIT_CV1 },
		{ label: '5', text: 'Connect <b>CV Out 2</b> to the <b>bottom oscillator</b> pitch input&hellip;',
		  match: () => calState === CAL.WAIT_CV2 },
		{ label: '&#8635;', text: () => trimmerInstructionText(),
		  match: () => calState === CAL.LIVE_TRACK },
	],
	trusted: [
		{ label: '1', text: 'Connect the oscillator sine or triangle output to <b>Audio In 1</b>.',
		  match: () => calState === CAL.WAIT_AUDIO1 },
		{ label: '2', text: 'Adjust the oscillator to around <b>261 Hz (C4)</b>. Waiting for stable signal&hellip;',
		  match: () => calState === CAL.WAIT_FREQ },
		{ label: '3', text: 'Connect <b>CV Out 1</b> to the oscillator 1V/oct input&hellip;',
		  match: () => calState === CAL.WAIT_CV1 },
		{ label: '4', text: 'Calibrating CV Out 1 &mdash; do not adjust anything.',
		  match: () => calState === CAL.TUNING && calChannel === 0 },
		{ label: '5', text: 'Connect <b>CV Out 2</b> to the oscillator 1V/oct input&hellip;',
		  match: () => calState === CAL.WAIT_CV2 },
		{ label: '6', text: 'Calibrating CV Out 2 &mdash; do not adjust anything.',
		  match: () => calState === CAL.TUNING && calChannel === 1 },
		{ label: '\u2713', text: 'Calibration complete.',
		  match: () => calState === CAL.DONE },
	],
	inputs: [
		{ label: '1',
		  match: () => (calState === CAL.WAIT_INPUT || calState === CAL.SWEEP_INPUT) && calInputIndex === 0,
		  text: () => inputStepText(0) },
		{ label: '2',
		  match: () => (calState === CAL.WAIT_INPUT || calState === CAL.SWEEP_INPUT) && calInputIndex === 1,
		  text: () => inputStepText(1) },
		{ label: '3',
		  match: () => (calState === CAL.WAIT_INPUT || calState === CAL.SWEEP_INPUT) && calInputIndex === 2,
		  text: () => inputStepText(2) },
		{ label: '4',
		  match: () => (calState === CAL.WAIT_INPUT || calState === CAL.SWEEP_INPUT) && calInputIndex === 3,
		  text: () => inputStepText(3) },
		{ label: '\u2713', text: 'Calibration complete.',
		  match: () => calState === CAL.DONE },
	],
	osctracking: [
		{ label: '1', text: 'Connect the <b>top oscillator</b> sine output to <b>Audio In 1</b>.',
		  match: () => calState === CAL.WAIT_AUDIO1 },
		{ label: '2', text: 'Connect the <b>bottom oscillator</b> sine output to <b>Audio In 2</b>.',
		  match: () => calState === CAL.WAIT_AUDIO2 },
		{ label: '3', text: 'Use the oscillator knobs to set both oscillators to around <b>261 Hz (C4)</b>. Waiting for both to be stable in range&hellip;',
		  match: () => calState === CAL.WAIT_FREQ },
		{ label: '4', text: 'Connect <b>CV Out 1</b> to the <b>top oscillator</b> 1V/oct input&hellip;',
		  match: () => calState === CAL.WAIT_CV1 },
		{ label: '5', text: 'Measuring top oscillator tracking &mdash; do not adjust anything.',
		  match: () => calState === CAL.TUNING && calChannel === 0 },
		{ label: '6', text: 'Disconnect CV Out 1 from the top oscillator and connect it to the <b>bottom oscillator</b> 1V/oct input&hellip;',
		  match: () => calState === CAL.WAIT_CV2 },
		{ label: '7', text: 'Measuring bottom oscillator tracking &mdash; do not adjust anything.',
		  match: () => calState === CAL.TUNING && calChannel === 1 },
		{ label: '\u2713', text: () => oscTrackingResultText(),
		  match: () => calState === CAL.DONE || calState === CAL.LIVE_TRACK },
	],
	combined: [
		{ label: '1', text: 'Connect the <b>top oscillator</b> sine output to <b>Audio In 1</b> on the Computer.<img class="instrimg" src="images/toposc_audio1.png">',
		  match: () => calState === CAL.WAIT_AUDIO1 },
		{ label: '2', text: 'Connect the <b>bottom oscillator</b> sine output to <b>Audio In 2</b> on the Computer.<img class="instrimg" src="images/bottomosc_audio2.png">',
		  match: () => calState === CAL.WAIT_AUDIO2 },
		{ label: '3', text: 'Use the oscillator knobs to set both oscillators to around <b>C4 (261 Hz)</b>.<br><br>Make sure the FM knobs are fully anti-clockwise<br>and no patch cables are connected to the pitch/FM inputs.<br><br>Waiting for both to be stable in range&hellip;',
		  match: () => calState === CAL.WAIT_FREQ && combinedPhase === 0 },
		{ label: '4', text: 'Connect <b>CV Out 1</b> on the Computer to the <b>top oscillator</b> pitch input&hellip;<img class="instrimg" src="images/toposc_cv1.png">',
		  match: () => calState === CAL.WAIT_CV1 && combinedPhase === 0 },
		{ label: '5', text: 'Measuring top oscillator tracking &mdash; do not adjust anything.',
		  match: () => calState === CAL.TUNING && calChannel === 0 && combinedPhase === 0 },
		{ label: '6', text: 'Move <b>CV Out 1</b> to the <b>bottom oscillator</b> pitch input&hellip;<img class="instrimg" src="images/bottomosc_cv1.png">',
		  match: () => calState === CAL.WAIT_CV2 && combinedPhase === 0 },
		{ label: '7', text: () => combinedLiveTrackText(),
		  match: () => calState === CAL.LIVE_TRACK },
		{ label: '8', text: 'Move <b>CV Out 1</b> back to the top oscillator pitch input and connect <b>CV Out 2</b> to the bottom oscillator pitch input instead &hellip;<img class="instrimg" src="images/bothoscs_cv.png">',
		  match: () => calState === CAL.WAIT_RECABLE },
		{ label: '9', text: 'Calibrating &mdash; do not adjust anything.',
		  match: () => calState === CAL.TUNING && combinedPhase === 1 },
		{ label: '\u2713', text: () => standaloneMode
			? 'Calibration complete.<br><br>If you\'re happy with the results, click the button to save them onto the Workshop Computer and continue to input calibration.'
			: 'Calibration complete.<br><br>If you\'re happy with the results, click the button to save them onto the Workshop Computer.',
		  match: () => calState === CAL.DONE && combinedPhase === 1 },
	],
};

////////////////////////////////////////////////////////////
// Oscillator tracking comparison result

function oscTrackingResult()
{
	if (calData[0].length < 2 || calData[1].length < 2) return null;

	const cvs0 = calData[0].map(d => d.cv);
	const l2_0 = calData[0].map(d => Math.log2(d.hz));
	const cvs1 = calData[1].map(d => d.cv);
	const l2_1 = calData[1].map(d => Math.log2(d.hz));

	const reg0 = linReg(cvs0, l2_0);
	const reg1 = linReg(cvs1, l2_1);

	// alpha = effective V/oct coefficient: 1.0 = perfect 1V/oct, range is ~0.891–1.0
	const alpha1 = reg0.slope * CV_TEST_HIGH;
	const alpha2 = reg1.slope * CV_TEST_HIGH;

	// Trimmer range: alpha varies from 82/92 to 1.0 over 30 turns
	const ALPHA_RANGE = 1.0 - 82 / 92;   // 0.108696
	const TURNS_TOTAL = 30;
	const deltaAlpha  = alpha1 - alpha2;  // required change to alpha2 to match alpha1
	const turns       = deltaAlpha / (ALPHA_RANGE / TURNS_TOTAL);

	return { alpha1, alpha2, deltaAlpha, turns };
}

function turnsText(turns)
{
	const quarters = Math.round(Math.abs(turns) * 4);
	const whole    = Math.floor(quarters / 4);
	const frac     = quarters % 4;

	if (whole === 0)
	{
		return ['a tiny amount', 'about a quarter turn', 'about half a turn', 'about three quarters of a turn'][frac];
	}

	const fracStr  = ['', ' and a quarter', ' and a half', ' and three-quarters'][frac];
	const wholeStr = whole === 1 ? 'one' :
	                 whole === 2 ? 'two' :
	                 whole === 3 ? 'three' :
	                 whole === 4 ? 'four' :
	                 whole === 5 ? 'five' :
	                 whole === 6 ? 'six' :
	                 whole === 7 ? 'seven' :
	                 whole === 8 ? 'eight' :
	                 whole === 9 ? 'nine' :
	                 whole === 10 ? 'ten' : `${whole}`;
	return `about ${wholeStr}${fracStr} turn${quarters === 4 ? '' : 's'}`;
}

function oscTrackingResultText()
{
	const r = oscTrackingResult();
	if (!r) return 'Insufficient data.';

	const pctDiff  = (r.alpha2 / r.alpha1 - 1) * 100;
	const absPct   = Math.abs(pctDiff).toFixed(2);
	const absTurns = turnsText(r.turns);

	if (Math.abs(pctDiff) < 0.05)
		return 'Top and bottom oscillators track closely. No trimmer adjustment needed.';

	const overUnder = pctDiff > 0 ? 'over-tracks' : 'under-tracks';
	const dir       = pctDiff > 0 ? 'decrease tracking (anticlockwise)' : 'increase tracking (clockwise)';
	const warning   = Math.abs(r.turns) > 25
		? ' <span style="color:#c04000">(Warning: measured tracking difference between oscillators is suspiciously large. Perhaps you bumped an oscillator pitch knob?)</span>'
		: '';

	return `Bottom oscillator ${overUnder} top by <b>${absPct}%</b> ` +
		`(\u03b1<sub>top</sub>&nbsp;=&nbsp;${r.alpha1.toFixed(3)}, ` +
		`\u03b1<sub>bot</sub>&nbsp;=&nbsp;${r.alpha2.toFixed(3)}). ` +
		`Turn the bottom oscillator trimmer to <b>${dir}</b> ` +
		`by <b>${absTurns}</b>.${warning} ` +
		`Re-run after adjustment to verify.`;
}

function combinedLiveTrackText()
{
	if (liveAlpha2 === null) return 'Measuring bottom oscillator tracking&hellip;';
	if (calData[0].length < 2) return 'Insufficient data.';

	const cvs0   = calData[0].map(d => d.cv);
	const l2_0   = calData[0].map(d => Math.log2(d.hz));
	const alpha1 = linReg(cvs0, l2_0).slope * CV_TEST_HIGH;

	const pctDiff  = (liveAlpha2 / alpha1 - 1) * 100;
	const absPct   = Math.abs(pctDiff).toFixed(2);

	if (Math.abs(pctDiff) < 0.05)
		return 'Top and bottom oscillators track closely.<br><br> Click <b>Continue</b> to proceed to Computer CV out calibration.';

	const overUnder = pctDiff > 0 ? 'over-tracks' : 'under-tracks';
	const dir       = pctDiff > 0 ? 'anticlockwise' : 'clockwise';
	const ALPHA_RANGE = 1.0 - 82 / 92;
	const turns     = Math.abs((alpha1 - liveAlpha2) / (ALPHA_RANGE / 30));
	const warning   = turns > 25
		? ' <span style="color:#c04000">(Warning: measured tracking difference between oscillators is suspiciously large. Perhaps you bumped an oscillator pitch knob?)</span>'
		: '';

	return `Use a small slot-head screwdriver to adjust the bottom oscillator trimmer <b>${dir}</b> by <b>${turnsText(turns)}</b>${warning}, ` +
		`or until the indicator is in the green region.<br><br>Click <b>Continue</b> when matched.`;
}

function remeasureBottomOsc()
{
	if ((calMode !== 'osctracking' && calMode !== 'combined') || (calState !== CAL.DONE && calState !== CAL.LIVE_TRACK)) return;
	liveAlpha2    = null;
	calData[1]    = [];
	calState      = CAL.WAIT_CV2;
	cvTestPhase   = 0;
	cvTestWarning = '';
	sendCV(0, 0);
	updateCalUI();
	drawCalGraphs();
}

function continueToCalibration()
{
	if (calMode !== 'combined' || calState !== CAL.LIVE_TRACK || combinedPhase !== 0) return;
	sendCV(0, 0);
	sendCV(1, 0);
	cvTestPhase   = 0;
	cvTestWarning = '';
	calState = CAL.WAIT_RECABLE;
	updateCalUI();
	drawCalGraphs();
}

function startPhase2()
{
	// Called automatically when CV Out 2 is detected on the bottom oscillator.
	// Both connections are already confirmed (CV Out 1 from osctracking, CV Out 2 just detected),
	// so skip the cvTest confirmation steps and go straight to calibration.
	// baseHz[] is kept from the osctracking phase - oscillators are still at the same pitch.
	combinedPhase = 1;
	calChannel    = 0;
	calState      = CAL.TUNING;
	startCalSweep();
}

function shuffleLiveTrackOrder()
{
	liveTrackCVOrder = LIVE_TRACK_CVS.map((_, i) => i);
	for (let i = liveTrackCVOrder.length - 1; i > 0; i--)
	{
		const j = Math.floor(Math.random() * (i + 1));
		[liveTrackCVOrder[i], liveTrackCVOrder[j]] = [liveTrackCVOrder[j], liveTrackCVOrder[i]];
	}
}

function startLiveTrack()
{
	calState          = CAL.LIVE_TRACK;
	liveTrackStep     = 0;
	liveTrackCount    = 0;
	liveTrackBuf      = [];
	liveTrackMeas     = [];
	liveTrackLastMeas = [];
	liveAlpha2        = null;
	shuffleLiveTrackOrder();
	sendCV(1, CAL_MIN_CV);
	sendCV(0, LIVE_TRACK_CVS[liveTrackCVOrder[0]]);
	updateCalUI();
	drawCalGraphs();
}

function liveTrackTick(hz)
{
	liveTrackCount++;
	if (liveTrackCount <= LIVE_TRACK_SETTLE) return;

	liveTrackBuf.push(hz);
	if (liveTrackBuf.length < LIVE_TRACK_COLLECT) return;

	liveTrackMeas.push({ cv: LIVE_TRACK_CVS[liveTrackCVOrder[liveTrackStep]], hz: avg(liveTrackBuf) });
	liveTrackStep++;
	liveTrackCount = 0;
	liveTrackBuf   = [];

	if (liveTrackStep >= LIVE_TRACK_CVS.length)
	{
		const cvs  = liveTrackMeas.map(d => d.cv);
		const l2s  = liveTrackMeas.map(d => Math.log2(d.hz));
		liveAlpha2        = linReg(cvs, l2s).slope * CV_TEST_HIGH;
		liveTrackLastMeas = [...liveTrackMeas];
		liveTrackStep     = 0;
		liveTrackMeas     = [];
		shuffleLiveTrackOrder();
		updateCalUI();
		drawCalGraphs();
	}

	sendCV(0, LIVE_TRACK_CVS[liveTrackCVOrder[liveTrackStep]]);
}

////////////////////////////////////////////////////////////
// Trimmer adjustment - simultaneous dual-channel live tracking

function shuffleTrimmerOrder()
{
	trimmerOrder = TRIMMER_TRACK_MVS.map((_, i) => i);
	for (let i = trimmerOrder.length - 1; i > 0; i--)
	{
		const j = Math.floor(Math.random() * (i + 1));
		[trimmerOrder[i], trimmerOrder[j]] = [trimmerOrder[j], trimmerOrder[i]];
	}
}

function startTrimmerLiveTrack()
{
	trimmerStep  = 0;
	trimmerCount = 0;
	trimmerBuf   = [[], []];
	trimmerMeas  = [[], []];
	trimmerAlpha = [null, null];
	shuffleTrimmerOrder();
	calState = CAL.LIVE_TRACK;
	const mv = TRIMMER_TRACK_MVS[trimmerOrder[0]];
	sendMV(0, mv);
	sendMV(1, mv);
	updateCalUI();
}

// Called from updateDisplay2 with the current hz from both Audio Ins.
// Both channels are driven by the same mV value, measured simultaneously.
function trimmerLiveTrackTick(hz1, hz2)
{
	trimmerCount++;
	if (trimmerCount <= TRIMMER_SETTLE) return;

	trimmerBuf[0].push(hz1);
	trimmerBuf[1].push(hz2);
	if (trimmerBuf[0].length < TRIMMER_COLLECT) return;

	const mv = TRIMMER_TRACK_MVS[trimmerOrder[trimmerStep]];
	trimmerMeas[0].push({ mv, hz: avg(trimmerBuf[0]) });
	trimmerMeas[1].push({ mv, hz: avg(trimmerBuf[1]) });
	trimmerStep++;
	trimmerCount = 0;
	trimmerBuf   = [[], []];

	if (trimmerStep >= TRIMMER_TRACK_MVS.length)
	{
		for (let ch = 0; ch < 2; ch++)
		{
			const mvs = trimmerMeas[ch].map(d => d.mv);
			const l2s = trimmerMeas[ch].map(d => Math.log2(d.hz));
			trimmerAlpha[ch] = linReg(mvs, l2s).slope * 1000;
		}
		trimmerStep = 0;
		trimmerMeas = [[], []];
		shuffleTrimmerOrder();
		updateCalUI();
	}

	if (trimmerStep < TRIMMER_TRACK_MVS.length)
	{
		const mv = TRIMMER_TRACK_MVS[trimmerOrder[trimmerStep]];
		sendMV(0, mv);
		sendMV(1, mv);
	}
}

////////////////////////////////////////////////////////////
// Sweep parameters (pitch calibration modes)
const CAL_STEPS        = 60;
const CAL_MIN_CV       = -160000;
const CAL_MAX_CV       =  160000;

// Osctracking sweep: 80% of the full range (drop 10% at each end) and half the points
const TRACK_STEPS  = 30;
const TRACK_MIN_CV = -128000;
const TRACK_MAX_CV =  128000;
const CAL_DISCARD      = 4;
const TRACK_DISCARD    = 12;  // longer settle for osctracking (random CV jumps can be large)
const CAL_STABLE_COUNT = 6;
const CAL_STABLE_CENTS = 0.5;
const CAL_TIMEOUT      = 50;

// CV connection test: value expected to shift pitch by ~1 octave
const CV_TEST_HIGH = 43691;

/// Live tracking (osctracking LIVE_TRACK state): 7-point regression in random order
const LIVE_TRACK_CVS     = [-128000, -87382, -43691, 0, 43691, 87382, 128000];
const LIVE_TRACK_SETTLE  = 3;   // D| messages to discard after each CV change
const LIVE_TRACK_COLLECT = 5;   // D| messages to average per point

// Trimmer adjustment: simultaneous dual-channel live tracking using calibrated mV
// Sweep +-3V (6 octaves) in 7 steps; calibrated CV outs give accurate absolute tracking.
const TRIMMER_TRACK_MVS = [-3000, -2000, -1000, 0, 1000, 2000, 3000];
const TRIMMER_SETTLE    = 3;   // D| messages to discard after each CV step
const TRIMMER_COLLECT   = 5;   // D| messages to average per point

// WAIT_FREQ stability parameters
const FREQ_WIN          = 15;
const FREQ_RANGE        = [196, 392];
const FREQ_STABLE_CENTS = 5;

// Input calibration sweep parameters
// CV Out 1 is swept in calibrated millivolts so the regression is directly in ADC/mV,
// removing any dependence on the CV_TEST_HIGH approximation.
const CAL_IN_STEPS   = 100;   // steps across the sweep range
const CAL_IN_SETTLE  = 8;     // readings to discard after each CV change
const CAL_IN_COLLECT = 7;     // readings to average per recorded point
const CAL_IN_MIN_MV  = -5000; // sweep start (mV)
const CAL_IN_MAX_MV  =  5000; // sweep end   (mV)

// Input connection detection: drive CV Out 1 to two known voltages and check
// that the target input's ADC reading changes by the expected amount.
// A cable plugged in at only one end shows no correlated change.
const DETECT_LOW_MV    = -1000; // mV for low test point
const DETECT_HIGH_MV   =  1000; // mV for high test point
const DETECT_SETTLE    = 5;     // D| messages to discard after each CV change (~100 ms)
const DETECT_COLLECT   = 3;     // D| messages to average per point (~60 ms)
const DETECT_THRESHOLD = 300;   // ADC counts; ~2V swing expected to give ~800+ counts

// Input channel metadata
const IN_COLOR  = ['#4b0082', '#c04000', '#007030', '#005090'];
const IN_NAMES  = ['Audio In 1', 'Audio In 2', 'CV In 1', 'CV In 2'];
const IN_IMAGES = ['input_a1.png', 'input_a2.png', 'input_c1.png', 'input_c2.png'];

function inputStepText(i)
{
	if (calState === CAL.SWEEP_INPUT && calInputIndex === i)
		return `Measuring <b>${IN_NAMES[i]}</b> &mdash; do not adjust anything.`;
	return `Connect <b>CV Out 1</b> to <b>${IN_NAMES[i]}</b>.<img class="instrimg" src="images/${IN_IMAGES[i]}">`;
}

////////////////////////////////////////////////////////////
// Calibration - shared mutable state

let calMode       = null;   // 'workshop' | 'trusted' | 'inputs' | 'combined' | null when IDLE
let calState      = CAL.IDLE;
let calChannel    = 0;
let combinedPhase = 0;     // 0 = osctracking phase, 1 = workshop calibration phase

let conn1 = false, conn2 = false, conn3 = false, conn4 = false;
let audio1SigSeen = false; // Audio In 1 has seen a D message value with signal
let audio2SigSeen = false; // Audio In 2 has seen a D message value with signal
let latestHz1 = 0, latestHz2 = 0;

let freqBuf = [[], []];  // rolling Hz windows per channel for WAIT_FREQ
let baseHz  = [0, 0];   // oscillator freq at 0V reference, per channel

// CV test sub-state (pitch calibration modes)
let cvTestPhase   = 0;
let cvTestBuf     = [];
let cvTestBase    = 0;
let cvTestWarning = '';

// Pitch sweep sub-state
let calStepIndex  = 0;
let calCVValues   = [];
let calData       = [[], []];  // [{cv, hz}] per channel
let calStepCount  = 0;
let calStepBuffer = [];

// Live tracking sub-state
let liveTrackStep  = 0;
let liveTrackCount = 0;
let liveTrackBuf   = [];
let liveTrackMeas     = [];   // [{cv, hz}] for current cycle
let liveTrackCVOrder  = [];   // shuffled indices into LIVE_TRACK_CVS for current cycle
let liveTrackLastMeas = [];   // last completed 3-point cycle, for graph overlay
let liveAlpha2        = null; // latest computed alpha2 from 3-point regression

// Trimmer adjustment live-track sub-state
let trimmerStep  = 0;
let trimmerCount = 0;
let trimmerBuf   = [[], []];
let trimmerMeas  = [[], []];  // [{mv, hz}] per channel for the current cycle
let trimmerAlpha = [null, null];
let trimmerOrder = [];

// Input calibration state
let calInputIndex     = 0;          // which input we are currently sweeping (0–3)
let inputCalData      = [[], [], [], []];  // [{cv, adc}] per input channel
let inputSweepCVVals  = [];
let inputSweepStepIdx = 0;
let inputSweepSettle  = 0;          // readings discarded since last CV change
let inputSweepBuf     = [];         // readings accumulated towards next average
let inputLastGoodRaw  = null;       // last accepted raw reading within current step (null = any value ok)

// Input connection detection sub-state (active during WAIT_INPUT)
let detectPhase  = 0;     // 0=start, 1=settling low, 2=collecting low, 3=settling high, 4=collecting high
let detectSettle = 0;
let detectBuf    = [];
let detectLowADC = null;

////////////////////////////////////////////////////////////
// CV output

function sendCV(channel, val)
{
	const str = channel === 0 ? `C|${val}|` : `C2|${val}|`;
	SendSysEx(str.split('').map(c => c.charCodeAt(0)));
}

function sendInputMV(mv)
{
	const str = `M|${mv}|`;
	SendSysEx(str.split('').map(c => c.charCodeAt(0)));
}

// Send calibrated millivolt value to either CV output (M|/M2| commands).
function sendMV(channel, mv)
{
	const str = channel === 0 ? `M|${mv}|` : `M2|${mv}|`;
	SendSysEx(str.split('').map(c => c.charCodeAt(0)));
}

////////////////////////////////////////////////////////////
// Wobble - sweeps CV during WAIT_FREQ so any accidental CV connection
// produces obvious pitch wobble and prevents the oscillator settling in range

let wobbleTimer = null;

function startWobble()
{
	if (wobbleTimer) return;
	wobbleTimer = setInterval(() => {
		if (calState !== CAL.WAIT_FREQ) { stopWobble(); return; }
		const cv = Math.round(80000 * Math.sin(2 * Math.PI * Date.now() / 3000));
		sendCV(0, cv);
		sendCV(1, cv);
	}, 50);
}

function stopWobble()
{
	if (wobbleTimer) { clearInterval(wobbleTimer); wobbleTimer = null; }
	sendCV(0, 0);
	sendCV(1, 0);
}

////////////////////////////////////////////////////////////
// Start / reset

function startCalibration(mode)
{
	freqStableSince = null;
	stopWobble();

	// Reset all other panels to idle appearance
	for (const m of Object.keys(CAL_STEP_INFO))
	{
		if (m === mode) continue;
		const i = document.getElementById(`calInstruction-${m}`);
		if (i) i.innerHTML = 'Press <b>Start Calibration</b> to begin.';
		const s = document.getElementById(`calSteps-${m}`);
		if (s) s.innerHTML = '';
		const p = document.getElementById(`calProgress-${m}`);
		if (p) p.textContent = '';
		const c = document.getElementById(`graphAtRes-${m}`);
		if (c) c.style.display = 'none';
		const e = document.getElementById(`eepromPanel-${m}`);
		if (e) e.style.display = 'none';
		const n = document.getElementById(`calNextPanel-${m}`);
		if (n) n.style.display = 'none';
	}

	calMode       = mode;
	calData       = [[], []];
	freqBuf       = [[], []];
	baseHz        = [0, 0];
	cvTestPhase   = 0;
	cvTestWarning = '';
	calChannel    = 0;
	if (mode === 'combined')
	{
		combinedPhase = 0;
		const btn = document.getElementById('startBtn-combined');
		if (btn) btn.style.display = 'none';
	}

	if (mode === 'trusted')
	{
		const btn = document.getElementById('startBtn-trusted');
		if (btn) btn.style.display = 'none';
	}

	if (mode === 'trimmer')
	{
		trimmerStep  = 0;
		trimmerCount = 0;
		trimmerBuf   = [[], []];
		trimmerMeas  = [[], []];
		trimmerAlpha = [null, null];
		trimmerOrder = [];
		const btn = document.getElementById('startBtn-trimmer');
		if (btn) btn.style.display = 'none';
	}

	if (mode === 'inputs')
	{
		calInputIndex    = 0;
		inputCalData     = [[], [], [], []];
		inputLastGoodRaw = null;
		inputSweepCVVals = [];
		calState         = CAL.WAIT_INPUT;
		detectPhase      = 0;
		const btn = document.getElementById('startBtn-inputs');
		if (btn) btn.style.display = 'none';
	}
	else
	{
		calState      = CAL.WAIT_AUDIO1;
		audio1SigSeen = false;
		audio2SigSeen = false;
	}

	updateCalUI();
	drawCalGraphs();
}

////////////////////////////////////////////////////////////
// Connection detection (jack normalisation probe)

function handleConnection(a1, a2, cv1, cv2)
{
	conn1 = a1;
	conn2 = a2;
	conn3 = cv1;
	conn4 = cv2;

	if (calState === CAL.WAIT_AUDIO1 && conn1 && audio1SigSeen)
	{
		if (calMode === 'trusted')
		{
			// Trusted mode uses only Audio In 1 - skip straight to frequency wait
			calState = CAL.WAIT_FREQ;
			freqBuf  = [[], []];
			startWobble();
		}
		else
		{
			calState = CAL.WAIT_AUDIO2;
		}
		updateCalUI();
	}
	else if (calState === CAL.WAIT_AUDIO2 && conn1 && conn2 && audio2SigSeen)
	{
		calState = CAL.WAIT_FREQ;
		freqBuf  = [[], []];
		startWobble();
		updateCalUI();
	}
}

////////////////////////////////////////////////////////////
// Frequency stability check (mode-aware)

function freqBufStable()
{
	// Trusted mode: only Audio In 1 is used, so only check channel 0
	const channels = calMode === 'trusted' ? [0] : [0, 1];
	for (const ch of channels)
	{
		const buf = freqBuf[ch];
		if (buf.length < FREQ_WIN) return false;
		if (buf.some(hz => hz < FREQ_RANGE[0] || hz > FREQ_RANGE[1])) return false;
		const lo = Math.min(...buf), hi = Math.max(...buf);
		if (Math.log2(hi / lo) * 1200 > FREQ_STABLE_CENTS) return false;
	}
	return true;
}

let freqStableSince = null;  // timestamp when buffer first became stably in-range
const FREQ_STABLE_HOLD_MS = 500;

function checkFreqAdvance()
{
	if (freqBufStable())
	{
		if (freqStableSince === null) freqStableSince = Date.now();
		if (Date.now() - freqStableSince >= FREQ_STABLE_HOLD_MS)
		{
			stopWobble();
			baseHz[0] = avg(freqBuf[0]);
			// Trusted mode: both channels share the same reference oscillator
			baseHz[1] = calMode === 'trusted' ? baseHz[0] : avg(freqBuf[1]);
			calState      = CAL.WAIT_CV1;
			cvTestPhase   = 0;
			cvTestWarning = '';
			freqStableSince = null;
		}
	}
	else
	{
		freqStableSince = null;
	}
}

////////////////////////////////////////////////////////////
// Incoming audio measurements (pitch calibration modes)

function updateDisplay1(hz)
{
	latestHz1 = hz;

	if (calState === CAL.WAIT_FREQ)
	{
		freqBuf[0].push(hz);
		if (freqBuf[0].length > FREQ_WIN) freqBuf[0].shift();
		checkFreqAdvance();
		updateCalUI();
	}
	else if (calState === CAL.WAIT_CV1)
	{
		cvTestTick(hz, 0);
	}
	else if (calState === CAL.WAIT_CV2 && calMode === 'trusted')
	{
		// Trusted mode: CV Out 2 is also tested against Audio In 1
		cvTestTick(hz, 1);
	}
	else if (calState === CAL.TUNING && calChannel === 0)
	{
		calSweepTick(hz);
	}
	else if (calState === CAL.TUNING && calChannel === 1 && calMode === 'trusted')
	{
		// Trusted mode: CV Out 2 sweep is also measured via Audio In 1
		calSweepTick(hz);
	}

	const freqEl = document.getElementById('freqDisplay');
	const noteEl = document.getElementById('noteDisplay');
	if (freqEl) freqEl.textContent = hz.toFixed(2) + ' Hz';
	if (noteEl) noteEl.textContent = hzToNote(hz);
}

function updateDisplay2(hz)
{
	latestHz2 = hz;

	const noteEl2 = document.getElementById('noteDisplay2');
	const freqEl2 = document.getElementById('freqDisplay2');
	if (noteEl2) noteEl2.textContent = hzToNote(hz);
	if (freqEl2) freqEl2.textContent = hz.toFixed(2) + ' Hz';

	if (calMode === 'trusted') return;  // trusted mode uses Audio In 1 only

	if (calState === CAL.WAIT_FREQ)
	{
		freqBuf[1].push(hz);
		if (freqBuf[1].length > FREQ_WIN) freqBuf[1].shift();
		checkFreqAdvance();
		updateCalUI();
	}
	else if (calState === CAL.WAIT_RECABLE && calMode === 'combined')
	{
		// Auto-detect CV Out 2 on bottom oscillator
		cvTestTick(hz, 1);
	}
	else if (calState === CAL.WAIT_CV2)
	{
		// Osctracking phase: CV Out 1 (ch 0) tested on bottom osc; workshop phase: CV Out 2 (ch 1)
		const useChannel0 = calMode === 'osctracking' || (calMode === 'combined' && combinedPhase === 0);
		cvTestTick(hz, useChannel0 ? 0 : 1);
	}
	else if (calState === CAL.TUNING && calChannel === 1)
	{
		calSweepTick(hz);
	}
	else if (calState === CAL.LIVE_TRACK)
	{
		if (calMode === 'trimmer')
			trimmerLiveTrackTick(latestHz1, hz);
		else
			liveTrackTick(hz);
	}
}

////////////////////////////////////////////////////////////
// CV connection test (pitch calibration modes)

function cvTestTick(hz, channel)
{
	// Phase 0: send CV=0, reset buffer
	if (cvTestPhase === 0)
	{
		sendCV(channel, 0);
		cvTestBuf   = [];
		cvTestPhase = 1;
		return;
	}

	// Phase 1: collect stable measurements at CV=0, then record base Hz
	if (cvTestPhase === 1)
	{
		cvTestBuf.push(hz);
		if (cvTestBuf.length > CAL_STABLE_COUNT) cvTestBuf.shift();
		if (cvTestBuf.length < CAL_STABLE_COUNT) return;
		const lo = Math.min(...cvTestBuf), hi = Math.max(...cvTestBuf);
		if (Math.log2(hi / lo) * 1200 > CAL_STABLE_CENTS * 4) return;
		cvTestBase  = avg(cvTestBuf);
		sendCV(channel, CV_TEST_HIGH);
		cvTestBuf   = [];
		cvTestPhase = 2;
		return;
	}

	// Phase 2: discard settling samples
	if (cvTestPhase === 2)
	{
		cvTestBuf.push(hz);
		if (cvTestBuf.length >= CAL_DISCARD) { cvTestBuf = []; cvTestPhase = 3; }
		return;
	}

	// Phase 3: collect stable measurements at CV_TEST_HIGH, check ratio
	if (cvTestPhase === 3)
	{
		cvTestBuf.push(hz);
		if (cvTestBuf.length > CAL_STABLE_COUNT) cvTestBuf.shift();
		if (cvTestBuf.length < CAL_STABLE_COUNT) return;
		const lo = Math.min(...cvTestBuf), hi = Math.max(...cvTestBuf);
		if (Math.log2(hi / lo) * 1200 > CAL_STABLE_CENTS * 4) return;

		const testHz = avg(cvTestBuf);
		const ratio  = testHz / cvTestBase;
		sendCV(channel, 0);

		if (ratio >= 1.5 && ratio <= 3.0)
		{
			if (calMode === 'combined' && calState === CAL.WAIT_RECABLE)
		{
			// CV Out 2 confirmed on bottom oscillator - begin calibration phase
			startPhase2();
		}
		else if (channel === 0 && (calMode === 'workshop' || (calMode === 'combined' && combinedPhase === 1)))
			{
				// Workshop / combined phase 1: wait to confirm second cable before sweeping
				calState      = CAL.WAIT_CV2;
				cvTestPhase   = 0;
				cvTestWarning = '';
			}
			else if ((calMode === 'osctracking' || (calMode === 'combined' && combinedPhase === 0)) && calState === CAL.WAIT_CV2)
			{
				if (calMode === 'combined')
				{
					// Combined mode: skip bottom-osc sweep, go straight to live tracking
					startLiveTrack();
				}
				else
				{
					// Osctracking phase: CV Out 1 confirmed on bottom osc -> sweep ch1 audio
					calState   = CAL.TUNING;
					calChannel = 1;
					startCalSweep();
				}
			}
			else if (channel === 1 && (calMode === 'workshop' || (calMode === 'combined' && combinedPhase === 1)))
			{
				// Workshop / combined phase 1: both cables confirmed - start ch0 sweep
				calState   = CAL.TUNING;
				calChannel = 0;
				startCalSweep();
			}
			else if (channel === 0 && calMode === 'trimmer')
			{
				// Trimmer mode: CV Out 1 confirmed on top osc - wait for CV Out 2 on bottom osc
				calState      = CAL.WAIT_CV2;
				cvTestPhase   = 0;
				cvTestWarning = '';
			}
			else if (channel === 1 && calMode === 'trimmer')
			{
				// Trimmer mode: CV Out 2 confirmed on bottom osc - start dual live tracking
				startTrimmerLiveTrack();
			}
			else
			{
				// Trusted ch0 or ch1: start sweep immediately on the channel just tested
				calState   = CAL.TUNING;
				calChannel = channel;
				startCalSweep();
			}
		}
		else
		{
			cvTestWarning = '';
			cvTestPhase   = 0;  // retry
		}
		updateCalUI();
	}
}

////////////////////////////////////////////////////////////
// Pitch calibration sweep

function startCalSweep()
{
	// Phase 2 of combined: start fresh calibration data
	if (calMode === 'combined' && combinedPhase === 1 && calChannel === 0)
		calData = [[], []];

	const isOscPhase = calMode === 'osctracking' || (calMode === 'combined' && combinedPhase === 0);
	const steps = isOscPhase ? TRACK_STEPS  : CAL_STEPS;
	const minCV = isOscPhase ? TRACK_MIN_CV : CAL_MIN_CV;
	const maxCV = isOscPhase ? TRACK_MAX_CV : CAL_MAX_CV;
	calCVValues = [];
	for (let i = 0; i < steps; i++)
		calCVValues.push(Math.round(minCV + i * (maxCV - minCV) / (steps - 1)));
	// Randomise order for the osctracking phase so settling errors don't accumulate
	if (isOscPhase)
		for (let i = calCVValues.length - 1; i > 0; i--)
		{
			const j = Math.floor(Math.random() * (i + 1));
			[calCVValues[i], calCVValues[j]] = [calCVValues[j], calCVValues[i]];
		}
	calStepIndex  = 0;
	calStepCount  = 0;
	calStepBuffer = [];
	// Osctracking phase: both sweeps use CV Out 1 (channel 0), not CV Out 2
	const cvCh = (isOscPhase && calChannel === 1) ? 0 : calChannel;
	sendCV(1 - cvCh, CAL_MIN_CV);  // hold the other CV out at minimum
	sendCV(cvCh, calCVValues[0]);
	updateCalUI();
}

function calSweepTick(hz)
{
	calStepCount++;
	const discard = (calMode === 'osctracking' || (calMode === 'combined' && combinedPhase === 0)) ? TRACK_DISCARD : CAL_DISCARD;
	if (calStepCount <= discard) return;

	calStepBuffer.push(hz);
	if (calStepBuffer.length > CAL_STABLE_COUNT) calStepBuffer.shift();

	// Timed out: skip this step without recording
	if (calStepCount > CAL_TIMEOUT)
	{
		advanceSweepStep();
		return;
	}

	if (calStepBuffer.length < CAL_STABLE_COUNT) return;

	const lo = Math.min(...calStepBuffer), hi = Math.max(...calStepBuffer);
	if (Math.log2(hi / lo) * 1200 > CAL_STABLE_CENTS) return;

	calData[calChannel].push({ cv: calCVValues[calStepIndex], hz: avg(calStepBuffer) });
	// drawCalGraphs();  // skip realtime redraw; graph shown on completion
	advanceSweepStep();
}

function advanceSweepStep()
{
	calStepIndex++;
	if (calStepIndex >= calCVValues.length)
	{
		if (calChannel === 0)
		{
			if (calMode === 'trusted' || calMode === 'osctracking' || (calMode === 'combined' && combinedPhase === 0))
			{
				// Pause for user to reconnect CV cable to the second oscillator
				calState      = CAL.WAIT_CV2;
				cvTestPhase   = 0;
				cvTestWarning = '';
				sendCV(0, 0);
			}
			else
			{
				// Workshop / combined phase 1: second cable already connected, sweep immediately
				calChannel = 1;
				startCalSweep();
				return;
			}
		}
		else
		{
			if (calMode === 'osctracking' || (calMode === 'combined' && combinedPhase === 0))
			{
				startLiveTrack();
				return;
			}
			calState = CAL.DONE;
		}
		updateCalUI();
		drawCalGraphs();
		return;
	}

	const cvCh = ((calMode === 'osctracking' || (calMode === 'combined' && combinedPhase === 0)) && calChannel === 1) ? 0 : calChannel;
	sendCV(cvCh, calCVValues[calStepIndex]);
	calStepCount  = 0;
	calStepBuffer = [];
	updateCalUI();
}

////////////////////////////////////////////////////////////
// Input calibration sweep

function startInputMeasurement()
{
	if (calMode !== 'inputs' || calState !== CAL.WAIT_INPUT) return;
	calState = CAL.SWEEP_INPUT;
	startInputSweep();
}

// Active during WAIT_INPUT: drives CV Out 1 to two known voltages and checks
// whether the target input's ADC reading changes by the expected amount.
// Retries automatically until a correlated response is seen.
function inputDetectTick(adc)
{
	if (detectPhase === 0)
	{
		sendInputMV(DETECT_LOW_MV);
		detectSettle = 0;
		detectBuf    = [];
		detectPhase  = 1;
		return;
	}
	if (detectPhase === 1)  // settling at low voltage
	{
		if (++detectSettle < DETECT_SETTLE) return;
		detectBuf.push(adc);
		if (detectBuf.length < DETECT_COLLECT) return;
		detectLowADC = avg(detectBuf);
		sendInputMV(DETECT_HIGH_MV);
		detectSettle = 0;
		detectBuf    = [];
		detectPhase  = 2;
		return;
	}
	if (detectPhase === 2)  // settling at high voltage
	{
		if (++detectSettle < DETECT_SETTLE) return;
		detectBuf.push(adc);
		if (detectBuf.length < DETECT_COLLECT) return;
		const highADC = avg(detectBuf);
		if (Math.abs(highADC - detectLowADC) >= DETECT_THRESHOLD)
			startInputMeasurement();  // confirmed: CV Out 1 is reaching this input
		else
			detectPhase = 0;  // no response - keep retrying
	}
}

function startInputSweep()
{
	inputSweepCVVals = [];
	for (let i = 0; i < CAL_IN_STEPS; i++)
		inputSweepCVVals.push(Math.round(CAL_IN_MAX_MV - i * (CAL_IN_MAX_MV - CAL_IN_MIN_MV) / (CAL_IN_STEPS - 1)));
	inputSweepStepIdx = 0;
	inputSweepSettle  = 0;
	inputSweepBuf     = [];
	inputLastGoodRaw  = null;
	sendInputMV(inputSweepCVVals[0]);
	updateCalUI();
}

// Called on each D| message with all four raw ADC readings.
function inputSweepTick(readings)
{
	if (calState === CAL.WAIT_INPUT)
	{
		inputDetectTick(readings[calInputIndex]);
		return;
	}
	if (calState !== CAL.SWEEP_INPUT) return;

	inputSweepSettle++;
	if (inputSweepSettle <= CAL_IN_SETTLE) return;  // discard settling samples

	// Reject individual raw readings that are outliers within this step's collection window.
	// inputLastGoodRaw is reset to null at the start of each step, so the first
	// reading after settling is always accepted as the reference.
	const raw = readings[calInputIndex];
	if (inputLastGoodRaw !== null && Math.abs(raw - inputLastGoodRaw) > 10) return;
	inputLastGoodRaw = raw;
	inputSweepBuf.push(raw);

	if (inputSweepBuf.length >= CAL_IN_COLLECT)
	{
		inputCalData[calInputIndex].push({ mv: inputSweepCVVals[inputSweepStepIdx], adc: avg(inputSweepBuf) });
		// drawInputResiduals();  // skip realtime redraw; graph shown on completion
		advanceInputSweep();
	}
}

function advanceInputSweep()
{
	inputSweepStepIdx++;
	if (inputSweepStepIdx >= inputSweepCVVals.length)
	{
		// This input is done
		sendInputMV(0);
		if (calInputIndex < IN_NAMES.length - 1)
		{
			calInputIndex++;
			calState    = CAL.WAIT_INPUT;
			detectPhase = 0;
		}
		else
		{
			calState = CAL.DONE;
			updateCalUI();
			saveInputCalToEEPROM();
			return;
		}
		updateCalUI();
		return;
	}

	sendInputMV(inputSweepCVVals[inputSweepStepIdx]);
	inputSweepSettle = 0;
	inputSweepBuf    = [];
	inputLastGoodRaw = null;
	updateCalUI();
}

////////////////////////////////////////////////////////////
// Frequency slider widget for WAIT_FREQ display

function makeFreqSlider(hz, inRange, stable)
{
	const logLo  = Math.log2(100), logHi = Math.log2(700);
	const toPct  = f => Math.max(0, Math.min(100, (Math.log2(Math.max(f, 1)) - logLo) / (logHi - logLo) * 100));
	const zLeft  = toPct(FREQ_RANGE[0]).toFixed(1);
	const zWidth = (toPct(FREQ_RANGE[1]) - toPct(FREQ_RANGE[0])).toFixed(1);
	const mPos   = (hz > 0 ? toPct(hz) : -2).toFixed(1);
	const mCol   = (inRange && stable) ? '#2a7' : (inRange ? '#888' : '#c04000');
	return `<div style="position:relative;height:16px;background:#e8e8e8;border-radius:3px;overflow:hidden;margin-top:3px">` +
		`<div style="position:absolute;left:${zLeft}%;width:${zWidth}%;height:100%;background:#c8efc8"></div>` +
		`<div style="position:absolute;left:${mPos}%;transform:translateX(-50%);width:3px;height:100%;background:${mCol}"></div>` +
		`</div>`;
}

function makeLiveTrackSlider(alpha1, alpha2live)
{
	const RANGE  = 2.0;   // ±2% shown on slider
	const GOOD   = 0.05;  // green window: ±0.05%
	const pct    = (alpha2live / alpha1 - 1) * 100;
	const toPos  = v => Math.max(0, Math.min(100, (v + RANGE) / (2 * RANGE) * 100));
	const mPos   = toPos(pct).toFixed(1);
	const gLeft  = toPos(-GOOD).toFixed(1);
	const gWidth = (toPos(GOOD) - toPos(-GOOD)).toFixed(1);
	const inGood = Math.abs(pct) < GOOD;
	const mCol   = inGood ? '#2a7' : '#c04000';
	const sign   = pct >= 0 ? '+' : '';
	const label  = inGood ? 'matched \u2714' : `${sign}${pct.toFixed(2)}%`;
	return `<div style="margin-top:4px">` +
		`<div style="display:flex;justify-content:space-between;font-size:0.75rem;margin-bottom:3px">` +
		`<span style="color:#888">bottom osc tracking vs top</span>` +
		`<span style="color:${mCol};font-weight:bold;font-family:'SF Mono','Fira Mono','Consolas',monospace">${label}</span>` +
		`</div>` +
		`<div style="position:relative;height:16px;background:#e8e8e8;border-radius:3px;overflow:hidden">` +
		`<div style="position:absolute;left:${gLeft}%;width:${gWidth}%;height:100%;background:#c8efc8"></div>` +
		`<div style="position:absolute;left:${mPos}%;transform:translateX(-50%);width:3px;height:100%;background:${mCol}"></div>` +
		`</div>` +
		`<div style="display:flex;justify-content:space-between;font-size:0.65rem;color:#bbb;margin-top:2px">` +
		`<span>\u22122%</span><span>0%</span><span>+2%</span></div>` +
		`</div>`;
}

////////////////////////////////////////////////////////////
// Trimmer adjustment UI helpers

function makeTrimmerSlider(ch)
{
	const alpha = trimmerAlpha[ch];
	const col   = CH_COLOR[ch];
	const name  = ch === 0 ? 'Top oscillator' : 'Bottom oscillator';
	if (alpha === null)
		return `<div style="margin-bottom:10px"><b style="color:${col}">${name}</b>: measuring&hellip;</div>`;

	const RANGE = 2.0;
	const GOOD  = 0.05;
	const pct   = (alpha - 1.0) * 100;
	const toPos = v => Math.max(0, Math.min(100, (v + RANGE) / (2 * RANGE) * 100));
	const mPos  = toPos(pct).toFixed(1);
	const gLeft = toPos(-GOOD).toFixed(1);
	const gW    = (toPos(GOOD) - toPos(-GOOD)).toFixed(1);
	const inGood = Math.abs(pct) < GOOD;
	const mCol  = inGood ? '#2a7' : '#c04000';
	const sign  = pct >= 0 ? '+' : '';
	const label = inGood ? 'tracking ok ✔' : `${sign}${pct.toFixed(2)}%`;
	return `<div style="margin-bottom:10px">` +
		`<div style="display:flex;justify-content:space-between;font-size:0.75rem;margin-bottom:3px">` +
		`<span><b style="color:${col}">${name}</b></span>` +
		`<span style="color:${mCol};font-weight:bold;font-family:'SF Mono','Fira Mono','Consolas',monospace">${label}</span>` +
		`</div>` +
		`<div style="position:relative;height:16px;background:#e8e8e8;border-radius:3px;overflow:hidden">` +
		`<div style="position:absolute;left:${gLeft}%;width:${gW}%;height:100%;background:#c8efc8"></div>` +
		`<div style="position:absolute;left:${mPos}%;transform:translateX(-50%);width:3px;height:100%;background:${mCol}"></div>` +
		`</div>` +
		`<div style="display:flex;justify-content:space-between;font-size:0.65rem;color:#bbb;margin-top:2px">` +
		`<span>&minus;2%</span><span>0%</span><span>+2%</span></div>` +
		`</div>`;
}

function trimmerChannelInstruction(ch)
{
	const alpha = trimmerAlpha[ch];
	const name  = ch === 0 ? 'Top' : 'Bottom';
	if (alpha === null) return '';
	const pct = (alpha - 1.0) * 100;
	if (Math.abs(pct) < 0.05) return `<b>${name} oscillator</b>: tracking correctly.`;
	const ALPHA_RANGE = 1.0 - 82 / 92;
	const turns = Math.abs(1.0 - alpha) / (ALPHA_RANGE / 30);
	const dir   = pct > 0 ? 'anticlockwise' : 'clockwise';
	const over  = pct > 0 ? 'over-tracks' : 'under-tracks';
	return `<b>${name} oscillator</b> ${over} by ${Math.abs(pct).toFixed(2)}% &mdash; ` +
		`turn trimmer <b>${dir}</b> by <b>${turnsText(turns)}</b>.`;
}

function trimmerInstructionText()
{
	if (trimmerAlpha[0] === null && trimmerAlpha[1] === null)
		return 'Adjust the oscillator trimmers to achieve 1&nbsp;V/oct tracking. Measuring&hellip;';
	const lines = [0, 1].map(trimmerChannelInstruction).filter(Boolean);
	return lines.join('<br><br>') || 'Measuring&hellip;';
}

////////////////////////////////////////////////////////////
// UI update - targets the active mode's panel elements

function updateCalUI()
{
	if (!calMode) return;

	const pfx     = `-${calMode}`;
	const instrEl = document.getElementById(`calInstruction${pfx}`);
	const progEl  = document.getElementById(`calProgress${pfx}`);
	const stepsEl = document.getElementById(`calSteps${pfx}`);
	if (!instrEl) return;

	const steps = CAL_STEP_INFO[calMode];

	// Instruction text (text field may be a string or a function)
	if (calState === CAL.IDLE)
	{
		instrEl.innerHTML = 'Press <b>Start Calibration</b> to begin.';
	}
	else
	{
		const info = steps.find(s => s.match());
		let text = info ? (typeof info.text === 'function' ? info.text() : info.text) : '';
		if ((calState === CAL.WAIT_CV1 || calState === CAL.WAIT_CV2) && cvTestWarning)
			text += ` <span style="color:#c04000">${cvTestWarning}</span>`;
		instrEl.innerHTML = text;
	}

	// Step indicator bubbles
	if (stepsEl)
	{
		const activeIdx = steps.findIndex(s => s.match());
		stepsEl.innerHTML = steps.map((s, i) => {
			const done   = activeIdx >= 0 && i < activeIdx;
			const active = i === activeIdx;
			const col  = done ? '#aaa' : (active ? '#4b0082' : '#ddd');
			const wt   = active ? 'bold' : 'normal';
			const bg   = active ? '#f0eaff' : 'transparent';
			return `<span style="color:${col};font-weight:${wt};background:${bg};` +
			       `border-radius:3px;padding:1px 5px;margin-right:2px">${s.label}</span>`;
		}).join('<span style="color:#ccc">›</span>');
	}

	// Progress / status area
	if (progEl)
	{
		if (calMode === 'inputs')
		{
			if (calState === CAL.SWEEP_INPUT)
			{
				const done = inputCalData[calInputIndex].length;
				const pct  = Math.round(done / CAL_IN_STEPS * 100);
				progEl.innerHTML =
					`${IN_NAMES[calInputIndex]}: ${pct}%<br>` + // ${done}/${CAL_IN_STEPS} points
					`<div style="background:#eee;height:6px;border-radius:3px;margin-top:3px">` +
					`<div style="background:#4b0082;height:6px;border-radius:3px;width:${pct}%"></div></div>`;
			}
			else
			{
				progEl.textContent = '';
			}
		}
		else
		{
			if (calState === CAL.WAIT_FREQ)
			{
				// Trusted mode shows only Audio In 1; workshop shows both
				const channels = calMode === 'trusted' ? [0] : [0, 1];
				const hzValues = [latestHz1, latestHz2];
				const rows = channels.map(ch => {
					const hz    = hzValues[ch];
					const chCol = CH_COLOR[ch];
					if (hz === 0)
						return `<div style="margin-bottom:5px">` +
							`<span><b style="color:${chCol}">In ${ch+1}</b> <span style="color:#aaa">no signal</span></span>` +
							makeFreqSlider(0, false, false) + `</div>`;
					const inRange = hz >= FREQ_RANGE[0] && hz <= FREQ_RANGE[1];
					const buf     = freqBuf[ch];
					const stable  = buf.length >= FREQ_WIN && (() => {
						const lo = Math.min(...buf), hi = Math.max(...buf);
						return Math.log2(hi / lo) * 1200 < FREQ_STABLE_CENTS;
					})();
					const statCol   = (inRange && stable) ? '#2a7' : (inRange ? '#888' : '#b03000');
					const dot       = (inRange && stable) ? '\u25CF' : (inRange ? '\u25CB' : '\u25CF');
					const statLabel = (inRange && stable) ? 'stable' : (inRange ? 'settling\u2026' : 'out of range');
					return `<div style="margin-bottom:5px">` +
						`<div style="display:flex;justify-content:space-between">` +
						`<span><b style="color:${chCol}">In ${ch+1}</b> &nbsp; ${hzToNote(hz)} &nbsp; ${hz.toFixed(1)} Hz</span>` +
						`<span style="color:${statCol}">${dot} ${statLabel}</span>` +
						`</div>` +
						makeFreqSlider(hz, inRange, stable) + `</div>`;
				});
				progEl.innerHTML = rows.join('');
			}
			else if (calState === CAL.TUNING)
			{
				const isOscPhase = calMode === 'osctracking' || (calMode === 'combined' && combinedPhase === 0);
			const label = isOscPhase
				? (calChannel === 0 ? 'Top oscillator' : 'Bottom oscillator')
				: (calChannel === 0 ? 'CV Out 1' : 'CV Out 2');
				const done  = calData[calChannel].length;
				const total = calCVValues.length || CAL_STEPS;
				const pct   = Math.round(done / total * 100);
				progEl.innerHTML =
					`${label}: ${pct}%<br>` + //${done}/${total} points
					`<div style="background:#eee;height:6px;border-radius:3px;margin-top:3px">` +
					`<div style="background:#4b0082;height:6px;border-radius:3px;width:${pct}%"></div></div>`;
			}
			else if (calState === CAL.LIVE_TRACK)
			{
				if (calMode === 'trimmer')
				{
					progEl.innerHTML = makeTrimmerSlider(0) + makeTrimmerSlider(1);
				}
				else
				{
					let alpha1 = null;
					if (calData[0].length >= 2)
					{
						const cvs0 = calData[0].map(d => d.cv);
						const l2_0 = calData[0].map(d => Math.log2(d.hz));
						alpha1 = linReg(cvs0, l2_0).slope * CV_TEST_HIGH;
					}
					if (alpha1 !== null && liveAlpha2 !== null)
						progEl.innerHTML = makeLiveTrackSlider(alpha1, liveAlpha2);
					else
						progEl.textContent = 'Measuring\u2026';
				}
			}
			else if (calState === CAL.DONE)
			{
				if (calMode === 'osctracking' || (calMode === 'combined' && combinedPhase === 0))
					progEl.textContent = `Top osc: ${calData[0].length} pts  ·  Bottom osc: ${calData[1].length} pts`;
				else
					progEl.textContent = `CV1: ${calData[0].length} pts  ·  CV2: ${calData[1].length} pts`;
			}
			else
			{
				progEl.textContent = '';
			}
		}
	}

	// Show/hide elements specific to each mode
	const eepromPanel = document.getElementById(`eepromPanel${pfx}`);
	if (eepromPanel)
	{
		const showEeprom = calState === CAL.DONE &&
			calMode !== 'osctracking' &&
			calMode !== 'inputs' &&
			calMode !== 'trusted' &&
			!(calMode === 'combined' && combinedPhase === 0) &&
			!(standaloneMode && calMode === 'combined');
		eepromPanel.style.display = showEeprom ? 'flex' : 'none';
	}

	const backBtn = document.getElementById('backBtn-inputs');
	if (backBtn && calMode === 'inputs')
		backBtn.textContent = calState === CAL.DONE ? '← Back' : '← Cancel';

	const standalonePanel = document.getElementById(`standalonePanel${pfx}`);
	if (standalonePanel)
	{
		const showStandalone =
			(standaloneMode && calState === CAL.DONE && calMode === 'combined' && combinedPhase === 1) ||
			(calMode === 'trusted' && calState === CAL.DONE);
		standalonePanel.style.display = showStandalone ? 'flex' : 'none';
	}

	const remeasurePanel = document.getElementById(`remeasurePanel${pfx}`);
	if (remeasurePanel)
		remeasurePanel.style.display = (
			calMode === 'osctracking'
			&& (calState === CAL.DONE || calState === CAL.LIVE_TRACK)
		) ? 'flex' : 'none';

const graphCanvas = document.getElementById(`graphAtRes${pfx}`);
	if (graphCanvas)
	{
		const showGraph = calMode === 'inputs'
			? false
			: calMode === 'combined'
				? (calState === CAL.DONE && combinedPhase === 1)
				: (calState === CAL.TUNING || calState === CAL.DONE || calState === CAL.LIVE_TRACK);
		graphCanvas.style.display = showGraph ? 'block' : 'none';
	}

	// Combined-mode panels
	if (calMode === 'combined')
	{
		const cont = document.getElementById('continuePanel-combined');
		if (cont)
			cont.style.display = (combinedPhase === 0 && calState === CAL.LIVE_TRACK) ? 'flex' : 'none';
	}
}

////////////////////////////////////////////////////////////
// Pitch calibration graphs

const AT_W     = 500;
const AT_RES_H = 250;

// Scale the canvas backing buffer to match physical pixels (HiDPI/Retina).
// Returns a context pre-scaled so all drawing uses CSS-pixel coordinates.
function setupCanvas(canvas)
{
	const dpr = window.devicePixelRatio || 1;
	canvas.width  = AT_W     * dpr;
	canvas.height = AT_RES_H * dpr;
	canvas.style.width  = AT_W     + 'px';
	canvas.style.height = AT_RES_H + 'px';
	const ctx = canvas.getContext('2d');
	ctx.scale(dpr, dpr);
	return ctx;
}

const CH_COLOR = ['#4b0082', '#c04000'];

function linReg(xs, ys)
{
	const n = xs.length;
	let sx = 0, sy = 0, sxy = 0, sxx = 0;
	for (let i = 0; i < n; i++) { sx += xs[i]; sy += ys[i]; sxy += xs[i]*ys[i]; sxx += xs[i]*xs[i]; }
	const slope = (n*sxy - sx*sy) / (n*sxx - sx*sx);
	return { slope, intercept: (sy - slope*sx) / n };
}

function drawCalGraphs()
{
	if (!calMode || calMode === 'inputs' || calMode === 'trimmer') return;
	drawAtResiduals(`graphAtRes-${calMode}`);
}

function drawAtResiduals(canvasId)
{
	const canvas = document.getElementById(canvasId);
	if (!canvas) return;
	const ctx = setupCanvas(canvas);
	ctx.fillStyle = '#fff';
	ctx.fillRect(0, 0, AT_W, AT_RES_H);

	const allData = [...calData[0], ...calData[1]];
	if (allData.length < 2) return;

	const allL2 = allData.map(d => Math.log2(d.hz));
	const xLo   = Math.min(...allL2), xHi = Math.max(...allL2);

	// For osctracking ch1: plot residuals against the TOP osc regression line so the
	// graph directly shows the deviation between the two oscillators' tracking.
	let reg0 = null;
	if ((calMode === 'osctracking' || (calMode === 'combined' && combinedPhase === 0)) && calData[0].length >= 2)
		reg0 = linReg(calData[0].map(d => d.cv), calData[0].map(d => Math.log2(d.hz)));

	let livePoints = null;  // computed after chRes (needs sweep residuals for drift correction)

	let allRes = [];
	const chRes = calData.map((data, ch) => {
		if (data.length < 2) return [];
		const cvs = data.map(d => d.cv);
		const l2  = data.map(d => Math.log2(d.hz));
		let slope, intercept;
		if (calMode === 'osctracking' && ch === 1 && reg0)
		{
			// Use osc 1's slope but osc 2's own intercept: removes tuning offset,
			// leaving only the tracking (slope) difference visible as a tilt.
			const reg1 = linReg(cvs, l2);
			slope     = reg0.slope;
			intercept = reg1.intercept;
		}
		else
		{
			({ slope, intercept } = linReg(cvs, l2));
		}
		const res = data.map((d, i) => (Math.log2(d.hz) - (slope * d.cv + intercept)) * 1200);
		allRes = allRes.concat(res);
		return res;
	});

	if (allRes.length === 0) return;

	// Drift-corrected live-track overlay.
	// Compute raw residuals with the same baseline as the sweep, then subtract the
	// mean difference vs the interpolated sweep curve so that pitch drift (temperature,
	// warm-up) cancels out.  What remains shows only genuine slope deviation.
	if (calState === CAL.LIVE_TRACK && reg0 && liveTrackLastMeas.length === LIVE_TRACK_CVS.length && chRes[1].length >= 2)
	{
		// Sort by cv (sweep may be in random order)
		const sweepPairs = calData[1].map((d, i) => [d.cv, chRes[1][i]]).sort((a, b) => a[0] - b[0]);
		const sweepCvs = sweepPairs.map(p => p[0]);
		const sweepRes = sweepPairs.map(p => p[1]);
		const reg1 = linReg(sweepCvs, calData[1].map(d => Math.log2(d.hz)));

		const rawRes = liveTrackLastMeas.map(d =>
			(Math.log2(d.hz) - (reg0.slope * d.cv + reg1.intercept)) * 1200
		);

		// Linearly interpolate sweep residuals at each live CV value
		const interpRes = liveTrackLastMeas.map(d => {
			const cv = d.cv;
			let i = sweepCvs.findIndex(c => c >= cv);
			if (i < 0)  return sweepRes[sweepRes.length - 1];
			if (i === 0) return sweepRes[0];
			const t = (cv - sweepCvs[i - 1]) / (sweepCvs[i] - sweepCvs[i - 1]);
			return sweepRes[i - 1] + t * (sweepRes[i] - sweepRes[i - 1]);
		});

		// Drift offset = mean deviation of live residuals from the sweep curve
		const drift = rawRes.reduce((s, r, j) => s + r - interpRes[j], 0) / rawRes.length;

		livePoints = liveTrackLastMeas.map((d, j) => ({
			l2:  Math.log2(d.hz),
			res: rawRes[j] - drift,
		}));
	}

	if (livePoints) livePoints.forEach(p => allRes.push(p.res));

	let rLo = Math.min(...allRes), rHi = Math.max(...allRes);
	const rSpan = Math.max(rHi - rLo, 2);
	const rMid  = (rLo + rHi) / 2;
	rLo = rMid - rSpan / 2 - 0.5;
	rHi = rMid + rSpan / 2 + 0.5;

	const pad = { l: 28, r: 8, t: 6, b: 18 };
	const toX = l2 => pad.l + (l2 - xLo) / (xHi - xLo) * (AT_W - pad.l - pad.r);
	const toY = r  => AT_RES_H - pad.b - (r - rLo) / (rHi - rLo) * (AT_RES_H - pad.t - pad.b);

	// Cent grid (y-axis)
	ctx.font = '10px monospace';
	for (let c = Math.ceil(rLo); c <= Math.floor(rHi); c++)
	{
		const y = toY(c);
		ctx.strokeStyle = c === 0 ? '#bbb' : '#eee';
		ctx.lineWidth   = c === 0 ? 1.5 : 1;
		ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(AT_W - pad.r, y); ctx.stroke();
		ctx.fillStyle = '#444';
		ctx.fillText(`${c >= 0 ? '+' : ''}${c}`, 2, y + 3);
	}

	// Octave grid lines + note labels (x-axis)
	const octaves = [[16.352,'C0'],[32.703,'C1'],[65.41,'C2'],[130.81,'C3'],[261.63,'C4'],[523.25,'C5'],[1046.5,'C6'],[2093.0,'C7']];
	ctx.fillStyle = '#444'; ctx.font = '10px monospace';
	for (const [f, label] of octaves)
	{
		const l2 = Math.log2(f);
		if (l2 < xLo || l2 > xHi) continue;
		const x = toX(l2);
		ctx.strokeStyle = '#ddd'; ctx.lineWidth = 1;
		ctx.beginPath(); ctx.moveTo(x, pad.t); ctx.lineTo(x, AT_RES_H - pad.b); ctx.stroke();
		ctx.fillText(label, x - 8, AT_RES_H - 4);
	}

	// Per-channel lines + dots (sort by cv so randomised sweeps draw a smooth line)
	for (let ch = 0; ch < 2; ch++)
	{
		const data = calData[ch];
		const res  = chRes[ch];
		if (data.length < 2) continue;

		const pairs = data.map((d, i) => [d, res[i]]).sort((a, b) => a[0].cv - b[0].cv);

		ctx.strokeStyle = CH_COLOR[ch]; ctx.lineWidth = 1.5;
		ctx.beginPath();
		pairs.forEach(([d, r], i) => {
			const x = toX(Math.log2(d.hz)), y = toY(r);
			if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
		});
		ctx.stroke();

		ctx.fillStyle = CH_COLOR[ch];
		pairs.forEach(([d, r]) => {
			ctx.beginPath();
			ctx.arc(toX(Math.log2(d.hz)), toY(r), 2.5, 0, 2 * Math.PI);
			ctx.fill();
		});
	}

	// Live-track overlay: draw the 3 fresh bottom-osc points as open circles
	if (livePoints)
	{
		ctx.strokeStyle = CH_COLOR[1];
		ctx.lineWidth   = 2;
		livePoints.forEach(p => {
			ctx.beginPath();
			ctx.arc(toX(p.l2), toY(p.res), 5.5, 0, 2 * Math.PI);
			ctx.stroke();
		});
	}

	ctx.fillStyle = '#444'; ctx.font = '10px monospace';
	ctx.fillText('Residual error (cents)', pad.l + 2, pad.t + 9);
}

////////////////////////////////////////////////////////////
// Input calibration residual graph

function drawInputResiduals()
{
	const canvas = document.getElementById('graphAtRes-inputs');
	if (!canvas) return;
	const ctx = setupCanvas(canvas);
	ctx.fillStyle = '#fff';
	ctx.fillRect(0, 0, AT_W, AT_RES_H);

	// Compute per-channel residuals; collect all for a common y-scale
	let allRes = [];
	const chRes = inputCalData.map(data => {
		if (data.length < 2) return [];
		const xs = data.map(d => d.mv);
		const ys = data.map(d => d.adc);
		const { slope, intercept } = linReg(xs, ys);
		const res = ys.map((y, i) => y - (slope * xs[i] + intercept));
		allRes = allRes.concat(res);
		return res;
	});

	if (allRes.length === 0) return;

	// X axis: millivolts converted to volts
	const xLo = CAL_IN_MIN_MV / 1000;
	const xHi = CAL_IN_MAX_MV / 1000;

	// Y axis: auto-scale to data, minimum span of ±2 ADC counts
	let rLo = Math.min(...allRes), rHi = Math.max(...allRes);
	const rSpan = Math.max(rHi - rLo, 4);
	const rMid  = (rLo + rHi) / 2;
	rLo = rMid - rSpan / 2 - 0.5;
	rHi = rMid + rSpan / 2 + 0.5;

	const pad = { l: 32, r: 90, t: 6, b: 18 };
	const toX = v  => pad.l + (v - xLo) / (xHi - xLo) * (AT_W - pad.l - pad.r);
	const toY = r  => AT_RES_H - pad.b - (r - rLo) / (rHi - rLo) * (AT_RES_H - pad.t - pad.b);

	// Y grid - choose a sensible tick step
	const rRange   = rHi - rLo;
	const tickStep = rRange <= 10 ? 1 : rRange <= 50 ? 5 : rRange <= 100 ? 10 : 20;
	ctx.font = '10px monospace';
	for (let c = Math.ceil(rLo / tickStep) * tickStep; c <= rHi; c += tickStep)
	{
		const y = toY(c);
		ctx.strokeStyle = c === 0 ? '#bbb' : '#eee';
		ctx.lineWidth   = c === 0 ? 1.5 : 1;
		ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(AT_W - pad.r, y); ctx.stroke();
		ctx.fillStyle = '#444';
		ctx.fillText(`${c >= 0 ? '+' : ''}${c}`, 2, y + 3);
	}

	// X grid - voltage reference lines at whole volts
	ctx.fillStyle = '#444'; ctx.font = '10px monospace';
	for (let v = Math.ceil(xLo); v <= Math.floor(xHi); v++)
	{
		const x = toX(v);
		ctx.strokeStyle = v === 0 ? '#ccc' : '#eee';
		ctx.lineWidth   = v === 0 ? 1.5 : 1;
		ctx.beginPath(); ctx.moveTo(x, pad.t); ctx.lineTo(x, AT_RES_H - pad.b); ctx.stroke();
		ctx.fillText(`${v}V`, x - 6, AT_RES_H - 4);
	}

	// Per-channel lines + dots
	for (let ch = 0; ch < 4; ch++)
	{
		const data = inputCalData[ch];
		const res  = chRes[ch];
		if (data.length < 2) continue;

		ctx.strokeStyle = IN_COLOR[ch]; ctx.lineWidth = 1.5;
		ctx.beginPath();
		data.forEach((d, i) => {
			const x = toX(d.mv / 1000), y = toY(res[i]);
			if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
		});
		ctx.stroke();
	}

	// Legend (right side, inside the right pad area)
	const lx = AT_W - pad.r + 6;
	IN_NAMES.forEach((name, ch) => {
		if (inputCalData[ch].length === 0) return;
		const ly = pad.t + 8 + ch * 18;
		ctx.fillStyle = IN_COLOR[ch];
		ctx.fillRect(lx, ly, 10, 10);
		ctx.fillStyle = '#444'; ctx.font = '9px monospace';
		ctx.fillText(name, lx + 13, ly + 9);
	});

	// Axis labels
	ctx.fillStyle = '#444'; ctx.font = '10px monospace';
	ctx.fillText('Residual (ADC counts)', pad.l + 2, pad.t + 9);
}

////////////////////////////////////////////////////////////
// Input calibration EEPROM save

function saveInputCalToEEPROM()
{
	const statusEl = document.getElementById('eepromStatus-inputs');

	for (let ch = 0; ch < 4; ch++)
	{
		if (inputCalData[ch].length < 2)
		{
			if (statusEl) statusEl.textContent = `Need calibration data for ${IN_NAMES[ch]}.`;
			return;
		}
	}

	// 38-byte buffer: 2-byte magic, 1-byte version, 1-byte padding,
	// 4 channels × 8 bytes (adcOffset int32 BE + mvPerAdcQ16 int32 BE), 2-byte CRC
	const buf = new Uint8Array(38);
	buf[0] = (2002 >> 8) & 0xFF;
	buf[1] =  2002       & 0xFF;
	buf[2] = 0;  // version
	buf[3] = 0;  // padding

	for (let ch = 0; ch < 4; ch++)
	{
		const data = inputCalData[ch];
		const xs = data.map(d => d.mv);   // millivolts (calibrated)
		const ys = data.map(d => d.adc);
		const { slope, intercept } = linReg(xs, ys);
		// slope is ADC counts per mV; 1/slope is mV per ADC count
		const adcOffset   = Math.round(intercept);
		const mvPerAdcQ16 = Math.round(65536 / slope);

		const off = 4 + ch * 8;
		buf[off]   = (adcOffset   >>> 24) & 0xFF;
		buf[off+1] = (adcOffset   >>> 16) & 0xFF;
		buf[off+2] = (adcOffset   >>>  8) & 0xFF;
		buf[off+3] = (adcOffset   >>>  0) & 0xFF;
		buf[off+4] = (mvPerAdcQ16 >>> 24) & 0xFF;
		buf[off+5] = (mvPerAdcQ16 >>> 16) & 0xFF;
		buf[off+6] = (mvPerAdcQ16 >>>  8) & 0xFF;
		buf[off+7] = (mvPerAdcQ16 >>>  0) & 0xFF;
	}

	const crc = crcCCITT(buf.subarray(0, 36));
	buf[36] = (crc >> 8) & 0xFF;
	buf[37] =  crc       & 0xFF;

	const hex     = Array.from(buf).map(b => b.toString(16).padStart(2, '0')).join('');
	const payload = 'I|' + hex + '|';
	SendSysEx(payload.split('').map(c => c.charCodeAt(0)));

	if (statusEl) statusEl.textContent = 'Saving…';
}

////////////////////////////////////////////////////////////
// EEPROM calibration save (pitch modes only)

// CRC-CCITT (poly 0x1021, init 0xFFFF) - matches firmware CRCencode()
function crcCCITT(buf)
{
	let crc = 0xFFFF;
	for (let i = 0; i < buf.length; i++)
	{
		crc ^= (buf[i] << 8) & 0xFFFF;
		for (let bit = 0; bit < 8; bit++)
		{
			if (crc & 0x8000) crc = ((crc << 1) ^ 0x1021) & 0xFFFF;
			else              crc =  (crc << 1)            & 0xFFFF;
		}
	}
	return crc;
}

// Derive 5 (voltage_tenths, dacSetting) calibration points from a channel's regression.
// 0V reference = baseHz, the frequency the oscillator played before CV was connected.
// dacSetting = 262143 - cv_raw_signed  (unsigned 19-bit representation used by EEPROM).
function buildChannelCalPoints(slope, intercept, f0)
{
	const LOG2_F0 = Math.log2(f0);
	const vA   = slope * CAL_MIN_CV + intercept - LOG2_F0;
	const vB   = slope * CAL_MAX_CV + intercept - LOG2_F0;
	const vLo  = Math.min(vA, vB);
	const vHi  = Math.max(vA, vB);

	const N        = 5;
	const tenthLo  = Math.ceil(vLo  * 10 + 0.5);
	const tenthHi  = Math.floor(vHi * 10 - 0.5);
	const step     = (tenthHi - tenthLo) / (N - 1);

	const points = [];
	for (let i = 0; i < N; i++)
	{
		const vTenths    = Math.round(tenthLo + i * step);
		const v          = vTenths / 10.0;
		const hz         = f0 * Math.pow(2, v);
		const cvRaw      = (Math.log2(hz) - intercept) / slope;
		const dacSetting = Math.round(262143 - cvRaw);
		points.push({ vTenths, dacSetting });
	}
	return points;
}

function saveCalToEEPROM()
{
	const statusEl = document.getElementById(`eepromStatus-${calMode}`);

	if (calData[0].length < 2 || calData[1].length < 2)
	{
		if (statusEl) statusEl.textContent = 'Need calibration data for both channels.';
		return;
	}

	const buf = new Uint8Array(88);

	// Header: magic number 2001 (big-endian), version 0
	buf[0] = (2001 >> 8) & 0xFF;
	buf[1] =  2001       & 0xFF;
	buf[2] = 0;
	buf[3] = 0;

	for (let ch = 0; ch < 2; ch++)
	{
		const data  = calData[ch];
		const cvs   = data.map(d => d.cv);
		const l2hz  = data.map(d => Math.log2(d.hz));
		const { slope, intercept } = linReg(cvs, l2hz);
		const points = buildChannelCalPoints(slope, intercept, baseHz[ch]);

		let off = 4 + 41 * ch;  // channel 0 -> byte 4, channel 1 -> byte 45
		buf[off++] = points.length;
		for (const p of points)
		{
			buf[off++] = p.vTenths & 0xFF;
			buf[off++] = (p.dacSetting >>> 24) & 0xFF;
			buf[off++] = (p.dacSetting >>> 16) & 0xFF;
			buf[off++] = (p.dacSetting >>>  8) & 0xFF;
			buf[off++] = (p.dacSetting >>>  0) & 0xFF;
		}
	}

	// CRC-CCITT over bytes 0–85, stored big-endian at bytes 86–87
	const crc = crcCCITT(buf.subarray(0, 86));
	buf[86] = (crc >> 8) & 0xFF;
	buf[87] =  crc       & 0xFF;

	// Send as SysEx: E|<176 hex chars>|
	const hex     = Array.from(buf).map(b => b.toString(16).padStart(2, '0')).join('');
	const payload = 'E|' + hex + '|';
	SendSysEx(payload.split('').map(c => c.charCodeAt(0)));

	if (statusEl) statusEl.textContent = 'Saving\u2026';
}
