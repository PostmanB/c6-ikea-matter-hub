'use strict';
const P = HubProtocol;
let hub = P.initialState();
let port = null, reader = null, writer = null, pollTimer = null;
let connected = false, closing = false, pendingPair = null, commandQueue = Promise.resolve();
let received = '', connectionTime = 0, messages = [], secrets = new Set();
const checked = new Set();
const labels = {bulb: 'KAJPLATS bulb', door: 'Door remote', desk: 'Desk remote', bedside: 'Bedside remote'};
const $ = id => document.getElementById(id);
for (const slot of P.slots) {
  const card = document.createElement('article');
  card.className = 'device';
  // All interpolated content below is fixed application text, never device input.
  card.innerHTML = `<div class="device-heading"><h3>${labels[slot]}</h3><span id="badge-${slot}" class="badge">Not paired</span></div>
    <p>${slot === 'bulb' ? 'Reset the bulb with six quick power-off/on cycles, ending on. It should blink.' : 'Hold the small system button inside the battery compartment for 10 seconds, until the red LED stops blinking.'}</p>
    <form id="pair-${slot}"><label for="code-${slot}">Printed Matter number</label><input id="code-${slot}" type="password" inputmode="numeric" autocomplete="off" maxlength="32" placeholder="11 digits, hyphens are OK" disabled><button id="button-${slot}" disabled>Pair ${slot === 'bulb' ? 'bulb' : 'remote'}</button></form><div id="activity-${slot}" class="activity"></div>`;
  $('devices').append(card);
  $(`pair-${slot}`).addEventListener('submit', event => { event.preventDefault(); pair(slot); });
}
for (const slot of P.slots.slice(1)) {
  const label = document.createElement('label');
  const checkbox = document.createElement('input');
  checkbox.type = 'checkbox'; checkbox.id = `tested-${slot}`; checkbox.disabled = true;
  checkbox.addEventListener('change', () => { checkbox.checked ? checked.add(slot) : checked.delete(slot); render(); });
  label.append(checkbox, document.createTextNode(`I tested On/Off, dimming with release, and colour changes on the ${labels[slot].toLowerCase()}.`));
  $('tests').append(label);
}
function notice(id, text, kind = '') { const el = $(id); el.textContent = text; el.className = `notice ${kind}`; }
function diagnostic(raw) {
  const line = P.redact(raw, [...secrets]);
  if (!/\b(hub|lighting|home):/.test(line) || /attribute cluster| ep=.*server=/.test(line)) return;
  messages.push(line); if (messages.length > 80) messages.shift();
  $('diagnostics').textContent = messages.join('\n');
}
function processLine(raw) {
  P.applyLine(hub, raw);
  diagnostic(raw);
  if (pendingPair) {
    if (P.isMapped(hub, pendingPair.slot) && (pendingPair.slot === 'bulb' || hub.devices[pendingPair.slot].listening)) {
      notice('pair-message', `${labels[pendingPair.slot]} is paired and ready.`, 'good');
      pendingPair = null;
    } else if (hub.error) {
      notice('pair-message', `${hub.error} Put that device back into pairing mode and try again when the hub is idle.`, 'error');
      if (hub.pairing === 'idle') pendingPair = null;
    }
  }
  render();
}
function render() {
  $('connect').disabled = connected || closing || !('serial' in navigator) || !window.isSecureContext;
  $('disconnect').disabled = !connected;
  $('refresh').disabled = !connected;
  const networkConfig = connected && hub.networkFirmware && hub.optionalStorage === 'ok' && !pendingPair && hub.pairing === 'idle';
  ['wifi-ssid', 'wifi-password', 'wifi-save', 'schedule-time', 'schedule-save', 'schedule-off', 'restart'].forEach(id => { $(id).disabled = !networkConfig; });
  ['home-pin', 'home-save'].forEach(id => { $(id).disabled = !networkConfig || hub.homeConfigured; });
  const networkProblem = P.networkProblem(hub);
  notice('network-status', !connected ? 'Connect the ESP to check clock and Apple Home settings.' : !hub.networkFirmware ? 'This firmware has no Wi-Fi/HomeKit configuration. The existing remotes still work.' :
    `${networkProblem ? `${networkProblem} ` : ''}Wi-Fi ${hub.wifiConnected ? 'connected' : hub.wifiConfigured ? 'saved; waiting for connection or restart' : 'not configured'} · HomeKit ${hub.homeRunning ? 'running' : hub.homeConfigured ? 'saved; restart to apply' : 'not configured'} · Clock ${hub.clock} · Daily turn-on ${hub.scheduleEnabled ? hub.scheduleTime : 'disabled'}`,
    hub.wifiConnected && hub.homeRunning ? 'good' : hub.optionalStorage === 'ERROR' || networkProblem ? 'error' : '');
  const readyNetwork = hub.recognized && hub.role >= 2 && hub.srp === 1 && hub.storage === 'ok';
  if (connected) {
    if (!hub.recognized) notice('connection', Date.now() - connectionTime > 20000 ? 'No hub response yet. Check the native USB socket, close other monitors, or press RESET once.' : 'USB connected. Waiting for the hub’s startup and console…');
    else if (hub.storage === 'unknown') notice('connection', 'Hub found. Reading saved pairing status…');
    else if (hub.storage !== 'ok') notice('connection', 'The hub reported a storage problem. Pairing is disabled; keep diagnostics and do not erase existing pairings.', 'error');
    else if (!readyNetwork) notice('connection', 'Hub found. Waiting for its Thread network to start…');
    else notice('connection', 'ESP connected. Local Thread network is ready.', 'good');
  }
  for (const slot of P.slots) {
    const d = hub.devices[slot];
    const mapped = P.isMapped(hub, slot), live = slot === 'bulb' ? hub.bulbState : d.listening;
    const badge = $(`badge-${slot}`);
    badge.textContent = !connected ? 'Reconnect to check' : !d.paired ? 'Not paired' : !mapped ? 'Finding controls' : live ? 'Ready' : 'Reconnecting';
    badge.className = `badge ${connected && mapped && live ? 'good' : ''}`;
    const canPair = P.canPair(hub, slot, connected, pendingPair);
    $(`code-${slot}`).disabled = !canPair;
    $(`button-${slot}`).disabled = !canPair;
    $(`button-${slot}`).textContent = d.paired ? 'Pairing saved' : pendingPair?.slot === slot ? 'Pairing…' : `Pair ${slot === 'bulb' ? 'bulb' : 'remote'}`;
    if (slot !== 'bulb') {
      $(`tested-${slot}`).disabled = !connected || !mapped || !live;
      const activity = hub.activity[slot];
      $(`activity-${slot}`).textContent = activity && connected ? 'Button event received' : '';
    }
  }
  const controlEnabled = connected && readyNetwork && P.isMapped(hub, 'bulb') && !pendingPair && hub.pairing === 'idle';
  document.querySelectorAll('.control').forEach(button => {
    button.disabled = !controlEnabled || (button.dataset.command.startsWith('color') && (!hub.bulbState || (!hub.rgb && !hub.white)));
  });
  $('brightness').disabled = !controlEnabled || !hub.bulbState;
  $('capabilities').textContent = hub.bulbState && connected ? `Bulb connected · brightness ${hub.level}/254 · ${hub.rgb ? 'RGB colours + ' : ''}${hub.white ? 'white tones' : 'no white-temperature control'}` : 'Bulb capabilities will appear after pairing.';
  if (pendingPair && Date.now() - pendingPair.started > 180000) {
    notice('pair-message', 'This pairing is taking longer than expected. Check the diagnostics and keep the remote awake. Wait for the hub to become idle before retrying.', 'error');
    if (hub.pairing === 'idle' && !hub.devices[pendingPair.slot].paired) pendingPair = null;
  }
  const operational = connected && P.operational(hub) && readyNetwork && !pendingPair;
  const complete = operational && checked.size === 3;
  notice('test-status', complete ? 'All four devices are connected and your three remote tests are checked.' : operational ? 'All devices connected. Test each remote and check the boxes above.' : 'Pair all four devices and wait for their connections.', complete ? 'good' : '');
  $('finish-summary').textContent = complete ? 'Setup is complete. Your ESP saved the pairings; you can now move everything into place.' : 'Finish pairing and check all three remotes first.';
}
async function send(command) {
  if (!writer || !connected) throw new Error('Connect the ESP first.');
  const sessionWriter = writer;
  const write = () => {
    if (!connected || writer !== sessionWriter) throw new Error('USB session ended. Reconnect to continue.');
    return sessionWriter.write(new TextEncoder().encode(`matter esp hub ${command}\r\n`));
  };
  const result = commandQueue.then(write);
  commandQueue = result.catch(() => {});
  return result;
}
async function pair(slot) {
  if (!P.canPair(hub, slot, connected, pendingPair)) return;
  try {
    const code = P.normalizeCode($(`code-${slot}`).value);
    secrets.add(code); secrets.add($(`code-${slot}`).value);
    $(`code-${slot}`).value = '';
    hub.error = ''; pendingPair = {slot, started: Date.now()};
    notice('pair-message', `Pairing ${labels[slot].toLowerCase()}… Keep it nearby and awake. This can take a couple of minutes.`);
    render();
    await send(`pair ${slot} ${code}`);
  } catch (error) { pendingPair = null; notice('pair-message', error.message, 'error'); render(); }
}
async function readLoop() {
  const decoder = new TextDecoder();
  try {
    while (!closing) {
      const {value, done} = await reader.read();
      if (done) break;
      received += decoder.decode(value, {stream: true});
      let split;
      while ((split = received.indexOf('\n')) >= 0) { processLine(received.slice(0, split)); received = received.slice(split + 1); }
      if (received.length > 16384) received = received.slice(-8192);
    }
  } catch (error) { if (!closing) notice('connection', `USB connection ended: ${error.message}. Reconnect to check saved pairings.`, 'error'); }
  finally {
    try { reader?.releaseLock(); } catch (_) {}
    reader = null;
    if (!closing) await disconnect(false);
  }
}
async function connect() {
  try {
    port = await navigator.serial.requestPort({filters: [{usbVendorId: 0x303a}]});
    await port.open({baudRate: 115200});
    await port.setSignals({dataTerminalReady: false, requestToSend: false});
    hub = P.initialState(); checked.clear();
    document.querySelectorAll('#tests input').forEach(input => { input.checked = false; });
    received = ''; messages = []; secrets.clear(); closing = false; connected = true;
    connectionTime = Date.now();
    writer = port.writable.getWriter(); reader = port.readable.getReader(); commandQueue = Promise.resolve();
    readLoop();
    await send('status');
    pollTimer = setInterval(() => { if (connected) send('status').catch(error => notice('connection', error.message, 'error')); render(); }, 3000);
    render();
  } catch (error) {
    notice('connection', error.name === 'NotFoundError' ? 'No device selected. Connect the native USB socket and try again.' : `Could not connect: ${error.message}`, 'error');
    await disconnect(false);
  }
}
async function disconnect(showMessage = true) {
  if (closing) return;
  closing = true; connected = false;
  clearInterval(pollTimer); pollTimer = null;
  try { await reader?.cancel(); } catch (_) {}
  try { await commandQueue; writer?.releaseLock(); } catch (_) {}
  writer = null;
  try { await port?.close(); } catch (_) {}
  port = null; reader = null; pendingPair = null; received = ''; secrets.clear();
  document.querySelectorAll('#devices input').forEach(input => { input.value = ''; });
  ['wifi-ssid', 'wifi-password', 'home-pin'].forEach(id => { $(id).value = ''; });
  closing = false;
  if (showMessage) notice('connection', 'Disconnected. Pairings stay saved on the ESP. Use UART/USB-UART for charger power.');
  render();
}
$('connect').addEventListener('click', connect);
$('disconnect').addEventListener('click', () => disconnect());
$('refresh').addEventListener('click', () => send('status').catch(error => notice('connection', error.message, 'error')));
document.querySelectorAll('.control').forEach(button => button.addEventListener('click', () => send(button.dataset.command).catch(error => notice('test-status', error.message, 'error'))));
$('brightness').addEventListener('input', () => { $('brightness-value').textContent = `${$('brightness').value}%`; });
$('brightness').addEventListener('change', () => send(`level ${$('brightness').value}`).catch(error => notice('test-status', error.message, 'error')));
const hex = value => [...new TextEncoder().encode(value)].map(byte => byte.toString(16).padStart(2, '0')).join('');
$('wifi-form').addEventListener('submit', async event => {
  event.preventDefault();
  try {
    const ssid = $('wifi-ssid').value, password = $('wifi-password').value;
    if (!ssid || new TextEncoder().encode(ssid).length > 32 || new TextEncoder().encode(password).length > 64 || (password && password.length < 8)) throw new Error('Use a Wi-Fi name of up to 32 bytes and an 8–64 character password, or an empty password for an open network.');
    secrets.add(password); secrets.add(hex(password)); secrets.add(ssid); secrets.add(hex(ssid));
    await send(`wifi ${hex(ssid)} ${password ? hex(password) : '-'}`);
    $('wifi-password').value = '';
    notice('network-message', 'Wi-Fi settings sent. Wait for “Wi-Fi configuration saved” in diagnostics, then restart to apply.');
  } catch (error) { notice('network-message', error.message, 'error'); }
});
$('home-form').addEventListener('submit', async event => {
  event.preventDefault();
  try {
    const pin = $('home-pin').value;
    if (!/^\d{8}$/.test(pin) || /^(\d)\1{7}$/.test(pin) || ['12345678', '87654321'].includes(pin)) throw new Error('Choose 8 digits; avoid repeated digits and counting sequences.');
    secrets.add(pin);
    await send(`home ${pin}`);
    $('home-pin').value = '';
    notice('network-message', 'HomeKit PIN sent. Wait for “HomeKit verifier saved” in diagnostics, then restart. Keep your PIN for adding the accessory in Home.');
  } catch (error) { notice('network-message', error.message, 'error'); }
});
$('schedule-form').addEventListener('submit', event => { event.preventDefault(); send(`schedule ${$('schedule-time').value}`).catch(error => notice('network-message', error.message, 'error')); });
$('schedule-off').addEventListener('click', () => send('schedule off').catch(error => notice('network-message', error.message, 'error')));
$('restart').addEventListener('click', () => send('restart').catch(error => notice('network-message', error.message, 'error')));
if (!('serial' in navigator) || !window.isSecureContext) { $('unsupported').hidden = false; $('connect').disabled = true; }
else render();
