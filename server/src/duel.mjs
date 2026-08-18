export const spells = ['protego', 'sectumsempra', 'levicorpus', 'langlock'];

const beats = {sectumsempra: 'langlock', levicorpus: 'sectumsempra', langlock: 'levicorpus'};

export function normalizeSpell(value) {
  const normalized = String(value || '').toLowerCase().replace(/[!\s_-]/g, '');
  return spells.includes(normalized) ? normalized : null;
}

export function resolveRound(firstSpell, secondSpell, firstProtegoStreak = 0, secondProtegoStreak = 0) {
  const first = normalizeSpell(firstSpell); const second = normalizeSpell(secondSpell);
  if (!first || !second) throw new Error('invalid_spell');
  const nextFirstStreak = first === 'protego' ? firstProtegoStreak + 1 : 0;
  const nextSecondStreak = second === 'protego' ? secondProtegoStreak + 1 : 0;
  const firstForfeit = nextFirstStreak >= 3; const secondForfeit = nextSecondStreak >= 3;
  if (firstForfeit || secondForfeit) return {
    winner: firstForfeit === secondForfeit ? null : firstForfeit ? 'second' : 'first',
    reason: firstForfeit === secondForfeit ? 'double_protego_forfeit' : 'protego_forfeit',
    firstProtegoStreak: firstForfeit ? 0 : nextFirstStreak,
    secondProtegoStreak: secondForfeit ? 0 : nextSecondStreak,
  };
  if (first === 'protego' || second === 'protego' || first === second) return {
    winner: null, reason: first === second ? 'same_spell' : 'protego',
    firstProtegoStreak: nextFirstStreak, secondProtegoStreak: nextSecondStreak,
  };
  return {winner: beats[first] === second ? 'first' : 'second', reason: 'spell', firstProtegoStreak: nextFirstStreak, secondProtegoStreak: nextSecondStreak};
}

export function parseChallenge(text) {
  const match = String(text || '').trim().match(/^\\?duel\s+<?([^<>]+?)>?[!.]?$/i);
  return match ? match[1].trim() : null;
}
