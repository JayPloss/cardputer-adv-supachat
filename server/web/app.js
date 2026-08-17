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
const adminOpen = document.querySelector('#admin-open');
const adminZone = document.querySelector('#admin-zone');
const adminClose = document.querySelector('#admin-close');
const inviteForm = document.querySelector('#invite-form');
const inviteState = document.querySelector('#invite-state');
const inviteResult = document.querySelector('#invite-result');
const inviteLink = document.querySelector('#invite-link');
const inviteShare = document.querySelector('#invite-share');
const inviteGenerate = document.querySelector('#invite-generate');
const roomSelect = document.querySelector('#room-select');
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
let adminGroups = []; let adminUsers = [];
const welcomeRequested = new URLSearchParams(location.search).get('welcome') === '1';
let messages = [];
let currentUser = null;
let rooms = [];
let currentRoom = new URLSearchParams(location.search).get('room') || localStorage.getItem('supachat-room') || 'family';
let clipRoom = '';

const api = async (path, options = {}) => {
  const response = await fetch(path, { ...options, headers: { 'content-type': 'application/json', ...(options.headers || {}) } });
  if (response.status === 401) { location.href = 'login'; throw new Error('unauthorized'); }
  if (!response.ok) throw new Error((await response.json()).error || 'request_failed');
  return response.json();
};
const escapeHtml = (value) => value.replace(/[&<>'"]/g, (char) => ({'&':'&amp;','<':'&lt;','>':'&gt;',"'":'&#39;','"':'&quot;'}[char]));
const time = (value) => new Intl.DateTimeFormat([], {hour:'numeric',minute:'2-digit'}).format(new Date(value));
const identityClass = (id) => ['albie', 'juju', 'papa'].includes(id) ? `author-${id}` : 'author-other';
const receiptLabel = (message) => {
  const names = { albie: 'Albie', juju: 'Juju' };
  const deviceReceipts = (message.receipts || []).filter((item) => names[item.user_id]);
  const read = deviceReceipts.filter((item) => item.state === 'read').map((item) => names[item.user_id]);
  if (read.length) return `Read by ${read.join(', ')}`;
  const delivered = deviceReceipts.filter((item) => item.state === 'delivered').map((item) => names[item.user_id]);
  return delivered.length ? `Delivered to ${delivered.join(', ')}` : 'Saved';
};
function renderMessages(scroll = false) {
  messagesEl.innerHTML = messages.length ? messages.map((message) => `
    <article class="message ${message.author_id === currentUser?.id ? 'mine' : ''} ${identityClass(message.author_id)}" data-id="${message.id}">
      <div class="bubble">${message.type === 'voice' ? `<button class="voice-play" data-voice-id="${message.id}">▶ Play voice</button><span class="voice-wave">${Math.round((message.voice?.duration_ms || 0) / 100) / 10}s</span>` : escapeHtml(message.body)}</div>
      <div class="message-meta"><strong>${escapeHtml(message.author_name)}</strong><span>${time(message.created_at)}</span>${message.author_id === currentUser?.id ? `<span class="receipt">${receiptLabel(message)}</span>` : ''}</div>
    </article>`).join('') : '<p class="empty">No messages yet.<br>Say the first thing.</p>';
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
  soundToggle.textContent = `Sound: ${notificationSoundEnabled ? 'on' : 'off'}`;
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
  button.disabled = true; button.textContent = 'Playing…';
  try {
    const response = await fetch(`api/voice/${button.dataset.voiceId}/audio`);
    if (!response.ok) throw new Error('play_failed');
    await playPcm(new Uint8Array(await response.arrayBuffer()));
  } finally { button.disabled = false; button.textContent = '▶ Play voice'; }
});

let clipChunks = [];
let clipTimer;
voiceClipButton.addEventListener('click', async () => {
  if (captureProcessor) {
    clearTimeout(clipTimer); stopCapture();
    const size = clipChunks.reduce((total, chunk) => total + chunk.length, 0);
    const pcm = new Uint8Array(size); let offset = 0;
    for (const chunk of clipChunks) { pcm.set(chunk, offset); offset += chunk.length; }
    voiceClipButton.textContent = 'Uploading…'; voiceClipButton.disabled = true;
    try {
      const response = await fetch('api/voice', { method:'POST', headers:{'content-type':'application/octet-stream','x-client-id':crypto.randomUUID(),'x-sample-rate':'8000','x-room-id':clipRoom}, body:pcm });
      if (!response.ok) throw new Error('upload_failed');
      voiceClipButton.textContent = 'Record voice clip';
    } catch { voiceClipButton.textContent = 'Upload failed — retry'; }
    finally { voiceClipButton.disabled = false; clipChunks = []; }
    return;
  }
  clipChunks = [];
  try {
    clipRoom = currentRoom;
    await startCapture((chunk) => clipChunks.push(chunk));
    voiceClipButton.textContent = 'Stop + send (5s max)';
    clipTimer = setTimeout(() => voiceClipButton.click(), 5000);
  } catch { voiceClipButton.textContent = 'Microphone unavailable'; }
});

let walkieSocket;
let pttGranted = false;
let pttHeld = false;
function connectWalkie() {
  const url = new URL('walkie', location.href); url.protocol = location.protocol === 'https:' ? 'wss:' : 'ws:'; url.searchParams.set('room', currentRoom);
  walkieSocket = new WebSocket(url); walkieSocket.binaryType = 'arraybuffer';
  walkieSocket.onopen = () => walkieState.textContent = 'Ready — hold to talk';
  walkieSocket.onmessage = async (event) => {
    if (event.data instanceof ArrayBuffer) { await playPcm(new Uint8Array(event.data)); return; }
    const message = JSON.parse(event.data);
    if (message.type === 'ptt_start') {
      pttGranted = message.user === currentUser?.id;
      walkieState.textContent = pttGranted ? `Transmitting as ${currentUser?.display_name || 'you'}` : `${message.user} is talking`;
      pttButton.classList.toggle('transmitting', pttGranted);
    } else if (message.type === 'ptt_stop') {
      pttGranted = false; walkieState.textContent = 'Ready — hold to talk'; pttButton.classList.remove('transmitting');
    } else if (message.type === 'busy') {
      pttGranted = false; pttHeld = false; stopCapture(); pttButton.classList.remove('transmitting');
      walkieState.textContent = `${message.speaker} has the channel`;
    }
  };
  walkieSocket.onclose = () => { walkieState.textContent = 'Reconnecting…'; setTimeout(connectWalkie, 1500); };
}
async function pttStart(event) {
  event.preventDefault(); if (walkieSocket?.readyState !== WebSocket.OPEN || captureProcessor) return;
  pttHeld = true; pttButton.setPointerCapture?.(event.pointerId);
  walkieSocket.send(JSON.stringify({type:'ptt_start'}));
  try {
    await startCapture((chunk) => { if (pttHeld && pttGranted && walkieSocket.readyState === WebSocket.OPEN) walkieSocket.send(chunk); });
    if (!pttHeld) stopCapture();
  } catch {
    pttHeld = false; walkieState.textContent = 'Microphone unavailable';
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
  if (!rooms.some((room) => room.id === currentRoom)) currentRoom = rooms[0]?.id || 'family';
  roomSelect.innerHTML = rooms.map((room) => `<option value="${escapeHtml(room.id)}">${escapeHtml(room.name)}</option>`).join('');
  roomSelect.value = currentRoom;
  const activeRoomName = rooms.find((room) => room.id === currentRoom)?.name || 'this room';
  composerLabel.textContent = `Message ${activeRoomName}`;
  voiceRoomNote.textContent = `One person talks at a time. Voice clips stay in ${activeRoomName} history.`;
  welcomeTitle.textContent = `You’re in ${activeRoomName}.`;
  welcomeRoomCopy.textContent = `This invitation added you to the ${activeRoomName} conversation.`;
  const requestedRoom = currentRoom;
  const [{ messages: next }, { presence }] = await Promise.all([api(`api/messages?room=${encodeURIComponent(requestedRoom)}&limit=100`), api(`api/presence?room=${encodeURIComponent(requestedRoom)}`)]);
  if (currentRoom !== requestedRoom) return;
  adminOpen.hidden = currentUser?.role !== 'admin';
  messages = next;
  renderMessages(true);
  renderPresence(presence);
  if (welcomeRequested) {
    const welcomeKey = `supachat-welcomed:${currentUser.id}`;
    if (!localStorage.getItem(welcomeKey)) welcomeZone.showModal();
    else clearWelcomeMarker();
  }
}
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
async function loadAdminGroups(selected = manageRoom.value) {
  const data = await api('api/admin/groups'); adminGroups = data.groups; adminUsers = data.users;
  manageRoom.innerHTML = adminGroups.map((group) => `<option value="${escapeHtml(group.id)}">${escapeHtml(group.name)} (${group.member_count})</option>`).join('');
  manageRoom.value = adminGroups.some((group) => group.id === selected) ? selected : adminGroups[0]?.id || '';
  const group = adminGroups.find((item) => item.id === manageRoom.value);
  groupMembers.innerHTML = group?.members.length ? group.members.map((member) => `<div class="group-member"><span>${escapeHtml(member.display_name)} <small>${escapeHtml(member.kind)}</small></span><button type="button" data-remove-user="${escapeHtml(member.id)}" ${member.id === currentUser.id ? 'disabled title="You cannot remove yourself"' : ''}>Remove</button></div>`).join('') : '<p class="empty-inline">No members.</p>';
  const memberIds = new Set(group?.members.map((member) => member.id));
  existingUser.innerHTML = adminUsers.filter((item) => !memberIds.has(item.id)).map((item) => `<option value="${escapeHtml(item.id)}">${escapeHtml(item.display_name)}</option>`).join('');
  addMember.disabled = !existingUser.value;
  inviteForm.elements.room_id.innerHTML = adminGroups.map((item) => `<option value="${escapeHtml(item.id)}">${escapeHtml(item.name)}</option>`).join('');
  if (group) inviteForm.elements.room_id.value = group.id;
}
adminOpen.addEventListener('click', async () => { adminZone.showModal(); try { await loadAdminGroups(currentRoom); } catch { groupState.textContent = 'Could not load groups.'; } });
adminClose.addEventListener('click', () => adminZone.close());
adminZone.addEventListener('click', (event) => { if (event.target === adminZone) adminZone.close(); });
manageRoom.addEventListener('change', () => loadAdminGroups(manageRoom.value));
newGroupForm.addEventListener('submit', async (event) => { event.preventDefault(); groupState.textContent = 'Creating…'; try { const result = await api('api/admin/groups', {method:'POST',body:JSON.stringify({name:new FormData(newGroupForm).get('name')})}); newGroupForm.reset(); groupState.textContent = `${result.group.name} created.`; await loadAdminGroups(result.group.id); await refresh(); } catch(error) { groupState.textContent = error.message === 'group_exists' ? 'That group already exists.' : 'Could not create group.'; } });
addMember.addEventListener('click', async () => { if (!existingUser.value) return; await api(`api/admin/groups/${encodeURIComponent(manageRoom.value)}/members`, {method:'POST',body:JSON.stringify({user_id:existingUser.value})}); await loadAdminGroups(manageRoom.value); });
groupMembers.addEventListener('click', async (event) => { const button = event.target.closest('[data-remove-user]'); if (!button) return; button.disabled = true; try { await api(`api/admin/groups/${encodeURIComponent(manageRoom.value)}/members/${encodeURIComponent(button.dataset.removeUser)}`, {method:'DELETE'}); await loadAdminGroups(manageRoom.value); } catch { groupState.textContent = 'Could not remove that member.'; button.disabled = false; } });
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
        room_id:String(data.get('room_id') || ''),
      }),
    });
    inviteLink.href = invitation.url; inviteLink.textContent = invitation.url;
    inviteLink.dataset.roomName = adminGroups.find((group) => group.id === invitation.room_id)?.name || invitation.room_id;
    inviteResult.hidden = false; inviteState.textContent = '';
  } catch (error) {
    inviteState.textContent = error.message === 'invalid_invitation' ? 'Check the name, username, and email.' : 'Could not generate the invite.';
  } finally { inviteGenerate.disabled = false; }
});
inviteShare.addEventListener('click', async () => {
  const url = inviteLink.href;
  const roomName = inviteLink.dataset.roomName || 'group';
  if (navigator.share) await navigator.share({title:'Join SUPACHAT', text:`Join our SUPACHAT ${roomName} room`, url});
  else { await navigator.clipboard.writeText(url); inviteState.textContent = 'Invite link copied.'; }
});
function renderPresence(presence) {
  presenceEl.innerHTML = presence.map((person) => `<div class="person ${identityClass(person.user_id)}"><strong><span class="dot ${person.status}"></span>${escapeHtml(person.display_name)}</strong><small>${person.status === 'online' ? 'online' : person.last_seen_at ? `seen ${time(person.last_seen_at)}` : 'offline'}</small></div>`).join('');
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
  sendState.textContent = 'Sending…';
  input.disabled = true;
  try {
    await api('api/messages', { method:'POST', body:JSON.stringify({body:value,client_id:crypto.randomUUID(),room_id:currentRoom}) });
    input.value = ''; count.textContent = '0'; sendState.textContent = 'Sent';
  } catch { sendState.textContent = 'Not sent — try again'; }
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
events.onerror = () => { sendState.textContent = 'Reconnecting…'; };
setInterval(async () => { try { const requestedRoom=currentRoom; const result=await api(`api/presence?room=${encodeURIComponent(requestedRoom)}`); if(currentRoom===requestedRoom)renderPresence(result.presence); sendState.textContent = 'Ready'; } catch {} }, 20_000);
refresh();
