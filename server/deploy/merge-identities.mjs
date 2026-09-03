import { backup, DatabaseSync } from 'node:sqlite';

const [databasePath, ...pairs] = process.argv.slice(2);
if (!databasePath || !pairs.length || pairs.some((pair) => !/^[a-z0-9-]+:[a-z0-9-]+$/.test(pair))) {
  throw new Error('Usage: node --experimental-sqlite merge-identities.mjs DATABASE OLD:TARGET [OLD:TARGET ...]');
}

const backupPath = `${databasePath}.before-identity-merge-${new Date().toISOString().replace(/[:.]/g, '-')}`;
const db = new DatabaseSync(databasePath);
await backup(db, backupPath);
db.exec('PRAGMA foreign_keys=ON; BEGIN IMMEDIATE');

try {
  for (const pair of pairs) {
    const [oldId, targetId] = pair.split(':');
    if (oldId === targetId) throw new Error(`Refusing self-merge for ${oldId}`);
    const oldUser = db.prepare('SELECT id, display_name FROM users WHERE id=?').get(oldId);
    const targetUser = db.prepare('SELECT id, display_name FROM users WHERE id=?').get(targetId);
    if (!oldUser || !targetUser) throw new Error(`Both identities must exist: ${oldId} -> ${targetId}`);
    const collision = db.prepare(`SELECT 1 FROM messages old_message JOIN messages target_message
      ON target_message.author_id=? AND target_message.client_id=old_message.client_id
      WHERE old_message.author_id=? LIMIT 1`).get(targetId, oldId);
    if (collision) throw new Error(`Message client-id collision: ${oldId} -> ${targetId}`);

    db.prepare(`INSERT INTO user_group_members(group_id,user_id)
      SELECT group_id,? FROM user_group_members WHERE user_id=? ON CONFLICT DO NOTHING`).run(targetId, oldId);
    db.prepare('DELETE FROM user_group_members WHERE user_id=?').run(oldId);
    db.prepare(`INSERT INTO conversation_members(conversation_id,user_id,display_name,color_index)
      SELECT conversation_id,?,display_name,color_index FROM conversation_members WHERE user_id=?
      ON CONFLICT(conversation_id,user_id) DO UPDATE SET
        display_name=COALESCE(conversation_members.display_name,excluded.display_name),
        color_index=COALESCE(conversation_members.color_index,excluded.color_index)`).run(targetId, oldId);
    db.prepare('DELETE FROM conversation_members WHERE user_id=?').run(oldId);
    db.prepare('UPDATE messages SET author_id=? WHERE author_id=?').run(targetId, oldId);
    db.prepare(`INSERT INTO receipts(message_id,user_id,state,updated_at)
      SELECT message_id,?,state,updated_at FROM receipts WHERE user_id=?
      ON CONFLICT(message_id,user_id) DO UPDATE SET
        state=CASE WHEN excluded.state='read' OR receipts.state='read' THEN 'read'
          WHEN excluded.state='delivered' OR receipts.state='delivered' THEN 'delivered' ELSE 'server' END,
        updated_at=MAX(receipts.updated_at,excluded.updated_at)`).run(targetId, oldId);
    db.prepare('DELETE FROM receipts WHERE user_id=?').run(oldId);
    db.prepare(`INSERT INTO presence(user_id,last_seen_at,connected)
      SELECT ?,last_seen_at,connected FROM presence WHERE user_id=?
      ON CONFLICT(user_id) DO UPDATE SET last_seen_at=MAX(presence.last_seen_at,excluded.last_seen_at),connected=MAX(presence.connected,excluded.connected)`).run(targetId, oldId);
    db.prepare('DELETE FROM presence WHERE user_id=?').run(oldId);
    db.prepare('UPDATE notification_devices SET user_id=? WHERE user_id=?').run(targetId, oldId);
    db.prepare(`INSERT INTO policy_acceptances(user_id,version,accepted_at)
      SELECT ?,version,accepted_at FROM policy_acceptances WHERE user_id=?
      ON CONFLICT(user_id,version) DO UPDATE SET accepted_at=MAX(policy_acceptances.accepted_at,excluded.accepted_at)`).run(targetId, oldId);
    db.prepare('DELETE FROM policy_acceptances WHERE user_id=?').run(oldId);
    db.prepare(`INSERT OR IGNORE INTO user_blocks(blocker_id,blocked_user_id,created_at)
      SELECT CASE WHEN blocker_id=? THEN ? ELSE blocker_id END,
        CASE WHEN blocked_user_id=? THEN ? ELSE blocked_user_id END,created_at
      FROM user_blocks WHERE (blocker_id=? OR blocked_user_id=?)
        AND (CASE WHEN blocker_id=? THEN ? ELSE blocker_id END)<>(CASE WHEN blocked_user_id=? THEN ? ELSE blocked_user_id END)`)
      .run(oldId,targetId,oldId,targetId,oldId,oldId,oldId,targetId,oldId,targetId);
    db.prepare('DELETE FROM user_blocks WHERE blocker_id=? OR blocked_user_id=?').run(oldId, oldId);
    db.prepare('UPDATE moderation_reports SET reporter_id=? WHERE reporter_id=?').run(targetId, oldId);
    db.prepare('UPDATE moderation_reports SET reported_user_id=? WHERE reported_user_id=?').run(targetId, oldId);
    db.prepare('UPDATE moderation_reports SET resolved_by=? WHERE resolved_by=?').run(targetId, oldId);
    db.prepare('UPDATE account_deletion_requests SET user_id=? WHERE user_id=?').run(targetId, oldId);
    db.prepare(`INSERT INTO device_preferences(user_id,language_override)
      SELECT ?,language_override FROM device_preferences WHERE user_id=?
      ON CONFLICT(user_id) DO UPDATE SET language_override=COALESCE(device_preferences.language_override,excluded.language_override)`).run(targetId, oldId);
    db.prepare('DELETE FROM device_preferences WHERE user_id=?').run(oldId);
    db.prepare('DELETE FROM users WHERE id=?').run(oldId);
    console.log(`${oldId} -> ${targetId} (${oldUser.display_name} -> ${targetUser.display_name})`);
  }
  db.exec('COMMIT');
  console.log(`backup=${backupPath}`);
} catch (error) {
  db.exec('ROLLBACK');
  throw error;
}
