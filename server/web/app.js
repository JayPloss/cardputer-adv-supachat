const messagesEl = document.querySelector('#messages');
const presenceEl = document.querySelector('#presence');
const composer = document.querySelector('#composer');
const input = document.querySelector('#message');
const count = document.querySelector('#count');
const sendState = document.querySelector('#send-state');
const logout = document.querySelector('#logout');
const voiceClipButton = document.querySelector('#voice-clip');
const pttButton = document.querySelector('#ptt');
const walkieState = document.querySelector('#walkie-state');
const soundToggle = document.querySelector('#sound-toggle');
const safetyOpen = document.querySelector('#safety-open');
const policyZone = document.querySelector('#policy-zone');
const policyAccept = document.querySelector('#policy-accept');
const policyClose = document.querySelector('#policy-close');
const adminOpen = document.querySelector('#admin-open');
const adminZone = document.querySelector('#admin-zone');
const adminClose = document.querySelector('#admin-close');
const inviteForm = document.querySelector('#invite-form');
const inviteState = document.querySelector('#invite-state');
const inviteResult = document.querySelector('#invite-result');
const inviteLink = document.querySelector('#invite-link');
const inviteQr = document.querySelector('#invite-qr');
const inviteShare = document.querySelector('#invite-share');
const inviteGenerate = document.querySelector('#invite-generate');
const roomSelect = document.querySelector('#room-select');
const languageSelect = document.querySelector('#language-select');
const welcomeZone = document.querySelector('#welcome-zone');
const welcomeClose = document.querySelector('#welcome-close');
const composerLabel = document.querySelector('#composer-label');
const voiceRoomNote = document.querySelector('#voice-room-note');
const welcomeTitle = document.querySelector('#welcome-title');
const welcomeRoomCopy = document.querySelector('#welcome-room-copy');
const newGroupForm = document.querySelector('#new-group-form');
const groupState = document.querySelector('#group-state');
const manageRoom = document.querySelector('#manage-room');
const groupMembers = document.querySelector('#group-members');
const existingUser = document.querySelector('#existing-user');
const addMember = document.querySelector('#add-member');
const newUserGroupForm = document.querySelector('#new-user-group-form');
const userGroupState = document.querySelector('#user-group-state');
const manageUserGroup = document.querySelector('#manage-user-group');
const userGroupMembers = document.querySelector('#user-group-members');
const existingGroupUser = document.querySelector('#existing-group-user');
const addGroupMember = document.querySelector('#add-group-member');
const complianceRefresh = document.querySelector('#compliance-refresh');
const complianceState = document.querySelector('#compliance-state');
const complianceQueue = document.querySelector('#compliance-queue');
let adminRooms = []; let adminUserGroups = []; let adminUsers = [];
const translations = {
  en: {
    tagline:'PRIVATE ROOMS / ALWAYS WAITING', room:'Room', language:'Language', admin:'Admin', safety:'Safety', logout:'Log out',
    soundOn:'Sound: on', soundOff:'Sound: off', write:'Write something…', send:'Send', ready:'Ready', connecting:'Connecting…',
    liveVoice:'LIVE VOICE', holdTalk:'Hold to talk', orVoice:'or leave a message', recordClip:'Record voice clip',
    adminZone:'ADMIN ZONE', rooms:'Rooms', adminIntro:'Manage chat rooms and keep user groups separate.',
    newRoom:'New room', manageRoom:'Manage room', newUserGroup:'New user group', manageUserGroup:'Manage user group', userGroup:'User group', userGroupHelp:'User groups organize people and do not grant room access.', addExisting:'Add existing user', add:'Add', none:'None', create:'Create', inviteUser:'Invite new user', inviteExpiry:'One-use link · expires in seven days.',
    name:'Name', username:'Username', email:'Email', optional:'(optional)', inviteReady:'Invite ready', shareInvite:'Share invite',
    generateInvite:'Generate one-time link', welcomeEyebrow:'WELCOME TO SUPACHAT', startChatting:'Start chatting',
    empty:'No messages yet.\nSay the first thing.', messageRoom:'Message {room}', voiceRoom:'One person talks at a time. Voice clips stay in {room} history.',
    welcomeRoom:'You’re in {room}.', welcomeCopy:'This invitation added you to the {room} conversation.', online:'online', offline:'offline', seen:'seen {time}',
    playing:'Playing…', playVoice:'▶ Play voice', uploading:'Uploading…', uploadFailed:'Upload failed — retry', stopSend:'Stop + send (5s max)', micUnavailable:'Microphone unavailable', walkieReady:'Ready — hold to talk', transmitting:'Transmitting as {name}', talking:'{name} is talking', hasChannel:'{name} has the channel', reconnecting:'Reconnecting…', sending:'Sending…', sent:'Sent', sendFailed:'Not sent — try again', saved:'Saved', readBy:'Read by {names}', deliveredTo:'Delivered to {names}'
  },
  fr: {
    tagline:'SALONS PRIVÉS / TOUJOURS PRÊT', room:'Salon', language:'Langue', admin:'Admin', safety:'Sécurité', logout:'Déconnexion',
    soundOn:'Son : activé', soundOff:'Son : désactivé', write:'Écrivez un message…', send:'Envoyer', ready:'Prêt', connecting:'Connexion…',
    liveVoice:'VOIX EN DIRECT', holdTalk:'Maintenir pour parler', orVoice:'ou laisser un message', recordClip:'Enregistrer un message vocal',
    adminZone:'ZONE ADMIN', rooms:'Salons', adminIntro:'Gérez les salons tout en gardant les groupes d’utilisateurs séparés.',
    newRoom:'Nouveau salon', manageRoom:'Gérer le salon', newUserGroup:'Nouveau groupe d’utilisateurs', manageUserGroup:'Gérer le groupe d’utilisateurs', userGroup:'Groupe d’utilisateurs', userGroupHelp:'Les groupes organisent les personnes sans donner accès aux salons.', addExisting:'Ajouter une personne', add:'Ajouter', none:'Aucun', create:'Créer', inviteUser:'Inviter une personne', inviteExpiry:'Lien à usage unique · expire dans sept jours.',
    name:'Nom', username:'Nom d’utilisateur', email:'Courriel', optional:'(facultatif)', inviteReady:'Invitation prête', shareInvite:'Partager l’invitation',
    generateInvite:'Créer un lien unique', welcomeEyebrow:'BIENVENUE SUR SUPACHAT', startChatting:'Commencer à clavarder',
    empty:'Aucun message.\nLancez la conversation.', messageRoom:'Message à {room}', voiceRoom:'Une personne parle à la fois. Les messages vocaux restent dans l’historique de {room}.',
    welcomeRoom:'Vous êtes dans {room}.', welcomeCopy:'Cette invitation vous a ajouté à la conversation {room}.', online:'en ligne', offline:'hors ligne', seen:'vu à {time}',
    playing:'Lecture…', playVoice:'▶ Écouter', uploading:'Téléversement…', uploadFailed:'Échec du téléversement — réessayer', stopSend:'Arrêter et envoyer (5 s max)', micUnavailable:'Microphone indisponible', walkieReady:'Prêt — maintenir pour parler', transmitting:'Transmission comme {name}', talking:'{name} parle', hasChannel:'{name} a le canal', reconnecting:'Reconnexion…', sending:'Envoi…', sent:'Envoyé', sendFailed:'Non envoyé — réessayer', saved:'Enregistré', readBy:'Lu par {names}', deliveredTo:'Livré à {names}'
  }
};
let locale = localStorage.getItem('supachat-language') || (navigator.language?.toLowerCase().startsWith('fr') ? 'fr' : 'en');
if (!translations[locale]) locale = 'en';
const t = (key, values = {}) => Object.entries(values).reduce((text, [name, value]) => text.replaceAll(`{${name}}`, value), translations[locale][key] || translations.en[key] || key);
function applyLocale() {
  document.documentElement.lang = locale; languageSelect.value = locale;
  document.querySelectorAll('[data-i18n]').forEach((node) => { node.textContent = t(node.dataset.i18n); });
  document.querySelectorAll('[data-i18n-placeholder]').forEach((node) => { node.placeholder = t(node.dataset.i18nPlaceholder); });
  updateSoundToggle();
}
const welcomeRequested = new URLSearchParams(location.search).get('welcome') === '1';
let messages = [];
let currentUser = null;
let rooms = [];
let currentRoom = new URLSearchParams(location.search).get('room') || localStorage.getItem('supachat-room') || '';
let clipRoom = '';
let policyVersion = '';
let policyRequired = false;

const api = async (path, options = {}) => {
  const response = await fetch(path, { ...options, headers: { 'content-type': 'application/json', ...(options.headers || {}) } });
  if (response.status === 401) { location.href = 'login'; throw new Error('unauthorized'); }
  if (!response.ok) throw new Error((await response.json()).error || 'request_failed');
  return response.json();
};
const escapeHtml = (value) => value.replace(/[&<>'"]/g, (char) => ({'&':'&amp;','<':'&lt;','>':'&gt;',"'":'&#39;','"':'&quot;'}[char]));
const time = (value) => new Intl.DateTimeFormat([], {hour:'numeric',minute:'2-digit'}).format(new Date(value));
const identityClass = (id) => ['albie', 'juju', 'papa', 'theo', 'josee', 'emmanuelle', 'andrew', 'naomie'].includes(id) ? `author-${id}` : 'author-other';
const receiptLabel = (message) => {
  const names = { albie: 'Albie', juju: 'Juju' };
  const deviceReceipts = (message.receipts || []).filter((item) => names[item.user_id]);
  const read = deviceReceipts.filter((item) => item.state === 'read').map((item) => names[item.user_id]);
  if (read.length) return t('readBy', {names:read.join(', ')});
  const delivered = deviceReceipts.filter((item) => item.state === 'delivered').map((item) => names[item.user_id]);
  return delivered.length ? t('deliveredTo', {names:delivered.join(', ')}) : t('saved');
};
function renderMessages(scroll = false) {
  messagesEl.innerHTML = messages.length ? messages.map((message) => `
    <article class="message ${message.author_id === currentUser?.id ? 'mine' : ''} ${identityClass(message.author_id)}" data-id="${message.id}">
      <div class="bubble"><strong class="message-sender">${escapeHtml(message.author_name)}</strong><span class="message-separator">: </span><span class="message-body">${message.type === 'voice' ? `<button class="voice-play" data-voice-id="${message.id}">${escapeHtml(t('playVoice'))}</button><span class="voice-wave">${Math.round((message.voice?.duration_ms || 0) / 100) / 10}s</span>` : escapeHtml(message.body)}</span></div>
      <div class="message-meta"><span>${time(message.created_at)}</span>${message.author_id === currentUser?.id ? `<span class="receipt">${receiptLabel(message)}</span>` : ''}</div>
    </article>`).join('') : `<p class="empty">${escapeHtml(t('empty')).replace('\n','<br>')}</p>`;
  if (scroll) messagesEl.scrollTop = messagesEl.scrollHeight;
}

let audioContext;
let micStream;
let captureSource;
let captureProcessor;
let nextPlaybackAt = 0;
let notificationSoundEnabled = localStorage.getItem('supachat-sound') !== 'off';
const ensureAudio = async () => {
  audioContext ||= new AudioContext();
  if (audioContext.state === 'suspended') await audioContext.resume();
  return audioContext;
};
const updateSoundToggle = () => {
  soundToggle.textContent = t(notificationSoundEnabled ? 'soundOn' : 'soundOff');
  soundToggle.setAttribute('aria-pressed', String(notificationSoundEnabled));
};
async function playNotificationSound() {
  if (!notificationSoundEnabled) return;
  try {
    const context = await ensureAudio();
    const start = context.currentTime + .02;
    [[587, 0, .09], [784, .11, .08], [659, .22, .16]].forEach(([frequency, offset, duration]) => {
      const oscillator = context.createOscillator(); const gain = context.createGain();
      oscillator.type = 'triangle'; oscillator.frequency.value = frequency;
      gain.gain.setValueAtTime(.0001, start + offset);
      gain.gain.exponentialRampToValueAtTime(.12, start + offset + .012);
      gain.gain.exponentialRampToValueAtTime(.0001, start + offset + duration);
      oscillator.connect(gain).connect(context.destination);
      oscillator.start(start + offset); oscillator.stop(start + offset + duration + .02);
    });
  } catch {}
}
updateSoundToggle();
applyLocale();
languageSelect.addEventListener('change', async () => {
  locale = translations[languageSelect.value] ? languageSelect.value : 'en';
  localStorage.setItem('supachat-language', locale); applyLocale(); await refresh();
});
soundToggle.addEventListener('click', async () => {
  notificationSoundEnabled = !notificationSoundEnabled;
  localStorage.setItem('supachat-sound', notificationSoundEnabled ? 'on' : 'off');
  updateSoundToggle();
  if (notificationSoundEnabled) await playNotificationSound();
});
const unlockAudio = () => { if (notificationSoundEnabled) ensureAudio().catch(() => {}); };
window.addEventListener('pointerdown', unlockAudio, { once:true });
window.addEventListener('keydown', unlockAudio, { once:true });
const pcmChunk = (samples, inputRate) => {
  const ratio = inputRate / 8000;
  const length = Math.floor(samples.length / ratio);
  const bytes = new Uint8Array(length * 2);
  const view = new DataView(bytes.buffer);
  for (let output = 0; output < length; output++) {
    const start = Math.floor(output * ratio); const end = Math.max(start + 1, Math.floor((output + 1) * ratio));
    let sum = 0; for (let inputIndex = start; inputIndex < end && inputIndex < samples.length; inputIndex++) sum += samples[inputIndex];
    const value = Math.max(-1, Math.min(1, sum / (end - start)));
    view.setInt16(output * 2, value < 0 ? value * 32768 : value * 32767, true);
  }
  return bytes;
};
async function startCapture(onChunk) {
  if (captureProcessor) return false;
  const context = await ensureAudio();
  micStream ||= await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation:true, noiseSuppression:true, channelCount:1 }, video:false });
  captureSource = context.createMediaStreamSource(micStream);
  captureProcessor = context.createScriptProcessor(2048, 1, 1);
  captureProcessor.onaudioprocess = (event) => onChunk(pcmChunk(event.inputBuffer.getChannelData(0), context.sampleRate));
  captureSource.connect(captureProcessor); captureProcessor.connect(context.destination);
  return true;
}
function stopCapture() {
  captureProcessor?.disconnect(); captureSource?.disconnect();
  captureProcessor = null; captureSource = null;
}
async function playPcm(bytes) {
  const context = await ensureAudio();
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const buffer = context.createBuffer(1, bytes.byteLength / 2, 8000);
  const channel = buffer.getChannelData(0);
  for (let index = 0; index < channel.length; index++) channel[index] = view.getInt16(index * 2, true) / 32768;
  const source = context.createBufferSource(); source.buffer = buffer; source.connect(context.destination);
  nextPlaybackAt = Math.max(context.currentTime + .04, nextPlaybackAt);
  source.start(nextPlaybackAt); nextPlaybackAt += buffer.duration;
}
messagesEl.addEventListener('click', async (event) => {
  const button = event.target.closest('[data-voice-id]'); if (!button) return;
  button.disabled = true; button.textContent = t('playing');
  try {
    const response = await fetch(`api/voice/${button.dataset.voiceId}/audio`);
    if (!response.ok) throw new Error('play_failed');
    await playPcm(new Uint8Array(await response.arrayBuffer()));
  } finally { button.disabled = false; button.textContent = t('playVoice'); }
});

let clipChunks = [];
let clipTimer;
voiceClipButton.addEventListener('click', async () => {
  if (captureProcessor) {
    clearTimeout(clipTimer); stopCapture();
    const size = clipChunks.reduce((total, chunk) => total + chunk.length, 0);
    const pcm = new Uint8Array(size); let offset = 0;
    for (const chunk of clipChunks) { pcm.set(chunk, offset); offset += chunk.length; }
    voiceClipButton.textContent = t('uploading'); voiceClipButton.disabled = true;
    try {
      const response = await fetch('api/voice', { method:'POST', headers:{'content-type':'application/octet-stream','x-client-id':crypto.randomUUID(),'x-sample-rate':'8000','x-room-id':clipRoom}, body:pcm });
      if (!response.ok) throw new Error('upload_failed');
      voiceClipButton.textContent = t('recordClip');
    } catch { voiceClipButton.textContent = t('uploadFailed'); }
    finally { voiceClipButton.disabled = false; clipChunks = []; }
    return;
  }
  clipChunks = [];
  try {
    clipRoom = currentRoom;
    await startCapture((chunk) => clipChunks.push(chunk));
    voiceClipButton.textContent = t('stopSend');
    clipTimer = setTimeout(() => voiceClipButton.click(), 5000);
  } catch { voiceClipButton.textContent = t('micUnavailable'); }
});

let walkieSocket;
let pttGranted = false;
let pttHeld = false;
function connectWalkie() {
  const url = new URL('walkie', location.href); url.protocol = location.protocol === 'https:' ? 'wss:' : 'ws:'; url.searchParams.set('room', currentRoom);
  walkieSocket = new WebSocket(url); walkieSocket.binaryType = 'arraybuffer';
  walkieSocket.onopen = () => walkieState.textContent = t('walkieReady');
  walkieSocket.onmessage = async (event) => {
    if (event.data instanceof ArrayBuffer) { await playPcm(new Uint8Array(event.data)); return; }
    const message = JSON.parse(event.data);
    if (message.type === 'ptt_start') {
      pttGranted = message.user === currentUser?.id;
      walkieState.textContent = pttGranted ? t('transmitting',{name:currentUser?.display_name || 'you'}) : t('talking',{name:message.user});
      pttButton.classList.toggle('transmitting', pttGranted);
    } else if (message.type === 'ptt_stop') {
      pttGranted = false; walkieState.textContent = t('walkieReady'); pttButton.classList.remove('transmitting');
    } else if (message.type === 'busy') {
      pttGranted = false; pttHeld = false; stopCapture(); pttButton.classList.remove('transmitting');
      walkieState.textContent = t('hasChannel',{name:message.speaker});
    }
  };
  walkieSocket.onclose = () => { walkieState.textContent = t('reconnecting'); setTimeout(connectWalkie, 1500); };
}
async function pttStart(event) {
  event.preventDefault(); if (walkieSocket?.readyState !== WebSocket.OPEN || captureProcessor) return;
  pttHeld = true; pttButton.setPointerCapture?.(event.pointerId);
  walkieSocket.send(JSON.stringify({type:'ptt_start'}));
  try {
    await startCapture((chunk) => { if (pttHeld && pttGranted && walkieSocket.readyState === WebSocket.OPEN) walkieSocket.send(chunk); });
    if (!pttHeld) stopCapture();
  } catch {
    pttHeld = false; walkieState.textContent = t('micUnavailable');
    if (walkieSocket?.readyState === WebSocket.OPEN) walkieSocket.send(JSON.stringify({type:'ptt_stop'}));
  }
}
function pttStop(event) {
  event?.preventDefault(); pttHeld = false;
  stopCapture(); pttGranted = false; pttButton.classList.remove('transmitting');
  if (walkieSocket?.readyState === WebSocket.OPEN) walkieSocket.send(JSON.stringify({type:'ptt_stop'}));
}
pttButton.addEventListener('pointerdown', pttStart);
for (const name of ['pointerup','pointercancel']) pttButton.addEventListener(name, pttStop);
window.addEventListener('blur', pttStop);
connectWalkie();
async function refresh() {
  const session = await api('api/session');
  currentUser = session.user; rooms = session.rooms || [];
  policyVersion = session.policy?.version || '';
  policyRequired = !session.policy?.accepted_at;
  policyClose.hidden = policyRequired;
  if (policyRequired && !policyZone.open) policyZone.showModal();
  if (!rooms.some((room) => room.id === currentRoom)) currentRoom = rooms[0]?.id || '';
  roomSelect.innerHTML = rooms.map((room) => `<option value="${escapeHtml(room.id)}">${escapeHtml(room.name)}</option>`).join('');
  roomSelect.value = currentRoom;
  const activeRoomName = rooms.find((room) => room.id === currentRoom)?.name || 'this room';
  document.title = `SupaChat — ${activeRoomName}`;
  composerLabel.textContent = t('messageRoom', {room:activeRoomName});
  voiceRoomNote.textContent = t('voiceRoom', {room:activeRoomName});
  welcomeTitle.textContent = t('welcomeRoom', {room:activeRoomName});
  welcomeRoomCopy.textContent = t('welcomeCopy', {room:activeRoomName});
  const requestedRoom = currentRoom;
  const [{ messages: next }, { presence }] = await Promise.all([api(`api/messages?room=${encodeURIComponent(requestedRoom)}&limit=100`), api(`api/presence?room=${encodeURIComponent(requestedRoom)}`)]);
  if (currentRoom !== requestedRoom) return;
  adminOpen.hidden = currentUser?.role !== 'admin';
  messages = next;
  renderMessages(true);
  renderPresence(presence);
  if (welcomeRequested && !policyRequired) {
    const welcomeKey = `supachat-welcomed:${currentUser.id}`;
    if (!localStorage.getItem(welcomeKey)) welcomeZone.showModal();
    else clearWelcomeMarker();
  }
}
safetyOpen.addEventListener('click', () => { policyRequired = false; policyClose.hidden = false; if (!policyZone.open) policyZone.showModal(); });
policyClose.addEventListener('click', () => { if (!policyRequired) policyZone.close(); });
policyZone.addEventListener('cancel', (event) => { if (policyRequired) event.preventDefault(); });
policyAccept.addEventListener('click', async () => {
  policyAccept.disabled = true;
  try {
    await api('api/policy/accept', {method:'POST',body:JSON.stringify({version:policyVersion})});
    policyRequired = false; policyClose.hidden = false; policyZone.close(); await refresh();
  } catch { policyAccept.textContent = 'Could not save — try again'; }
  finally { policyAccept.disabled = false; }
});
roomSelect.addEventListener('change', async () => {
  currentRoom = roomSelect.value;
  localStorage.setItem('supachat-room', currentRoom);
  const url = new URL(location.href); url.searchParams.set('room', currentRoom); history.replaceState(null, '', `${url.pathname}${url.search}${url.hash}`);
  messages = []; renderMessages(true); walkieSocket?.close(); await refresh();
});
function clearWelcomeMarker() {
  const url = new URL(location.href);
  url.searchParams.delete('welcome');
  history.replaceState(null, '', `${url.pathname}${url.search}${url.hash}`);
}
function dismissWelcome() {
  if (currentUser) localStorage.setItem(`supachat-welcomed:${currentUser.id}`, '1');
  welcomeZone.close();
  clearWelcomeMarker();
  input.focus();
}
welcomeClose.addEventListener('click', dismissWelcome);
welcomeZone.addEventListener('cancel', (event) => { event.preventDefault(); dismissWelcome(); });
const memberRows = (members, attribute, protectSelf = false) => members?.length ? members.map((member) => `<div class="group-member"><span>${escapeHtml(member.display_name)} <small>${escapeHtml(member.kind)}</small></span><button type="button" ${attribute}="${escapeHtml(member.id)}" ${protectSelf && member.id === currentUser.id ? 'disabled title="You cannot remove yourself"' : ''}>Remove</button></div>`).join('') : '<p class="empty-inline">No members.</p>';
async function loadAdminData(selectedRoom = manageRoom.value, selectedUserGroup = manageUserGroup.value) {
  const [roomData, userGroupData] = await Promise.all([api('api/admin/rooms'), api('api/admin/user-groups')]);
  adminRooms = roomData.rooms; adminUserGroups = userGroupData.groups; adminUsers = roomData.users;
  manageRoom.innerHTML = adminRooms.map((room) => `<option value="${escapeHtml(room.id)}">${escapeHtml(room.name)} (${room.member_count})</option>`).join('');
  manageRoom.value = adminRooms.some((room) => room.id === selectedRoom) ? selectedRoom : adminRooms[0]?.id || '';
  const room = adminRooms.find((item) => item.id === manageRoom.value);
  groupMembers.innerHTML = memberRows(room?.members, 'data-remove-user', true);
  const roomMemberIds = new Set(room?.members.map((member) => member.id));
  existingUser.innerHTML = adminUsers.filter((item) => !roomMemberIds.has(item.id)).map((item) => `<option value="${escapeHtml(item.id)}">${escapeHtml(item.display_name)}</option>`).join('');
  addMember.disabled = !existingUser.value;
  manageUserGroup.innerHTML = adminUserGroups.map((group) => `<option value="${escapeHtml(group.id)}">${escapeHtml(group.name)} (${group.member_count})</option>`).join('');
  manageUserGroup.value = adminUserGroups.some((group) => group.id === selectedUserGroup) ? selectedUserGroup : adminUserGroups[0]?.id || '';
  const userGroup = adminUserGroups.find((item) => item.id === manageUserGroup.value);
  userGroupMembers.innerHTML = memberRows(userGroup?.members, 'data-remove-group-user');
  const groupMemberIds = new Set(userGroup?.members.map((member) => member.id));
  existingGroupUser.innerHTML = adminUsers.filter((item) => !groupMemberIds.has(item.id)).map((item) => `<option value="${escapeHtml(item.id)}">${escapeHtml(item.display_name)}</option>`).join('');
  addGroupMember.disabled = !existingGroupUser.value || !userGroup;
  inviteForm.elements.room_ids.innerHTML = adminRooms.map((item) => `<option value="${escapeHtml(item.id)}">${escapeHtml(item.name)}</option>`).join('');
  if (room) inviteForm.elements.room_ids.value = room.id;
  inviteForm.elements.user_group_id.innerHTML = `<option value="">${escapeHtml(t('none'))}</option>${adminUserGroups.map((item) => `<option value="${escapeHtml(item.id)}">${escapeHtml(item.name)}</option>`).join('')}`;
}
async function loadComplianceQueue() {
  complianceState.textContent = 'Loading…';
  try {
    const data = await api('api/admin/compliance');
    const reports = data.reports.map((report) => `<div class="group-member"><span><strong>Report: ${escapeHtml(report.category.replaceAll('_',' '))}</strong><small>${escapeHtml(report.reporter_name)} reported ${escapeHtml(report.reported_user_name)} · ${escapeHtml(report.conversation_id)} message ${report.message_id}</small></span><button type="button" data-compliance-kind="reports" data-compliance-id="${report.id}" data-compliance-status="resolved">Resolve</button></div>`);
    const deletions = data.deletion_requests.map((request) => `<div class="group-member"><span><strong>Delete account</strong><small>${escapeHtml(request.display_name || request.contact || 'Unknown account')} · ${escapeHtml(request.source)}</small></span><button type="button" data-compliance-kind="deletions" data-compliance-id="${request.id}" data-compliance-status="completed">Complete</button></div>`);
    complianceQueue.innerHTML = [...reports,...deletions].join('') || '<p class="empty-inline">Nothing waiting.</p>';
    complianceState.textContent = '';
  } catch { complianceState.textContent = 'Could not load the safety queue.'; }
}
adminOpen.addEventListener('click', async () => { adminZone.showModal(); await Promise.allSettled([loadAdminData(currentRoom),loadComplianceQueue()]); });
complianceRefresh.addEventListener('click', loadComplianceQueue);
complianceQueue.addEventListener('click', async (event) => {
  const button = event.target.closest('[data-compliance-kind]'); if (!button) return;
  button.disabled = true;
  try { await api(`api/admin/compliance/${button.dataset.complianceKind}/${button.dataset.complianceId}`, {method:'PATCH',body:JSON.stringify({status:button.dataset.complianceStatus})}); await loadComplianceQueue(); }
  catch { complianceState.textContent = 'Could not update that item.'; button.disabled = false; }
});
adminClose.addEventListener('click', () => adminZone.close());
adminZone.addEventListener('click', (event) => { if (event.target === adminZone) adminZone.close(); });
manageRoom.addEventListener('change', () => loadAdminData(manageRoom.value));
manageUserGroup.addEventListener('change', () => loadAdminData(manageRoom.value, manageUserGroup.value));
newGroupForm.addEventListener('submit', async (event) => { event.preventDefault(); groupState.textContent = 'Creating…'; try { const result = await api('api/admin/rooms', {method:'POST',body:JSON.stringify({name:new FormData(newGroupForm).get('name')})}); newGroupForm.reset(); groupState.textContent = `${result.room.name} created.`; await loadAdminData(result.room.id); await refresh(); } catch(error) { groupState.textContent = error.message === 'room_exists' ? 'That room already exists.' : 'Could not create room.'; } });
addMember.addEventListener('click', async () => { if (!existingUser.value) return; await api(`api/admin/rooms/${encodeURIComponent(manageRoom.value)}/members`, {method:'POST',body:JSON.stringify({user_id:existingUser.value})}); await loadAdminData(manageRoom.value); });
groupMembers.addEventListener('click', async (event) => { const button = event.target.closest('[data-remove-user]'); if (!button) return; button.disabled = true; try { await api(`api/admin/rooms/${encodeURIComponent(manageRoom.value)}/members/${encodeURIComponent(button.dataset.removeUser)}`, {method:'DELETE'}); await loadAdminData(manageRoom.value); } catch { groupState.textContent = 'Could not remove that member.'; button.disabled = false; } });
newUserGroupForm.addEventListener('submit', async (event) => { event.preventDefault(); userGroupState.textContent = 'Creating…'; try { const result = await api('api/admin/user-groups', {method:'POST',body:JSON.stringify({name:new FormData(newUserGroupForm).get('name')})}); newUserGroupForm.reset(); userGroupState.textContent = `${result.group.name} created.`; await loadAdminData(manageRoom.value, result.group.id); } catch(error) { userGroupState.textContent = error.message === 'user_group_exists' ? 'That user group already exists.' : 'Could not create user group.'; } });
addGroupMember.addEventListener('click', async () => { if (!existingGroupUser.value || !manageUserGroup.value) return; await api(`api/admin/user-groups/${encodeURIComponent(manageUserGroup.value)}/members`, {method:'POST',body:JSON.stringify({user_id:existingGroupUser.value})}); await loadAdminData(manageRoom.value, manageUserGroup.value); });
userGroupMembers.addEventListener('click', async (event) => { const button = event.target.closest('[data-remove-group-user]'); if (!button) return; button.disabled = true; try { await api(`api/admin/user-groups/${encodeURIComponent(manageUserGroup.value)}/members/${encodeURIComponent(button.dataset.removeGroupUser)}`, {method:'DELETE'}); await loadAdminData(manageRoom.value, manageUserGroup.value); } catch { userGroupState.textContent = 'Could not remove that user.'; button.disabled = false; } });
inviteForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  const data = new FormData(inviteForm);
  inviteGenerate.disabled = true; inviteState.textContent = 'Generating…'; inviteResult.hidden = true;
  try {
    const invitation = await api('api/admin/invitations', {
      method:'POST',
      body:JSON.stringify({
        display_name:String(data.get('display_name') || '').trim(),
        username:String(data.get('username') || '').trim(),
        email:String(data.get('email') || '').trim(),
        room_ids:data.getAll('room_ids').map(String),
        user_group_id:String(data.get('user_group_id') || ''),
      }),
    });
    inviteLink.href = invitation.url; inviteLink.textContent = invitation.url;
    inviteQr.src = invitation.qr_data_url;
    inviteLink.dataset.roomName = invitation.room_ids.map((id) => adminRooms.find((room) => room.id === id)?.name || id).join(', ');
    inviteResult.hidden = false; inviteState.textContent = '';
  } catch (error) {
    inviteState.textContent = error.message === 'invalid_invitation' ? 'Check the name, username, and email.' : 'Could not generate the invite.';
  } finally { inviteGenerate.disabled = false; }
});
inviteShare.addEventListener('click', async () => {
  const url = inviteLink.href;
  const roomName = inviteLink.dataset.roomName;
  if (navigator.share) await navigator.share({title:'Join SUPACHAT', text:roomName ? `Join these SUPACHAT rooms: ${roomName}` : 'Join SUPACHAT', url});
  else { await navigator.clipboard.writeText(url); inviteState.textContent = 'Invite link copied.'; }
});
function renderPresence(presence) {
  presenceEl.innerHTML = presence.map((person) => `<div class="person ${identityClass(person.user_id)}"><strong><span class="dot ${person.status}"></span>${escapeHtml(person.display_name)}</strong><small>${person.status === 'online' ? t('online') : person.last_seen_at ? t('seen',{time:time(person.last_seen_at)}) : t('offline')}</small></div>`).join('');
}
input.addEventListener('input', () => count.textContent = [...input.value].length);
logout?.addEventListener('click', async () => {
  if (location.hostname === 'supachat.net') { location.href = '/outpost.goauthentik.io/sign_out'; return; }
  await api('logout', { method:'POST', body:'{}' });
  location.href = 'login';
});
composer.addEventListener('submit', async (event) => {
  event.preventDefault();
  const value = input.value.trim();
  if (!value) return;
  sendState.textContent = t('sending');
  input.disabled = true;
  try {
    await api('api/messages', { method:'POST', body:JSON.stringify({body:value,client_id:crypto.randomUUID(),room_id:currentRoom}) });
    input.value = ''; count.textContent = '0'; sendState.textContent = t('sent');
  } catch { sendState.textContent = t('sendFailed'); }
  finally { input.disabled = false; input.focus(); }
});
const events = new EventSource('api/events');
events.addEventListener('message', (event) => {
  const message = JSON.parse(event.data);
  if (message.conversation_id !== currentRoom) return;
  const incoming = message.author_id !== currentUser?.id && !messages.some((item) => item.id === message.id);
  if (!messages.some((item) => item.id === message.id)) messages.push(message);
  messages = messages.slice(-100); renderMessages(true);
  if (incoming) playNotificationSound();
});
events.addEventListener('receipt', async () => {
  const requestedRoom = currentRoom;
  const result = await api(`api/messages?room=${encodeURIComponent(requestedRoom)}&limit=100`);
  if (currentRoom !== requestedRoom) return;
  messages = result.messages; renderMessages();
});
events.onerror = () => { sendState.textContent = t('reconnecting'); };
setInterval(async () => { try { const requestedRoom=currentRoom; const result=await api(`api/presence?room=${encodeURIComponent(requestedRoom)}`); if(currentRoom===requestedRoom)renderPresence(result.presence); sendState.textContent = t('ready'); } catch {} }, 20_000);
refresh();
