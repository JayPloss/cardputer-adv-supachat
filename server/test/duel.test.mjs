import assert from 'node:assert/strict';
import test from 'node:test';
import {normalizeSpell, parseChallenge, resolveRound} from '../src/duel.mjs';

test('duel spell cycle and aliases resolve', () => {
  assert.equal(normalizeSpell('Sectum Sempra!'), 'sectumsempra');
  assert.equal(resolveRound('sectumsempra', 'langlock').winner, 'first');
  assert.equal(resolveRound('sectumsempra', 'levicorpus').winner, 'second');
  assert.equal(resolveRound('levicorpus', 'langlock').winner, 'second');
});

test('Protego blocks and its third consecutive use forfeits', () => {
  assert.deepEqual(resolveRound('protego', 'langlock', 0, 0), {winner:null, reason:'protego', firstProtegoStreak:1, secondProtegoStreak:0});
  assert.equal(resolveRound('protego', 'langlock', 2, 0).winner, 'second');
  assert.equal(resolveRound('protego', 'protego', 2, 2).reason, 'double_protego_forfeit');
});

test('duel command accepts friendly brackets but not extra prose', () => {
  assert.equal(parseChallenge('duel <Albie>'), 'Albie');
  assert.equal(parseChallenge('DUEL Julien!'), 'Julien');
  assert.equal(parseChallenge('please duel Albie'), null);
});
