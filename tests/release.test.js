'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file));
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

test('individual flash images match the release checksum manifest', () => {
  const manifest = JSON.parse(read('firmware/flash-manifest.json'));
  assert.equal(manifest.flash_settings.flash_size, '8MB');
  assert.equal(manifest.files.length, 4);
  for (const part of manifest.files) assert.equal(sha(read(`firmware/${part.file}`)), part.sha256);
});
test('merged browser image contains the tested app/partition/PAA and blank credential storage', () => {
  const image = read('firmware/install-merged.bin');
  assert.equal(image.length, 0x6b0000);
  assert.equal(sha(image), read('firmware/install-sha256.txt').toString().split(/\s/)[0]);
  for (const [offset, file] of [[0x8000, '2-partition-table.bin'], [0x30000, '1-c6_matter_hub.bin'], [0x630000, '3-paa_cert.bin']]) {
    const expected = read(`firmware/${file}`);
    assert.deepEqual(image.subarray(offset, offset + expected.length), expected);
  }
  // Both NVS partitions are erased padding, never copied from a physical board.
  assert.ok(image.subarray(0x9000, 0xf000).every(byte => byte === 0xff));
  assert.ok(image.subarray(0x10000, 0x30000).every(byte => byte === 0xff));
});
test('browser installer restricts chip family and resolves the merged image', () => {
  const manifest = JSON.parse(read('setup/manifest.json'));
  assert.equal(manifest.builds.length, 1);
  assert.equal(manifest.builds[0].chipFamily, 'ESP32-C6');
  assert.equal(manifest.builds[0].parts[0].offset, 0);
  assert.ok(fs.existsSync(path.resolve(root, 'setup', manifest.builds[0].parts[0].path)));
});
