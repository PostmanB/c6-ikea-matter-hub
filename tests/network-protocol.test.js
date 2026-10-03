'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const P = require('../setup/protocol.js');
test('legacy firmware does not expose unsupported network controls', () => {
  const s = P.initialState();
  P.applyLine(s, 'hub: Thread role=4 SRP=1');
  assert.equal(s.networkFirmware, false);
});
test('network diagnostics report saved configuration separately from active services', () => {
  const s = P.initialState();
  P.applyLine(s, 'home: Wi-Fi configured=1 connected=0 HomeKit configured=1 running=0 optionalStorage=ok');
  assert.equal(s.networkFirmware, true); assert.equal(s.wifiConfigured, true);
  assert.equal(s.wifiConnected, false); assert.equal(s.homeRunning, false);
  P.applyLine(s, 'home: Clock=2026-10-03 05:59:30 CEST zone=Europe/Bratislava schedule=on 06:00 lastDay=20261002');
  assert.equal(s.clock, '2026-10-03 05:59:30 CEST');
  assert.equal(s.scheduleEnabled, true); assert.equal(s.scheduleTime, '06:00');
});
test('passwords and HomeKit PINs are hidden even after the in-memory secret list is cleared', () => {
  assert.equal(P.redact('matter esp hub wifi 486f6d65 536563726574'), 'matter esp hub wifi [settings hidden]');
  assert.equal(P.redact('matter esp hub home 39167284'), 'matter esp hub home [settings hidden]');
  assert.equal(P.redact('home: HomeKit verifier saved; note your PIN; restart to apply'), 'home: HomeKit verifier saved; note your PIN; restart to apply');
});
test('Wi-Fi failure is actionable and an old disconnect reason clears on connection', () => {
  const s = P.initialState();
  assert.equal(P.networkProblem(s), '');
  P.applyLine(s, 'home: Network step=11 error=ESP_OK disconnectReason=201');
  assert.match(P.networkProblem(s), /network not found.*2\.4 GHz/);
  P.applyLine(s, 'home: Network step=11 error=ESP_OK disconnectReason=202');
  assert.match(P.networkProblem(s), /authentication failed/);
  P.applyLine(s, 'home: Wi-Fi configured=1 connected=1 HomeKit configured=1 running=1 optionalStorage=ok');
  assert.equal(P.networkProblem(s), '');
});
