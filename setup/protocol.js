(function (root) {
  'use strict';
  const slots = ['bulb', 'door', 'desk', 'bedside'];
  function initialState() {
    return {
      devices: Object.fromEntries(slots.map(slot => [slot, {paired: false, on: 65535, off: 65535, listening: false}])),
      pairing: 'unknown', storage: 'unknown', role: 0, srp: 0, bulbState: false,
      rgb: false, white: false, level: null, readySignal: 'unknown', recognized: false,
      ack: 0, activity: {}, error: '', lastStatus: 0,
      networkFirmware: false, wifiConfigured: false, wifiConnected: false,
      homeConfigured: false, homeRunning: false, optionalStorage: 'unknown', clock: 'unsynchronized',
      scheduleEnabled: false, scheduleTime: '06:00',
      networkStep: 0, networkError: 'ESP_OK', disconnectReason: 0,
      scanSupported: false, wifiScanState: 'idle', wifiScanError: '', wifiNetworks: [], scanTotal: 0,
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
    if (/home: Wi-Fi scan available=1/.test(line)) state.scanSupported = true;
    if (/home: Wi-Fi scan (requested|started)$/.test(line)) {
      state.scanSupported = true; state.wifiScanState = 'running'; state.wifiScanError = '';
      state.wifiNetworks = []; state.scanTotal = 0;
    }
    if ((m = line.match(/home: Wi-Fi AP ssid=([0-9a-f]{2,64}) rssi=(-?\d+) auth=(\d+) channel=(\d+)$/i)) && state.wifiScanState === 'running') {
      const hex = m[1].toLowerCase();
      if (hex.length % 2 === 0 && !hex.match(/../g).includes('00')) {
        const ssid = new TextDecoder().decode(Uint8Array.from(hex.match(/../g), byte => parseInt(byte, 16)));
        const network = {hex, ssid, rssi: +m[2], auth: +m[3], channel: +m[4]};
        const old = state.wifiNetworks.findIndex(value => value.hex === hex);
        if (old >= 0 && state.wifiNetworks[old].rssi < network.rssi) state.wifiNetworks[old] = network;
        else if (old < 0 && state.wifiNetworks.length < 20) state.wifiNetworks.push(network);
        state.wifiNetworks.sort((a, b) => b.rssi - a.rssi);
      }
    }
    if ((m = line.match(/home: Wi-Fi scan done count=(\d+) total=(\d+)$/))) {
      state.wifiScanState = 'done'; state.scanTotal = +m[2];
    }
    if ((m = line.match(/home: Wi-Fi scan failed error=(\w+) status=(\d+)$/))) {
      state.wifiScanState = 'error'; state.wifiScanError = m[1];
    }
    if ((m = line.match(/home: Network step=(\d+) error=(\w+) disconnectReason=(\d+)/))) {
      state.networkStep = +m[1]; state.networkError = m[2]; state.disconnectReason = +m[3];
    }
    if ((m = line.match(/home: Wi-Fi configured=(\d) connected=(\d) HomeKit configured=(\d) running=(\d) optionalStorage=(\w+)/))) {
      state.networkFirmware = true; state.wifiConfigured = m[1] === '1'; state.wifiConnected = m[2] === '1';
      state.homeConfigured = m[3] === '1'; state.homeRunning = m[4] === '1'; state.optionalStorage = m[5];
    }
    if ((m = line.match(/home: Clock=(.+) zone=Europe\/Bratislava schedule=(on|off) (\d{2}:\d{2}) lastDay=(\d+)/))) {
      state.clock = m[1]; state.scheduleEnabled = m[2] === 'on'; state.scheduleTime = m[3];
    }
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
    result = result.replace(/(matter esp hub (?:wifi|home))\s+[^\r\n]+/gi, '$1 [settings hidden]');
    result = result.replace(/(home: Wi-Fi AP ssid=)[0-9a-f]+/gi, '$1[name hidden]');
    for (const secret of secrets) {
      if (secret) result = result.split(secret).join('[code hidden]');
    }
    return result;
  }
  function networkProblem(state) {
    if (state.wifiConnected) return '';
    if (state.networkError !== 'ESP_OK') return `Wi-Fi startup error: ${state.networkError}. Keep diagnostics for troubleshooting.`;
    if (state.disconnectReason === 201) return 'Wi-Fi network not found. Check its exact name and that 2.4 GHz is enabled, then save Wi-Fi and restart.';
    if ([202, 204].includes(state.disconnectReason)) return 'Wi-Fi authentication failed. Re-enter the network password, then save Wi-Fi and restart.';
    if ([210, 211].includes(state.disconnectReason)) return 'Wi-Fi security is incompatible. Use a 2.4 GHz WPA2 or WPA2/WPA3 network.';
    return '';
  }
  const api = {slots, initialState, cleanLine, normalizeCode, applyLine, isMapped, canPair, operational, redact, networkProblem};
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.HubProtocol = api;
})(typeof globalThis === 'undefined' ? this : globalThis);
