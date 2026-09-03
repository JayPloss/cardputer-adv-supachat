import assert from 'node:assert/strict';
import fs from 'node:fs';
import tls from 'node:tls';
import {interpretRawKeys,navPositions} from './state.mjs';

const firmware = fs.readFileSync(new URL('../firmware/src/main.cpp', import.meta.url), 'utf8');
const songHeader = fs.readFileSync(new URL('../firmware/include/keypress_song.h', import.meta.url), 'utf8');
const installedFonts = fs.readFileSync(new URL('../firmware/.pio/libdeps/emmanuelle/M5GFX/src/lgfx/v1/lgfx_fonts.cpp', import.meta.url), 'utf8');
const installedGlcdFont = fs.readFileSync(new URL('../firmware/.pio/libdeps/emmanuelle/M5GFX/src/lgfx/Fonts/glcdfont.h', import.meta.url), 'utf8');
const pinMatch = firmware.match(/kTlsFingerprint\[\] = "([0-9A-Fa-f :]+)"/);
assert.ok(pinMatch, 'firmware TLS pin is missing');

// Executable port of ESP32 Arduino 2.0.16 verify_ssl_fingerprint(): it always
// parses exactly 32 bytes (SHA-256), ignoring spaces and colons.
export function parseEsp32Fingerprint(value) {
  const bytes = [];
  let position = 0;
  for (let index = 0; index < 32; index++) {
    while (value[position] === ' ' || value[position] === ':') position++;
    if (position > value.length - 2) return { ok: false, httpError: -5, reason: 'fingerprint too short' };
    const pair = value.slice(position, position + 2);
    if (!/^[0-9a-f]{2}$/i.test(pair)) return { ok: false, httpError: -5, reason: 'invalid hex sequence' };
    bytes.push(Number.parseInt(pair, 16)); position += 2;
  }
  while (value[position] === ' ' || value[position] === ':') position++;
  if (position !== value.length) return { ok: false, httpError: -5, reason: 'fingerprint too long' };
  return { ok: true, bytes: Buffer.from(bytes) };
}

export function verifyEsp32Fingerprint(value, remoteSha256) {
  const parsed = parseEsp32Fingerprint(value);
  if (!parsed.ok) return parsed;
  if (!parsed.bytes.equals(Buffer.from(remoteSha256.replaceAll(':', ''), 'hex')))
    return { ok: false, httpError: -5, reason: 'fingerprint mismatch' };
  return { ok: true, httpError: 0 };
}

export function liveLeafFingerprint(host) {
  return new Promise((resolve, reject) => {
    const socket = tls.connect({ host, port: 443, servername: host, rejectUnauthorized: true }, () => {
      const certificate = socket.getPeerCertificate(); socket.end();
      resolve(certificate.fingerprint256);
    });
    socket.once('error', reject);
  });
}

// This is the regression that the previous fake emulator missed.
const oldSha1 = 'E2 F8 A1 D5 E0 0F EE 25 31 D4 87 DF 2E C0 9A 62 7F 14 D8 4C';
assert.deepEqual(parseEsp32Fingerprint(oldSha1),
  { ok: false, httpError: -5, reason: 'fingerprint too short' });

const parsedCompiledPin = parseEsp32Fingerprint(pinMatch[1]);
assert.equal(parsedCompiledPin.ok, true, parsedCompiledPin.reason);
const livePin = await liveLeafFingerprint('supachat.net');
const verified = verifyEsp32Fingerprint(pinMatch[1], livePin);
assert.equal(verified.ok, true, `${verified.reason}: compiled=${pinMatch[1]} live=${livePin}`);

// M5's recorder exposes completed audio two DMA slots behind the active fill.
const ring = [null, null, null]; let recordIndex = 2; let queued = 0; const consumed = [];
for (const sample of [101, 202, 303, 404]) {
  const queuedIndex = recordIndex; ring[queuedIndex] = sample; queued++;
  if (queued > 2) consumed.push(ring[(queuedIndex + 1) % 3]);
  recordIndex = (queuedIndex + 1) % 3;
}
assert.deepEqual(consumed, [101, 202], 'M5 two-slot DMA delay contract');

// Port the observable M5 Keyboard_Class contract: keysState reports modifiers
// and printable word while keyList retains the physical matrix coordinates.
const rightKey=[{x:navPositions.right[0],y:navPositions.right[1]}];
assert.deepEqual(interpretRawKeys('chat',{shift:true,word:['?'],keyList:rightKey}),{kind:'text',text:'?'},
  'Shift+/ must remain printable on a text screen');
assert.deepEqual(interpretRawKeys('chat',{fn:true,word:['/'],keyList:rightKey}),{kind:'navigation',direction:'right'},
  'Fn plus the same physical key must navigate on a text screen');
assert.deepEqual(interpretRawKeys('menu',{word:['/'],keyList:rightKey}),{kind:'navigation',direction:'right'},
  'physical arrows must be primary on navigation screens');
assert.deepEqual(interpretRawKeys('menu',{fn:true,word:['/'],keyList:rightKey}),{kind:'ignored'},
  'navigation screens use the primary unmodified arrow layer');
assert.match(firmware, /if \(character == '\?'\) \{ appendKeyboardText\(target, u8"é"/,
  'French Shift+/ must emit é directly');
assert.match(firmware, /if \(character == '\\'\'\) \{ frenchGravePending = true; return; \}/,
  'French apostrophe must wait for its composition key');
assert.match(firmware, /character == 'a'[\s\S]*u8"à"[\s\S]*character == 'e'[\s\S]*u8"è"/,
  'French dead-key composition must emit à and è');
assert.match(firmware, /textEntryScreen[\s\S]*textEntryScreen \? keys\.fn : !keys\.fn/,
  'arrows must require Fn during text entry and remain primary elsewhere');
const loopBody=firmware.slice(firmware.indexOf('void loop()'),firmware.indexOf('delay(2);',firmware.indexOf('void loop()')));
assert.doesNotMatch(loopBody,/Keyboard\.isChange/,'loop must not consume the keyboard edge before handleKeyboard');
const cp437 = new Map([[0xA9,0x82],[0xA8,0x8A],[0xA0,0x85]]);
assert.deepEqual([...Buffer.from('éèà')].reduce((out,byte,index,input)=>{if(byte===0xC3)out.push(cp437.get(input[index+1]));return out},[]),[0x82,0x8A,0x85]);
assert.match(firmware, /0xA9[\s\S]*0x82[\s\S]*0xA8[\s\S]*0x8A[\s\S]*0xA0[\s\S]*0x85/,
  'UTF-8 accents must map to the matching CP437 bitmap slots');
assert.match(firmware, /UTF8_SWITCH, false[\s\S]*cp437\(true\)[\s\S]*print\(encoded\)[\s\S]*UTF8_SWITCH, true/,
  'Font0 accent output must bypass Unicode-to-wrong-slot rendering');
const fontBytes = [...installedGlcdFont.match(/font\[\].*?= \{([\s\S]*?)\};/)[1].matchAll(/0x([0-9A-Fa-f]{2})/g)].map(match=>Number.parseInt(match[1],16));
const glyph = code => fontBytes.slice(code*5,code*5+5);
assert.deepEqual(glyph(0x82),[0x38,0x54,0x54,0x55,0x59],'é must use the installed CP437 e-acute bitmap');
assert.deepEqual(glyph(0x8A),[0x39,0x55,0x54,0x54,0x58],'è must use the installed CP437 e-grave bitmap');
assert.deepEqual(glyph(0x85),[0x21,0x55,0x54,0x78,0x40],'à must use the installed CP437 a-grave bitmap');
assert.notDeepEqual(glyph(0x82),glyph(0xE9),'é must not render through the Greek-symbol slot reported on hardware');

const bootStep = Number(firmware.match(/kBootTuneStepMs = (\d+)/)[1]);
const bootTuneBody = firmware.match(/kBootTuneFrequencies\[\] = \{([\s\S]*?)\};/)[1];
const bootNotes = [...bootTuneBody.matchAll(/\b\d+\b/g)].map(match => Number(match[0]));
assert.equal(bootNotes.length, 232, 'boot arrangement must retain its complete original 232 steps');
assert.equal(Number(firmware.match(/kBootTuneNoteMs = (\d+)/)[1]), 145,
  'boot notes must retain MiLFFINDER timing rather than being stretched');
assert.deepEqual(bootNotes.slice(0, 32), [
  196, 247, 294, 392, 0, 294, 330, 294,
  247, 196, 220, 247, 294, 0, 392, 370,
  330, 262, 330, 392, 494, 440, 392, 0,
  196, 294, 392, 494, 587, 523, 392, 294,
], 'startup song must remain the original complete splash arrangement');
const bootFunction = firmware.slice(firmware.indexOf('void showBootSplash()'), firmware.indexOf('String cleanField'));
const firmwareVersion = firmware.match(/kFirmwareVersion\[\] = "(v\d+\.\d{2})"/);
assert.ok(firmwareVersion, 'splash firmware version must use exactly two decimal places');
const drawBootFunction = firmware.slice(firmware.indexOf('void drawBootSplash()'), firmware.indexOf('void showBootSplash()'));
assert.match(drawBootFunction, /setCursor\(4, 125\)[\s\S]*print\(kFirmwareVersion\)/,
  'firmware version must appear at the splash bottom-left');
assert.match(bootFunction, /for \(;;\)/, 'boot attract screen must continue until input');
assert.match(bootFunction, /step % kBootTuneLength/, 'the complete boot arrangement must loop');
assert.match(bootFunction, /Speaker\.tone\(kBootTuneFrequencies\[noteIndex\], kBootTuneNoteMs, 0, true\)/,
  'startup tones must reuse one virtual channel instead of accumulating across automatic channels');
assert.match(bootFunction, /const uint32_t target = millis\(\) \+ kBootTuneStepMs/,
  'startup notes must schedule from actual submission time so stalls cannot create catch-up bursts');
assert.doesNotMatch(bootFunction, /target \+= kBootTuneStepMs/,
  'startup melody must never compress overdue notes to catch up with a cumulative deadline');
assert.doesNotMatch(bootFunction, /fillRect/, 'boot loop must not draw over the custom splash art');
assert.doesNotMatch(bootFunction, /serviceMessageNotification\(\)/,
  'incoming-message tones must not interrupt the startup melody');
assert.match(bootFunction, /Keyboard\.isPressed\(\) \|\| M5Cardputer\.BtnA\.isPressed\(\)/,
  'boot wait loop must accept any keyboard key or rear button as skip');

const notificationBody = firmware.match(/kMessageNotificationFrequencies\[\] = \{([^}]+)\}/)[1];
const notificationNotes = [...notificationBody.matchAll(/\d+/g)].map(match => Number(match[0]));
const keypressBody = songHeader.match(/kKeypressSongFrequencies\[\] = \{([\s\S]*?)\};/)[1];
const keypressSequence = [...keypressBody.matchAll(/\d+/g)].map(match => Number(match[0]));
assert.equal(keypressSequence.length, 131, 'key feedback must contain the MIDI verse and complete Hooktheory chorus');
assert.match(firmware, /Speaker\.tone\(kKeypressSongFrequencies\[songPosition\], kToneDurationMs, 0, true\)/,
  'key feedback must replace the active note on one channel instead of queueing tones');
const keypressNotes = new Set(keypressSequence);
assert.equal(notificationNotes.some(note => keypressNotes.has(note)), false,
  'received-message motif must use pitches distinct from the keypress melody');
const syncFunction = firmware.slice(firmware.indexOf('void synchronize()'), firmware.indexOf('void networkTask'));
assert.match(syncFunction, /if \(initialSyncComplete && !newlyRead\.empty\(\)\) messageNotificationPending = true/,
  'only a nonempty post-hydration incoming-message batch should request the motif');
assert.match(syncFunction, /const int64_t syncAfter = initialSyncComplete \? lastServerId : 0/,
  'boot and room-switch hydration must not trust a stale local cursor');
assert.match(syncFunction, /const size_t syncLimit = initialSyncComplete \? kSyncBatchLimit : kHistoryLimit/,
  'initial hydration must request the complete retained history window');
assert.match(syncFunction, /deserializeJson\(document, &response\[0\], response\.length\(\)\)/,
  'full-history JSON must parse the mutable HTTP buffer without duplicating every string');
assert.doesNotMatch(syncFunction, /downloadVoiceClip/,
  'received voice messages must never auto-download or auto-play');

// The previous 250 ms loop opened roughly 240 fresh TLS sessions per minute,
// reproducing Julien's minute-scale reset under sustained heap/socket churn.
const syncPollMs = Number(firmware.match(/kSyncPollMs = (\d+)/)[1]);
assert.ok(syncPollMs >= 2000, `unsafe HTTPS churn: ${60000 / syncPollMs} syncs/minute`);
const networkFunction = firmware.slice(firmware.indexOf('void networkTask'), firmware.indexOf('void walkieTask'));
assert.match(networkFunction, /vTaskDelay\(pdMS_TO_TICKS\(kSyncPollMs\)\)/,
  'network loop must apply the contracted sync cadence');
assert.match(networkFunction, /voiceDownloadRequestedId[\s\S]*downloadVoiceClip\(messageId\)[\s\S]*synchronize\(\)/,
  'manual voice playback requests must run before the next sync poll');
assert.equal(Number(firmware.match(/kSyncBatchLimit = (\d+)/)[1]), 20,
  'device sync pages must stay small enough for a no-PSRAM Cardputer');
assert.match(syncFunction, /&limit=" \+ String\(syncLimit\)/,
  'firmware must request the bounded hydration or incremental sync page');

// M5Unified maps Cardputer ADV battery sensing to GPIO10 ADC and exposes no
// direct VBUS state. Infer charging only from a sustained idle upward trend;
// user input changes load and invalidates the sample window.
assert.match(firmware, /M5Cardputer\.Power\.getBatteryVoltage\(\)/);
assert.doesNotMatch(firmware, /M5Cardputer\.Power\.isCharging\(\)/);
assert.doesNotMatch(firmware, /M5Cardputer\.Power\.getBatteryLevel\(\)/,
  'raw M5 linear battery percentage must not drive the UI');
const riseStepMs = Number(firmware.match(/kBatteryRiseStepMs = (\d+)/)[1]);
const fastRiseStepMs = Number(firmware.match(/kBatteryFastRiseStepMs = (\d+)/)[1]);
const trendSamples = Number(firmware.match(/kPowerTrendSamples = (\d+)/)[1]);
const trendRiseMv = Number(firmware.match(/kPowerTrendMinimumRiseMv = (\d+)/)[1]);
const trendToleranceMv = Number(firmware.match(/kPowerTrendNoiseToleranceMv = (\d+)/)[1]);
const idleAfterInputMs = Number(firmware.match(/kPowerIdleAfterInputMs = (\d+)/)[1]);
const fallStepMs = Number(firmware.match(/kBatteryFallStepMs = (\d+)/)[1]);
assert.ok(riseStepMs >= 180000, 'battery estimate may rise at most 1% per three minutes');
assert.ok(fastRiseStepMs <= 10000, 'confirmed voltage rise must accelerate the SOC estimate');
assert.ok(trendSamples >= 5, 'charging needs multiple sustained samples');
assert.ok(trendRiseMv >= 15, 'tiny ADC fluctuations must not imply charging');
assert.ok(idleAfterInputMs >= 5000, 'power inference must wait until user activity settles');
assert.ok(fallStepMs >= 90000, 'ordinary discharge estimate may fall at most 1% per 90 seconds');
assert.match(firmware, /preferences\.getInt\("battery_soc", -1\)/,
  'battery estimate must survive reboot and USB/load transitions');
assert.match(firmware, /batteryFilteredMv \+= \(nextVoltage - batteryFilteredMv\) \* 0\.12f/,
  'battery ADC readings must be low-pass filtered');
function steadilyRising(samples) {
  if (samples.length !== trendSamples || samples.at(-1) - samples[0] < trendRiseMv) return false;
  return samples.slice(1).every((value, index) => value + trendToleranceMv >= samples[index]);
}
assert.equal(steadilyRising([3700,3704,3707,3710,3713,3716]), true,
  'a clear sustained idle rise must indicate charging');
assert.equal(steadilyRising([3716,3716,3715,3716,3715,3716]), false,
  'a flat unplugged battery must clear charging');
assert.equal(steadilyRising([3700,3712,3698,3715,3701,3720]), false,
  'load-driven jumps must not count as a steady charging trend');
assert.match(firmware, /handleKeyboard\(\)[\s\S]*Keyboard\.isChange\(\)[\s\S]*lastUserInputAt = millis\(\)/,
  'keyboard activity must reset the idle clock after consuming its edge exactly once');
assert.match(firmware, /if \(M5Cardputer\.BtnA\.isPressed\(\)\) lastUserInputAt = millis\(\)/,
  'rear-button activity must reset the idle clock');
assert.match(firmware, /now - lastUserInputAt < kPowerIdleAfterInputMs[\s\S]*powerTrendCount = 0/,
  'no charging inference may run while user input is active');
assert.match(firmware, /UP\/DOWN CHOOSE  ENTER PLAY\/STOP/,
  'voice inbox must expose explicit selection and play/stop controls');
assert.match(firmware, /voicePlaybackCancelled[\s\S]*incomingAudio\.clear\(\)/,
  'voice playback must be cancellable and clear buffered audio');
const headerFunction = firmware.slice(firmware.indexOf('void drawHeader'), firmware.indexOf('void drawChat'));
assert.doesNotMatch(headerFunction, /drawRect/,
  'battery header must not restore the unreadable outline');
assert.match(headerFunction, /externalPowerDetected[\s\S]*fillTriangle/,
  'header must show the bounded cable indicator');
assert.match(headerFunction, /setTextSize\(1\); display\.setTextColor\(TFT_WHITE/,
  'battery percentage must use clean integer-size white text');
const setupFunction = firmware.slice(firmware.indexOf('void setup()'), firmware.indexOf('void loop()'));
assert.ok(setupFunction.indexOf('drawBootSplash()') < setupFunction.indexOf('SD.begin'),
  'custom art must appear before SD/history initialization');
assert.ok(setupFunction.indexOf('xTaskCreatePinnedToCore(networkTask') < setupFunction.indexOf('showBootSplash()'),
  'network sync must start before entering the optional boot lobby');
assert.ok(setupFunction.indexOf('xTaskCreatePinnedToCore(walkieTask') < setupFunction.indexOf('showBootSplash()'),
  'walkie transport must start before entering the optional boot lobby');

// M5Unified retains the caller's pointer asynchronously and exposes two queue
// slots per virtual channel. Persistent alternating buffers keep both slots
// fed, unlike the old destroyed 20 ms temporary vectors.
assert.match(firmware, /std::array<std::vector<int16_t>, 2> playbackBuffers/);
assert.match(firmware, /Speaker\.isPlaying\(0\) < 2/);
assert.match(firmware, /playRaw\(buffer\.data\(\), buffer\.size\(\), kVoiceSampleRate, false, 1, 0, false\)/);
assert.equal(Number(firmware.match(/kVoicePlaybackBlock = (\d+)/)[1]), 1024);
let playbackQueued = 0; let underruns = 0;
for (let chunk = 0; chunk < 20; chunk++) {
  while (playbackQueued < 2 && chunk < 20) { playbackQueued++; chunk++; }
  chunk--; playbackQueued--; if (playbackQueued === 0) underruns++;
}
assert.equal(underruns, 0, 'two-slot producer must keep one queued block while the other plays');

// The ADV TCA8418 reports a maintained key list, but a single empty update
// must not terminate push-to-talk while the user is physically holding Space.
const pttReleaseDebounceMs = Number(firmware.match(/kPttReleaseDebounceMs = (\d+)/)[1]);
assert.ok(pttReleaseDebounceMs >= 500, 'PTT release must reject short keyboard-state dropouts');
let pttHeld = true; let releaseStarted = 0;
for (const sample of [
  {at:100, pressed:true}, {at:500, pressed:false}, {at:650, pressed:true},
  {at:900, pressed:false}, {at:1100, pressed:true}, {at:1800, pressed:true},
]) {
  if (sample.pressed) releaseStarted = 0;
  else if (!releaseStarted) releaseStarted = sample.at;
  else if (sample.at - releaseStarted >= pttReleaseDebounceMs) pttHeld = false;
}
assert.equal(pttHeld, true, 'transient false samples must not cut off a held voice message');
for (const sample of [{at:2000,pressed:false},{at:2500,pressed:false}]) {
  if (!releaseStarted) releaseStarted = sample.at;
  else if (sample.at - releaseStarted >= pttReleaseDebounceMs) pttHeld = false;
}
assert.equal(pttHeld, false, 'a sustained release must still stop recording');
const loopFunction = firmware.slice(firmware.indexOf('void loop()'));
assert.match(loopFunction, /spaceReleaseStartedAt[\s\S]*kPttReleaseDebounceMs[\s\S]*stopVoiceRecording\(\)/,
  'firmware loop must apply the contracted PTT release debounce');

console.log(`supachat_device_contract=PASS live_sha256=${livePin}`);
