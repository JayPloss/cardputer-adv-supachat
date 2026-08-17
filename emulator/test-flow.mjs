import assert from 'node:assert/strict';
import fs from 'node:fs';
import {SupaChatState,menuItems} from './state.mjs';
const s=new SupaChatState();
s.bootTick(58000);s.bootTick(250);assert.equal(s.bootElapsedMs,58250);const skipped=new SupaChatState();skipped.bootTick(900);skipped.skipBoot();skipped.bootTick(7100);assert.equal(skipped.bootElapsedMs,900);
s.right();assert.equal(s.currentRoom,'K-BUDS');assert.equal(s.roomNew[1],false);assert.equal(s.receive('Papa','Late Family response','papa-late','Family'),false);assert.equal(s.messages.length,0);s.left();assert.equal(s.currentRoom,'Family');s.menu();assert.equal(s.screen,'menu');s.down();assert.equal(s.menuSelection,1);s.enter();assert.equal(s.screen,'rooms');s.down();s.enter();assert.equal(s.currentRoom,'K-BUDS');
s.menu();s.menuSelection=3;s.enter();assert.equal(s.screen,'walkie');s.holdSpace();s.tick(1200);s.releaseSpace();assert.equal(s.clipReady,true);s.enter();assert.equal(s.replayAudible,true);
s.left();s.open('BACK TO CHAT');assert.equal(s.type('?',{shift:true}),'punctuation');assert.equal(s.draft,'?');const notices=s.notifications;s.receive('Papa','Dinner!');assert.equal(s.notifications,notices+1);
s.sync('io');assert.equal(s.network,'SYNC IO -1');s.sync('ok');assert.equal(s.network,'SYNCED');for(const item of menuItems){s.open(item);assert.ok(['chat','rooms','walkie','volume','networks','status'].includes(s.screen)||item==='SYNC NOW')}
const firmware=fs.readFileSync(new URL('../firmware/src/main.cpp',import.meta.url),'utf8');const capture=firmware.slice(firmware.indexOf('void captureVoice()'),firmware.indexOf('void playSamples('));
assert.match(firmware,/identity\.indexOf\("albie"\) >= 0\) return TFT_SKYBLUE/);assert.match(firmware,/participantColour\(message\.authorId, message\.authorName\)/);assert.match(capture,/voiceDmaQueued > 2/);assert.match(capture,/voiceDmaBlocks\[\(queuedIndex \+ 1\) % 3\]/);
assert.match(firmware,/client\.verify\(kTlsFingerprint, kApiHost\)/);assert.match(firmware,/walkieSocket\.beginSSL\(kApiHost, 443, walkiePath\.c_str\(\), kTlsFingerprint\)/);assert.match(firmware,/room_id/);assert.match(firmware,/X-Room-Id/);assert.match(firmware,/ScreenMode::Rooms/);
assert.match(firmware,/latest_message_id/);assert.match(firmware,/void switchRoom\(int direction\)/);assert.match(firmware,/if \(goLeft\) \{ switchRoom\(-1\)/);assert.match(firmware,/if \(goRight\) \{ switchRoom\(1\)/);
assert.match(firmware,/messageRoomId\.isEmpty\(\) \|\| messageRoomId != currentRoomId/);
assert.match(firmware,/roomHistoryPath\(currentRoomId/);assert.match(firmware,/message\.roomId = currentRoomId/);assert.match(firmware,/queuedRoomId != currentRoomId/);
const pin=firmware.match(/kTlsFingerprint\[\] = "([0-9A-F ]+)"/)[1].replaceAll(' ','');assert.equal(pin.length,64);assert.match(firmware,/navigationChord = !\(keys\.shift \|\| keys\.ctrl/);assert.match(firmware,/menuSelection = \(menuSelection \+ 1\) % 7/);assert.match(firmware,/kEspNowEnabled = false/);
console.log('supachat_emulator_flow=PASS');
