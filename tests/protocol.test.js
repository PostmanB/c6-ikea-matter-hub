'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const P = require('../setup/protocol.js');

function status(state, {paired = true, listening = true, storage = 'ok', role = 4, srp = 1} = {}) {
  P.applyLine(state, `I (100) hub: Heap free=197300 largest=172032 min=180000 pairing=idle storage=${storage}`);
  for (const [index, slot] of P.slots.entries()) {
    P.applyLine(state, `I (100) hub: ${slot} node=0x${(256 + index).toString(16)} paired=${+paired} onEP=${paired ? 1 : 65535} offEP=${paired && slot !== 'bulb' ? 2 : 65535} listening=${+listening}`);
  }
  P.applyLine(state, `I (100) hub: Thread role=${role} SRP=${srp}`, 1234);
  P.applyLine(state, `I (100) lighting: State subscribed=${+listening} level=${listening ? 127 : -1} range=1..254 colorFeatures=0x1f capabilities=0x1f RGB=1 white=1 mired=153..555 preset=white 2700K holding=-1`);
}
test('fresh state cannot pair or report ready', () => {
  const s = P.initialState();
  assert.equal(P.canPair(s, 'bulb', true, null), false);
  assert.equal(P.operational(s), false);
});
test('native console status restores all four devices and capabilities', () => {
  const s = P.initialState(); status(s);
  assert.equal(P.operational(s), true);
  assert.equal(s.rgb, true); assert.equal(s.white, true);
  assert.equal(s.level, 127); assert.equal(s.lastStatus, 1234);
  assert.equal(P.canPair(s, 'door', true, null), false);
});
test('pair bulb first; serialize pairing; require storage and Thread', () => {
  const s = P.initialState(); status(s, {paired: false, listening: false});
  assert.equal(P.canPair(s, 'bulb', true, null), true);
  assert.equal(P.canPair(s, 'door', true, null), false);
  P.applyLine(s, 'hub: bulb node=0x100 paired=1 onEP=1 offEP=65535 listening=0');
  assert.equal(P.canPair(s, 'door', true, null), true);
  assert.equal(P.canPair(s, 'door', true, {slot: 'desk'}), false);
  s.pairing = 'desk'; assert.equal(P.canPair(s, 'door', true, null), false);
  s.pairing = 'idle'; s.storage = 'ERROR'; assert.equal(P.canPair(s, 'door', true, null), false);
  s.storage = 'ok'; s.role = 1; assert.equal(P.canPair(s, 'door', true, null), false);
  s.role = 4; s.srp = 0; assert.equal(P.canPair(s, 'door', true, null), false);
  s.srp = 1; assert.equal(P.canPair(s, 'door', false, null), false);
});
test('saved pairing without mapped endpoints is not complete', () => {
  const s = P.initialState(); status(s);
  P.applyLine(s, 'hub: bedside node=0x103 paired=1 onEP=65535 offEP=65535 listening=0');
  assert.equal(P.operational(s), false);
  assert.equal(P.isMapped(s, 'bedside'), false);
  P.applyLine(s, 'hub: bedside commissioned and persisted; discovering endpoints');
  assert.equal(P.operational(s), false);
});
test('lost bulb subscription clears ready and unknown level parses', () => {
  const s = P.initialState(); status(s);
  P.applyLine(s, 'lighting: State subscribed=0 level=-1 range=1..254 colorFeatures=0x0 capabilities=0x0 RGB=0 white=0');
  assert.equal(P.operational(s), false); assert.equal(s.level, -1); assert.equal(s.rgb, false);
});
test('failed pairing becomes retryable; startup signals and events parse', () => {
  const s = P.initialState(); s.pairing = 'door';
  P.applyLine(s, '\x1b[31mE (1) hub: door not found in pairing mode\x1b[0m\r');
  assert.equal(s.pairing, 'idle'); assert.match(s.error, /not found/);
  P.applyLine(s, 'hub: door listening (subscription=32)');
  P.applyLine(s, 'hub: door button ep=1 event=9 type=1 count=2', 1000);
  assert.deepEqual(s.activity.door, {type: 1, count: 2, timestamp: 1000});
  P.applyLine(s, 'hub: READY: bulb acknowledged two startup flashes; all three remotes connected');
  assert.equal(s.readySignal, 'done');
});
test('manual codes normalize; command injection and QR are rejected', () => {
  assert.equal(P.normalizeCode(' 1234-567-8901 '), '12345678901');
  assert.equal(P.normalizeCode('123456789012345678901'), '123456789012345678901');
  for (const invalid of ['12345', 'MT:EXAMPLE', '12345678901\non', '12345678901;off']) {
    assert.throws(() => P.normalizeCode(invalid));
  }
});
test('diagnostics hide both command echoes and known code variants', () => {
  assert.equal(P.redact('matter esp hub pair door 12345678901'), 'matter esp hub pair door [code hidden]');
  assert.equal(P.redact('hub: payload 1234-567-8901', ['1234-567-8901']), 'hub: payload [code hidden]');
});
