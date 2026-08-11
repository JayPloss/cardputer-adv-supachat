import { readFileSync, writeFileSync } from 'node:fs';
import { basename } from 'node:path';

const [input, output] = process.argv.slice(2);
if (!input || !output) throw new Error('usage: node midi-to-header.mjs input.mid output.h');
const bytes = readFileSync(input);
let position = 0;
const readU16 = () => { const value = bytes.readUInt16BE(position); position += 2; return value; };
const readU32 = () => { const value = bytes.readUInt32BE(position); position += 4; return value; };
const readText = (length) => { const value = bytes.subarray(position, position + length).toString('ascii'); position += length; return value; };
const readVar = (end) => {
  let value = 0;
  for (let count = 0; count < 4 && position < end; count++) {
    const byte = bytes[position++]; value = (value << 7) | (byte & 0x7f);
    if (!(byte & 0x80)) return value;
  }
  throw new Error('invalid variable-length MIDI value');
};

if (readText(4) !== 'MThd') throw new Error('not a standard MIDI file');
const headerLength = readU32();
const format = readU16();
const trackCount = readU16();
const ticksPerBeat = readU16();
position = 8 + headerLength;
const notes = [];
const lyrics = [];

for (let track = 0; track < trackCount; track++) {
  if (readText(4) !== 'MTrk') throw new Error(`missing track ${track}`);
  const trackLength = readU32();
  const end = position + trackLength;
  let tick = 0;
  let runningStatus = 0;
  while (position < end) {
    tick += readVar(end);
    let status = bytes[position++];
    if (status < 0x80) { position--; status = runningStatus; } else if (status < 0xf0) runningStatus = status;
    if (status === 0xff) {
      const metaType = bytes[position++];
      const length = readVar(end);
      if (metaType === 0x05) lyrics.push({ tick, text: bytes.subarray(position, position + length).toString('utf8') });
      position += length;
      continue;
    }
    if (status === 0xf0 || status === 0xf7) {
      const length = readVar(end);
      position += length;
      runningStatus = 0;
      continue;
    }
    const command = status & 0xf0;
    const data1 = bytes[position++];
    const data2 = command === 0xc0 || command === 0xd0 ? 0 : bytes[position++];
    if (command === 0x90 && data2 > 0) notes.push({ tick, note: data1, velocity: data2, track, channel: status & 0x0f });
  }
  position = end;
}

notes.sort((a, b) => a.tick - b.tick || a.track - b.track || a.note - b.note);
const channelCounts = new Map();
for (const event of notes) channelCounts.set(event.channel, (channelCounts.get(event.channel) ?? 0) + 1);
console.log(`Note events by MIDI channel: ${[...channelCounts.entries()].map(([channel, count]) => `${channel + 1}=${count}`).join(', ')}`);
for (const channel of [...channelCounts.keys()].sort((a, b) => a - b)) {
  const channelEvents = notes.filter((event) => event.channel === channel);
  const channelNotes = channelEvents.map((event) => event.note);
  console.log(`  ch ${channel + 1}: ticks ${channelEvents[0].tick}-${channelEvents.at(-1).tick}, MIDI ${Math.min(...channelNotes)}-${Math.max(...channelNotes)}, first ${channelNotes.slice(0, 16).join(' ')}`);
}
console.log(`Lyric syllables: ${lyrics.length}; first ${lyrics.slice(0, 12).map((item) => `${item.tick}:${item.text}`).join(' | ')}`);
for (const channel of [...channelCounts.keys()].sort((a, b) => a - b)) {
  const channelEvents = notes.filter((event) => event.channel === channel);
  const exact = lyrics.filter((lyric) => channelEvents.some((event) => event.tick === lyric.tick)).length;
  const near = lyrics.filter((lyric) => channelEvents.some((event) => Math.abs(event.tick - lyric.tick) <= 60)).length;
  console.log(`  lyric alignment ch ${channel + 1}: exact=${exact}, within60=${near}`);
}
// This particular MIDI is a multichannel arrangement. MIDI channel 4 is the
// vocal melody: its note onsets align with every embedded lyric timestamp.
// Transpose that whole voice by one octave as a unit so intervals and key stay
// intact while remaining clear on the tiny speaker.
const leadChannel = 3; // MIDI channel 4, zero based.
const leadNotes = notes.filter((event) => event.channel === leadChannel);
if (!leadNotes.length) throw new Error('vocal melody channel 4 has no notes');
const playableNotes = leadNotes.map(({ note }) => note + 12);
const frequencies = playableNotes.map((note) => Math.round(440 * 2 ** ((note - 69) / 12)));
const lines = [];
for (let index = 0; index < frequencies.length; index += 16) lines.push(`    ${frequencies.slice(index, index + 16).join(', ')}`);
writeFileSync(output, `#pragma once\n#include <stdint.h>\n\n// Generated from ${basename(input)}; MIDI format ${format}, ${trackCount} tracks, ${ticksPerBeat} ticks/beat.\nconstexpr uint16_t kKeypressSongFrequencies[] = {\n${lines.join(',\n')}\n};\nconstexpr size_t kKeypressSongLength = sizeof(kKeypressSongFrequencies) / sizeof(kKeypressSongFrequencies[0]);\n`);
console.log(`Generated ${output} with ${playableNotes.length} lead-melody note events.`);
