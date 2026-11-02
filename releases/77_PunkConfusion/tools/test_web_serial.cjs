// Exercise the actual page with a simulated WebSerial device; no hardware needed.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

function element() {
  const children = new Map();
  return {
    value: "2mb",
    textContent: "",
    listeners: {},
    addEventListener(name, callback) { this.listeners[name] = callback; },
    querySelector(selector) {
      if (!children.has(selector)) children.set(selector, element());
      return children.get(selector);
    },
    cloneNode() { return element(); },
    append() {},
    content: { firstElementChild: { cloneNode: () => element() } }
  };
}

const document = element();
const context = vm.createContext({ document, navigator: {}, TextDecoder, setTimeout, clearTimeout });
vm.runInContext(fs.readFileSync(path.join(__dirname, "../web/app.js"), "utf8"), context);
const evaluate = (code) => vm.runInContext(code, context);
const tick = () => new Promise(setImmediate);

function device() {
  let controller;
  const writes = [];
  return {
    readable: new ReadableStream({ start(c) { controller = c; } }),
    writable: { getWriter: () => ({
      write: async (bytes) => { writes.push(Buffer.from(bytes)); },
      releaseLock() {}
    }) },
    writes,
    async open(options) { assert.equal(options.baudRate, 115200); },
    async setSignals(signals) { assert.equal(signals.dataTerminalReady, true); },
    async close() { this.closed = true; },
    send(text) { controller.enqueue(new TextEncoder().encode(text)); },
    end() { controller.close(); }
  };
}

async function connect(card) {
  context.navigator.serial = { requestPort: async () => card };
  await document.querySelector("#connect").listeners.click();
  assert.equal(card.writes[0].toString(), "PCID");
}

(async () => {
  // Connect long after boot: the page requests identification explicitly.
  const card = device();
  await connect(card);
  for (const byte of "PUNKCONF LOADER READY\r\nSAMPLE_BANK_BYTES 14680064\r\n") card.send(byte);
  await tick();
  assert.equal(evaluate("loaderGreetingSeen"), true);
  assert.equal(document.querySelector("#firmwareNotice").className, "notice ok");
  assert.equal(document.querySelector("#buildSelect").value, "16mb");

  // Replies must survive both split packets and a full, truncated display log.
  evaluate('appendLog("x".repeat(4000))');
  const ready = evaluate('waitForSerial(/OK SEND\\s+\\d+/, 1000)');
  card.send("OK SE");
  card.send("ND 128\r\n");
  assert.equal((await ready).match[0], "OK SEND 128");
  const done = evaluate('waitForSerial(/OK DONE/, 1000)');
  evaluate('appendLog("Instructions mentioning OK DONE are not a device reply")');
  assert.equal(evaluate("serialWaiters.length"), 1);
  card.send("OK WRITING\nOK DONE\n");
  assert.equal((await done).match[0], "OK DONE");

  const disconnected = assert.rejects(evaluate('waitForSerial(/NEVER/, 1000)'), /disconnected/);
  card.end();
  await disconnected;
  await tick();
  assert.equal(card.closed, true);
  assert.equal(evaluate("port"), null);
  assert.equal(document.querySelector("#connect").disabled, false);

  // A fresh connection discards partial lines from the previous device.
  const second = device();
  await connect(second);
  second.send("PUNKCONF LOADER READY\nSAMPLE_BANK_BYTES 1048576\n");
  await tick();
  assert.equal(evaluate("loaderGreetingSeen"), true);
  assert.equal(document.querySelector("#buildSelect").value, "2mb");
  second.end();
  await tick();

  context.navigator.serial.requestPort = async () => { throw new Error("Chooser cancelled"); };
  await document.querySelector("#connect").listeners.click();
  assert.equal(document.querySelector("#connect").disabled, false);
  console.log("WebSerial checks passed: identification, fragmented replies, full log, disconnect, reconnect, chooser cancellation.");
})().catch((error) => { console.error(error); process.exitCode = 1; });
