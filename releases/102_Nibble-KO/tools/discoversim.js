#!/usr/bin/env node
/* discoversim.js - model of how the browser tool FINDS the card.
 *
 * Runs the real discovery code: it extracts the block between the
 * `// >>> discovery` and `// <<< discovery` sentinels in web/index.html and
 * evaluates it against fake MIDI ports, so this tests the shipping source
 * rather than a copy of it that can drift.
 *
 * This exists because discovery used to match on the port NAME, and that is
 * wrong in a way no amount of reading catches:
 *
 *   Every Workshop System card ships the same VID/PID (2E8A:10C1), and macOS
 *   CoreMIDI caches a MIDI Studio name against that USB identity. A card that
 *   enumerates after a sibling therefore appears under the SIBLING's name. One
 *   was reported in the field as "MTMComputer", with the page sitting on
 *   "Waiting..." while the card was enumerated, in WebUI mode, and answering
 *   perfectly. Windows truncates names to ~31 chars or reports a generic "USB
 *   Audio Device"; ALSA truncates to "NIBBLE-KO (Work" and only matched
 *   /nibble/ by luck.
 *
 * The properties worth checking are not "is the card found" but:
 *
 *   - Identity comes from the ANSWER, so a card under a wrong name is found.
 *   - A LOOPBACK is not a card. An IAC bus echoes our own HELLO straight back,
 *     which is a real F0 7D ... F7 message from a real port and the single
 *     most likely thing to be mistaken for a reply.
 *   - A port that does not answer must not stall the search, including when
 *     its name is the most promising one there.
 *   - Every borrowed onmidimessage handler is given back on EVERY exit path.
 *     A probe that leaves a listener installed steals replies from
 *     attachMidiIn afterwards, which presents as "card did not respond" to a
 *     message the card in fact answered - the same failure shape the rxQueue
 *     comment in index.html describes.
 *
 * Usage: node tools/discoversim.js
 */

"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");

// An explicit path is how these checks get mutation-tested: copy the page,
// break one thing in it, and confirm the model notices. A check that has never
// been seen to fail is not yet a check.
const PAGE = process.argv[2] || path.join(__dirname, "..", "web", "index.html");

// ---------------------------------------------------------------------------
// Load the real thing

function loadDiscovery() {
  const src = fs.readFileSync(PAGE, "utf8");
  const a = src.indexOf("// >>> discovery");
  const b = src.indexOf("// <<< discovery");
  if (a < 0 || b < 0 || b < a)
    throw new Error("discovery sentinels not found in web/index.html - did the " +
                    "block get renamed? This model runs the page's own code and " +
                    "deliberately has no copy to fall back on.");

  const block = src.slice(a, b);
  const sandbox = {
    MFR: 0x7D,
    MSG: { HELLO: 0x01, INFO: 0x02 },
    setTimeout: setTimeout,
    clearTimeout: clearTimeout,
    console: console,
  };
  vm.createContext(sandbox);
  vm.runInContext(block, sandbox, { filename: "web/index.html [discovery]" });
  return vm.runInContext(
    "({ portNames, probeOne, probeForCard, NAME_HINT_RE, NAME_SKIP_RE })", sandbox);
}

// ---------------------------------------------------------------------------
// Fake MIDI

// A valid MSG_INFO: F0 7D 02 <18 payload> F7 = 21 bytes, which is the minimum
// connect() accepts. Shorter is a v1 card and must not be reported as a find.
function infoReply(len) {
  const d = new Uint8Array(len === undefined ? 21 : len);
  d[0] = 0xF0; d[1] = 0x7D; d[2] = 0x02;
  d[d.length - 1] = 0xF7;
  return d;
}

class FakeIn {
  constructor(name) { this.name = name; this.onmidimessage = null; }
  deliver(data) { if (this.onmidimessage) this.onmidimessage({ data: data }); }
}

// An output with a `respond(bytes)` hook deciding what, if anything, comes back
// and on which input. Replies arrive on a later turn of the event loop, as a
// real device's would.
class FakeOut {
  constructor(name, respond) {
    this.name = name;
    this.respond = respond || function () { return null; };
    this.sent = [];
  }
  send(bytes) {
    this.sent.push(Array.from(bytes));
    const r = this.respond(Array.from(bytes));
    if (r) setTimeout(() => r.port.deliver(r.data), 1);
  }
}

function access(outs, ins) {
  return {
    outputs: new Map(outs.map((p, i) => [String(i), p])),
    inputs: new Map(ins.map((p, i) => [String(i), p])),
  };
}

// ---------------------------------------------------------------------------
// Harness

let failures = 0;
function check(label, cond, detail) {
  if (cond) { console.log("  ok   " + label); return; }
  failures++;
  console.log("  FAIL " + label + (detail ? "\n         " + detail : ""));
}

const D = loadDiscovery();

// These are the real code paths, so the model pays the real waits. The default
// 400ms across several ports makes for a slow test; the logic is unchanged.
const TIMEOUT = 25;

async function main() {
  console.log("discoversim - browser-side card discovery\n");

  // -- 1. The reported bug ---------------------------------------------------
  console.log("a card under a cached name is still found");
  {
    const cardIn = new FakeIn("MTMComputer");
    const cardOut = new FakeOut("MTMComputer", () => ({ port: cardIn, data: infoReply() }));
    const hit = await D.probeForCard(access([cardOut], [cardIn]), TIMEOUT);

    check("found by its answer, not its name", hit !== null,
          "this is the field report: CoreMIDI renamed the port and the page went blind");
    check("the name would NOT have matched the old test",
          !/nibble|workshop|pico/i.test("MTMComputer"));
    check("paired with the input that replied", hit !== null && hit.in === cardIn);
    check("handler handed back", cardIn.onmidimessage === null);
  }

  // -- 2. A loopback is not a card -------------------------------------------
  console.log("\nan echo of our own HELLO is not mistaken for a card");
  {
    // Named so it is skipped outright AND made to echo, so the check still
    // means something if the skip list is ever narrowed.
    const iacIn = new FakeIn("IAC Driver Bus 1");
    const iacOut = new FakeOut("IAC Driver Bus 1",
                               (b) => ({ port: iacIn, data: Uint8Array.from(b) }));
    check("the IAC bus is skipped by name", D.NAME_SKIP_RE.test("IAC Driver Bus 1"));

    const hit = await D.probeForCard(access([iacOut], [iacIn]), TIMEOUT);
    check("no card reported", hit === null);
    check("nothing was even sent to it", iacOut.sent.length === 0);

    // The same echo on a port the skip list does not know about: now the
    // message-type and length test has to carry it alone.
    const echoIn = new FakeIn("Some Other Interface");
    const echoOut = new FakeOut("Some Other Interface",
                                (b) => ({ port: echoIn, data: Uint8Array.from(b) }));
    const hit2 = await D.probeForCard(access([echoOut], [echoIn]), TIMEOUT);
    check("a bare echo on an unknown port is still rejected", hit2 === null,
          "F0 7D 01 F7 is a real SysEx message from a real port; the MSG_INFO " +
          "type test is the only thing telling it apart from a reply");

    // The two halves of the test guard different things, and each needs a case
    // that fails without it: the type test alone lets a v1 card through, and
    // the length test alone lets ANY long message from another 0x7D device
    // through - including this card's own MSG_LIBDET burst, arriving late from
    // a previous session's page in another tab.
    const noisyIn = new FakeIn("Some Other Interface");
    const noisy = infoReply(30);
    noisy[2] = 0x25;   // MSG_LIBDET
    const noisyOut = new FakeOut("Some Other Interface", () => ({ port: noisyIn, data: noisy }));
    const hit2b = await D.probeForCard(access([noisyOut], [noisyIn]), TIMEOUT);
    check("a long reply that is not MSG_INFO is rejected", hit2b === null);

    // A v1-length reply is a real card answering with a payload this page
    // cannot read. connect() already rejects it, so discovery must not claim a
    // find that the handshake will then refuse.
    const oldIn = new FakeIn("NIBBLE-KO (Workshop)");
    const oldOut = new FakeOut("NIBBLE-KO (Workshop)",
                               () => ({ port: oldIn, data: infoReply(17) }));
    const hit3 = await D.probeForCard(access([oldOut], [oldIn]), TIMEOUT);
    check("a short (v1) INFO is not accepted", hit3 === null);
  }

  // -- 3. A silent port must not stall the search ----------------------------
  console.log("\na promising name that never answers is passed over");
  {
    const deadIn = new FakeIn("NIBBLE-KO (Workshop)");
    const deadOut = new FakeOut("NIBBLE-KO (Workshop)");
    const cardIn = new FakeIn("USB Audio Device");
    const cardOut = new FakeOut("USB Audio Device", () => ({ port: cardIn, data: infoReply() }));

    const hit = await D.probeForCard(access([cardOut, deadOut], [cardIn, deadIn]), TIMEOUT);
    check("the hint-named port was tried FIRST despite being listed second",
          deadOut.sent.length === 1 && D.NAME_HINT_RE.test(deadOut.name),
          "the name still orders the search, so the ordinary case costs one round trip");
    check("the answering port won", hit !== null && hit.out === cardOut);
    check("every handler handed back",
          deadIn.onmidimessage === null && cardIn.onmidimessage === null);
  }

  // -- 4. Nothing answers ----------------------------------------------------
  console.log("\nwhen nothing answers, the port names are reportable");
  {
    const ins = [new FakeIn("IAC Driver Bus 1"), new FakeIn("Launchpad")];
    const outs = [new FakeOut("IAC Driver Bus 1"), new FakeOut("Launchpad")];
    const midi = access(outs, ins);

    const hit = await D.probeForCard(midi, TIMEOUT);
    check("no card reported", hit === null);

    const names = D.portNames(midi);
    check("names are readable for the diagnostic",
          names.outputs.join() === "IAC Driver Bus 1,Launchpad",
          "the silent Waiting... is what cost the reporter two sessions; the " +
          "page knew this list all along and never showed it");
    check("every handler handed back", ins.every((i) => i.onmidimessage === null));
    check("the skipped bus was left alone", outs[0].sent.length === 0);
  }

  // -- 5. Degenerate cases ---------------------------------------------------
  console.log("\ndegenerate cases");
  {
    const hit = await D.probeForCard(access([], []), TIMEOUT);
    check("nothing plugged in returns null rather than throwing", hit === null);

    // A port can disappear between being enumerated and being sent to - a card
    // dropped out of WebUI mode mid-probe does exactly this.
    const gone = new FakeOut("NIBBLE-KO (Workshop)");
    gone.send = () => { throw new Error("port closed"); };
    const hit2 = await D.probeForCard(access([gone], [new FakeIn("NIBBLE-KO (Workshop)")]),
                                      TIMEOUT);
    check("a port that throws on send does not break the search", hit2 === null);
  }

  console.log(failures ? "\n" + failures + " FAILED" : "\nall passed");
  process.exit(failures ? 1 : 0);
}

main().catch((e) => { console.error(e); process.exit(1); });
