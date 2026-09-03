import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { DatabaseSync } from 'node:sqlite';
import test from 'node:test';

test('identity merge preserves related data and removes the obsolete identity', () => {
  const directory = mkdtempSync(join(tmpdir(), 'supachat-merge-'));
  const path = join(directory, 'test.sqlite');
  const db = new DatabaseSync(path);
  db.exec(`
    PRAGMA foreign_keys=ON;
    CREATE TABLE users(id TEXT PRIMARY KEY,display_name TEXT,short_name TEXT,kind TEXT);
    CREATE TABLE user_group_members(group_id TEXT,user_id TEXT REFERENCES users(id),PRIMARY KEY(group_id,user_id));
    CREATE TABLE conversation_members(conversation_id TEXT,user_id TEXT REFERENCES users(id),display_name TEXT,color_index INTEGER,PRIMARY KEY(conversation_id,user_id));
    CREATE TABLE messages(id INTEGER PRIMARY KEY,author_id TEXT REFERENCES users(id),client_id TEXT,UNIQUE(author_id,client_id));
    CREATE TABLE receipts(message_id INTEGER,user_id TEXT REFERENCES users(id),state TEXT,updated_at INTEGER,PRIMARY KEY(message_id,user_id));
    CREATE TABLE presence(user_id TEXT PRIMARY KEY REFERENCES users(id),last_seen_at INTEGER,connected INTEGER);
    CREATE TABLE notification_devices(id INTEGER PRIMARY KEY,user_id TEXT REFERENCES users(id),token TEXT UNIQUE);
    CREATE TABLE policy_acceptances(user_id TEXT REFERENCES users(id),version TEXT,accepted_at INTEGER,PRIMARY KEY(user_id,version));
    CREATE TABLE user_blocks(blocker_id TEXT REFERENCES users(id),blocked_user_id TEXT REFERENCES users(id),created_at INTEGER,PRIMARY KEY(blocker_id,blocked_user_id),CHECK(blocker_id<>blocked_user_id));
    CREATE TABLE moderation_reports(id INTEGER PRIMARY KEY,reporter_id TEXT REFERENCES users(id),reported_user_id TEXT REFERENCES users(id),resolved_by TEXT REFERENCES users(id));
    CREATE TABLE account_deletion_requests(id INTEGER PRIMARY KEY,user_id TEXT REFERENCES users(id));
    CREATE TABLE device_preferences(user_id TEXT PRIMARY KEY REFERENCES users(id),language_override TEXT);
    INSERT INTO users VALUES ('old','Nico','Nico','web'),('nico','Nico','Nico','web'),('friend','Friend','Friend','web');
    INSERT INTO user_group_members VALUES ('k-buds','old'),('k-buds','nico');
    INSERT INTO conversation_members VALUES ('k-buds','old',NULL,7),('k-buds','nico',NULL,3);
    INSERT INTO messages VALUES (1,'old','old-message');
    INSERT INTO receipts VALUES (1,'old','read',10);
    INSERT INTO presence VALUES ('old',20,1),('nico',10,0);
    INSERT INTO notification_devices VALUES (1,'old','push-token');
    INSERT INTO policy_acceptances VALUES ('old','v1',15),('nico','v1',5);
    INSERT INTO user_blocks VALUES ('old','friend',1),('friend','old',2);
    INSERT INTO moderation_reports VALUES (1,'old','friend','old');
    INSERT INTO account_deletion_requests VALUES (1,'old');
    INSERT INTO device_preferences VALUES ('old','fr');
  `);
  db.close();

  const script = join(import.meta.dirname, '..', 'deploy', 'merge-identities.mjs');
  const result = spawnSync(process.execPath, ['--experimental-sqlite', script, path, 'old:nico'], { encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
  const merged = new DatabaseSync(path);
  assert.equal(merged.prepare("SELECT COUNT(*) AS count FROM users WHERE id='old'").get().count, 0);
  assert.equal(merged.prepare('SELECT author_id FROM messages WHERE id=1').get().author_id, 'nico');
  const memberships = merged.prepare("SELECT group_id,user_id FROM user_group_members WHERE group_id='k-buds'").all();
  assert.equal(memberships.length, 1);
  assert.equal(memberships[0].group_id, 'k-buds');
  assert.equal(memberships[0].user_id, 'nico');
  assert.equal(merged.prepare("SELECT color_index FROM conversation_members WHERE conversation_id='k-buds' AND user_id='nico'").get().color_index, 3);
  const presence = merged.prepare("SELECT last_seen_at,connected FROM presence WHERE user_id='nico'").get();
  assert.equal(presence.last_seen_at, 20);
  assert.equal(presence.connected, 1);
  assert.equal(merged.prepare("SELECT accepted_at FROM policy_acceptances WHERE user_id='nico' AND version='v1'").get().accepted_at, 15);
  for (const table of ['receipts','notification_devices','user_blocks','moderation_reports','account_deletion_requests','device_preferences']) {
    const columns = merged.prepare(`PRAGMA table_info(${table})`).all().map((column) => column.name).filter((name) => name.includes('user') || name.includes('reporter') || name === 'resolved_by' || name.includes('blocker'));
    for (const column of columns) assert.equal(merged.prepare(`SELECT COUNT(*) AS count FROM ${table} WHERE ${column}='old'`).get().count, 0, `${table}.${column}`);
  }
  merged.close();
  rmSync(directory, {recursive:true,force:true});
});
