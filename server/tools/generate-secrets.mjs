import { createHash, randomBytes, scryptSync } from 'node:crypto';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';

const outputDirectory = resolve(process.argv[2] || '');
if (!process.argv[2]) throw new Error('usage: node generate-secrets.mjs <private-output-directory>');
mkdirSync(outputDirectory, { recursive: true });

const papaPassword = randomBytes(15).toString('base64url');
const deviceToken = randomBytes(32).toString('hex');
const sessionSecret = randomBytes(48).toString('hex');
const salt = randomBytes(16);
const passwordHash = `scrypt$${salt.toString('hex')}$${scryptSync(papaPassword, salt, 32).toString('hex')}`;
const deviceTokenHash = createHash('sha256').update(deviceToken).digest('hex');
const createdAt = new Date().toISOString();

writeFileSync(resolve(outputDirectory, 'supachat-credentials.json'), JSON.stringify({
  created_at: createdAt,
  papa: { username: 'papa', password: papaPassword },
  albie: { device_id: 'albie', device_token: deviceToken },
}, null, 2), { mode: 0o600 });

writeFileSync(resolve(outputDirectory, 'supachat.env'), [
  'SUPACHAT_HOST=127.0.0.1',
  'SUPACHAT_PORT=8094',
  'SUPACHAT_DATA_DIR=/var/lib/supachat',
  'SUPACHAT_PUBLIC_BASE=/supachat',
  `SUPACHAT_SESSION_SECRET=${sessionSecret}`,
  `SUPACHAT_PAPA_PASSWORD_HASH=${passwordHash}`,
  `SUPACHAT_ALBIE_DEVICE_TOKEN_HASH=${deviceTokenHash}`,
  '',
].join('\n'), { mode: 0o600 });

console.log(`Generated private SupaChat credentials in ${outputDirectory}`);

