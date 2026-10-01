(function (root) {
  'use strict';
  const slots = ['bulb', 'door', 'desk', 'bedside'];
  function initialState() {
    return {
      devices: Object.fromEntries(slots.map(slot => [slot, {paired: false, on: 65535, off: 65535, listening: false}])),
      pairing: 'unknown', storage: 'unknown', role: 0, srp: 0, bulbState: false,
      rgb: false, white: false, level: null, readySignal: 'unknown', recognized: false,
      ack: 0, activity: {}, error: '', lastStatus: 0,
    };
  }
  function cleanLine(value) {
    return value.replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, '').replace(/\r/g, '').trim();
  }
  function normalizeCode(value) {
    const code = value.replace(/[\s-]/g, '');
    if (!/^(?:\d{11}|\d{21})$/.test(code)) throw new Error('Enter the 11- or 21-digit Matter number printed on this device.');
    return code;
  }
  function applyLine(state, raw, timestamp = Date.now()) {
    const line = cleanLine(raw);
    let m;
    if ((m = line.match(/hub: (bulb|door|desk|bedside) node=0x[0-9a-f]+ paired=(\d) onEP=(\d+) offEP=(\d+) listening=(\d)/i))) {
      state.recognized = true;
      state.devices[m[1]] = {paired: m[2] === '1', on: +m[3], off: +m[4], listening: m[5] === '1'};
    }
    if ((m = line.match(/hub: Heap .*pairing=(\w+) storage=(\w+)/))) {
      state.recognized = true; state.pairing = m[1]; state.storage = m[2];
    }
    if ((m = line.match(/hub: Thread role=(\d+) SRP=(\d+)/))) {
      state.recognized = true; state.role = +m[1]; state.srp = +m[2]; state.lastStatus = timestamp;
    }
    if ((m = line.match(/hub: Startup ready signal: (.+)/))) state.readySignal = m[1];
    if ((m = line.match(/lighting: State subscribed=(\d) level=(-?\d+).*RGB=(\d) white=(\d)/))) {
      state.bulbState = m[1] === '1'; state.level = +m[2]; state.rgb = m[3] === '1'; state.white = m[4] === '1';
    }
    if ((m = line.match(/hub: (door|desk|bedside) listening \(/))) state.devices[m[1]].listening = true;
    if ((m = line.match(/hub: (bulb|door|desk|bedside) commissioned and persisted/))) state.devices[m[1]].paired = true;
    if ((m = line.match(/hub: (door|desk|bedside) button ep=.*type=(\d+) count=(\d+)/))) {
      state.activity[m[1]] = {type: +m[2], count: +m[3], timestamp};
    }
    if (/hub: Bulb acknowledged (on|off)|lighting: Bulb acknowledged (level|palette)/.test(line)) ++state.ack;
    if (/hub: READY:/.test(line)) state.readySignal = 'done';
    if (/hub:.*(Commissioning failed|Pairing session failed|not found in pairing mode|Cannot persist|Cannot stop discovery)/.test(line)) {
      state.error = line.replace(/^.*hub: /, '');
      if (/not found in pairing mode|Commissioning failed|Pairing session failed/.test(line)) state.pairing = 'idle';
    }
    return state;
  }
  function isMapped(state, slot) {
    const d = state.devices[slot];
    return d.paired && d.on !== 65535 && (slot === 'bulb' || d.off !== 65535);
  }
  function canPair(state, slot, connected, pending) {
    return connected && state.recognized && state.storage === 'ok' && state.role >= 2 && state.srp === 1 &&
      state.pairing === 'idle' && !pending && !state.devices[slot].paired &&
      (slot === 'bulb' || isMapped(state, 'bulb'));
  }
  function operational(state) {
    return slots.every(slot => isMapped(state, slot)) && slots.slice(1).every(slot => state.devices[slot].listening) && state.bulbState;
  }
  function redact(line, secrets = []) {
    let result = cleanLine(line).replace(/(matter esp hub pair \w+)\s+\S+/gi, '$1 [code hidden]');
    for (const secret of secrets) {
      if (secret) result = result.split(secret).join('[code hidden]');
    }
    return result;
  }
  const api = {slots, initialState, cleanLine, normalizeCode, applyLine, isMapped, canPair, operational, redact};
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.HubProtocol = api;
})(typeof globalThis === 'undefined' ? this : globalThis);
