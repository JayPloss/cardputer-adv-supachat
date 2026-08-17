import assert from 'node:assert/strict';
import test from 'node:test';
import {parseChallenge, resolveRound} from '../src/duel.mjs';

test('Half-Blood Prince attack cycle resolves correctly', () => {
  assert.equal(resolveRound('sectum sempra', 'langlock').winner, 'first');
  assert.equal(resolveRound('sectumsempra', 'levicorpus').winner, 'second');
  assert.equal(resolveRound('levicorpus', 'langlock').winner, 'second');
});

test('Protego blocks without scoring and third consecutive use forfeits', () => {
  assert.deepEqual(resolveRound('protego', 'langlock', 0, 0), {winner:null, reason:'protego', firstProtegoStreak:1, secondProtegoStreak:0});
  assert.equal(resolveRound('protego', 'langlock', 2, 0).winner, 'second');
  assert.equal(resolveRound('protego', 'protego', 2, 2).reason, 'double_protego_forfeit');
});

test('duel command accepts friendly brackets and casing', () => {
  assert.equal(parseChallenge('duel <Albie>'), 'Albie');
  assert.equal(parseChallenge('DUEL Julien'), 'Julien');
  assert.equal(parseChallenge('not a duel'), null);
});
