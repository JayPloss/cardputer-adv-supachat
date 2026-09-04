export const menuPages = [
  ['BACK TO CHAT','ROOMS','SYNC NOW','VOICE MESSAGES','WALKIE-TALKIE','VOLUME'],
  ['LANGUAGE','NETWORKS','ESP-NOW LOCAL','CHARGING MODE','STATUS','CHANGELOG'],
  ['EMOJI RECIPES','FOX FINDING'],
];
export const menuItems = menuPages.flat();
export const changelog = [
  {version:'v0.53',lines:['Reliable Fox peer discovery','Shared ESP-NOW hunt channel','Newest-first history pages','Compact device sync payload','Unified feature release']},
  {version:'v0.52',lines:['Newest-first history pages','Compact device sync payload','Large-room JSON fix']},
  {version:'v0.51',lines:['Simpler charging screen','20-second screen timeout','Reduced charging redraws']},
  {version:'v0.50',lines:['Papa custom boot art','Mama build + boot art']},
  {version:'v0.49',lines:['Low-power charging mode','Charge history plot','Battery event logging','Boot key quarantine','Verbose sync diagnostics']},
  {version:'v0.48',lines:['Scrollable build changes','Working language controls','Reliable room switching','Zero-copy history sync','Fixed cable detection','Clean battery digits','Voice / walkie split']},
  {version:'v0.47',lines:['Paged menus','Build changelog','ESP-NOW local only','Language menu fix']},
  {version:'v0.46',lines:['Groups own rooms','EN/FR preferences','Automatic group language','QR group invites']},
  {version:'v0.45',lines:['Contextual arrows','Correct French glyphs','Fallback WiFi profiles','Legacy mesh key fix']},
  {version:'v0.44',lines:['French accents','Complete key chorus','Original startup song','Bilingual web UI']},
  {version:'v0.43',lines:['History + notices','Sender names + colours','ESP-NOW fallback','Complete startup melody']},
  {version:'v0.42',lines:['Wolfpack terminals','Emma, Naomie, Andrew','Voice + local replay','Startup audio fixes']},
];
export const networks = [
  {ssid:'Plossco Family',rssi:-41,open:false},{ssid:'Papa Hotspot',rssi:-57,open:false},
  {ssid:'Library Guest',rssi:-68,open:true},{ssid:'NETGEAR-2G',rssi:-76,open:false}
];
export const navPositions = {up:[11,2],left:[10,3],down:[11,3],right:[12,3]};
export function interpretRawKeys(screen,{fn=false,shift=false,ctrl=false,alt=false,opt=false,enter=false,word=[],keyList=[]}={}){
  if(enter)return{kind:'enter'};
  const textEntry=screen==='chat'||screen==='password',otherModifier=shift||ctrl||alt||opt;
  const navigationChord=!otherModifier&&(textEntry?fn:!fn);
  if(navigationChord)for(const[direction,[x,y]]of Object.entries(navPositions))if(keyList.some(key=>key.x===x&&key.y===y))return{kind:'navigation',direction};
  if(textEntry&&word.length)return{kind:'text',text:word.join('')};
  return{kind:'ignored'};
}
export class SupaChatState {
  constructor(name='Albie',language='en') {
    Object.assign(this,{name,screen:'chat',menuPage:0,menuSelections:[0,0,0],changelogSelection:0,changelogLineOffset:0,statusPage:0,localOnly:false,networkSelection:0,network:'SYNCED',ssid:'Plossco Family',
      walkie:'READY',transport:'HETZNER',recording:false,spaceHeld:false,recordedMs:0,clipReady:false,clipPending:false,
      replayAudible:false,voiceSelection:0,volume:3,draft:'',password:'',selectedSsid:'',tones:0,notifications:0,http:200,heap:181432,
      batteryLevel:79,batteryVoltageMv:3986,externalPowerDetected:false,roomSelection:0,currentRoom:'Family',rooms:['Family','K-BUDS','Wolfpack'],roomNew:[false,true,false],
      easternTime:'3:42P',language,languageOverride:'auto',gravePending:false,
      bootElapsedMs:0,bootSkipped:false,bootActive:true,
      charging:false,chargingDimmed:false,chargingElapsedMs:0,chargingScreenIdleMs:0,chargingStartMv:3986,chargeNextSampleMs:300000,chargeTrend:'WAITING',chargePoints:[],chargeEvents:[],
      wifiEnabled:true,espNowEnabled:true,syncEnabled:true,audioEnabled:true,inputQuarantined:false,
      foxState:'selecting',foxPeers:[{name:'Juju',rssi:-58},{name:'Papa',rssi:-71}],foxSelection:0,foxTarget:'',foxRssi:-127,foxGuidance:'CALIBRATING',foxPlaceSimilarity:0,foxPlaceOverlap:0,foxConfidence:0,foxPackInvite:null,foxJoinedPack:false,foxPackEvidence:[],foxForcedLocalOnly:false,
      lastSyncError:'NONE',lastSyncDetail:'',lastSyncErrorHttp:0,lastSyncResponseBytes:0,lastSyncErrorHeap:0,lastSyncErrorRoom:'',lastSyncErrorPhase:'-',lastSyncErrorAgeSeconds:0,
      messages:[{id:'papa',messageId:41,author:'Papa',body:'Hey Albie — testing SupaChat.',state:'read'},{id:'juju',messageId:42,author:'Julien',body:'I found the orange channel.',state:'saved'},{id:'papa',messageId:43,author:'Papa',body:'',voice:true,state:'saved'},{id:'albie',messageId:44,author:name,body:'I am online!',state:'saved'}]});
  }
  tone(){if(this.audioEnabled&&this.volume>0)this.tones++}
  bootTick(ms){if(!this.bootSkipped)this.bootElapsedMs+=ms}
  skipBoot(){this.bootSkipped=true;this.bootActive=false;this.inputQuarantined=true}
  releaseInputs(){this.inputQuarantined=false}
  receive(author,body,id=author.toLowerCase(),room=this.currentRoom){if(this.charging||room!==this.currentRoom)return false;this.messages.push({id,author,body,state:'saved'});if(author!==this.name&&this.volume>0)this.notifications++;return true}
  menu(){this.screen=this.screen==='menu'?'chat':'menu';this.tone()}
  switchRoom(direction){this.roomSelection=(this.roomSelection+direction+this.rooms.length)%this.rooms.length;this.currentRoom=this.rooms[this.roomSelection];this.roomNew[this.roomSelection]=false;this.messages=[];this.network='SWITCHING'}
  left(){if(this.screen==='chat')this.switchRoom(-1);else if(this.screen==='menu')this.menuPage=(this.menuPage+2)%3;else if(this.screen==='fox-finding'){this.foxState='selecting';this.screen='menu';if(this.foxForcedLocalOnly){this.foxForcedLocalOnly=false;this.setLocalOnly(false)}}else if(this.screen==='charging')this.stopCharging();else if(this.screen==='charging-confirm')this.screen='menu';else if(this.screen==='language')this.cycleLanguage(-1);else if(this.screen==='status')this.statusPage=1-this.statusPage;else if(this.screen==='changelog'){this.changelogSelection=Math.max(0,this.changelogSelection-1);this.changelogLineOffset=0}else this.screen='menu';this.tone()}
  up(){if(this.screen==='menu'){const count=menuPages[this.menuPage].length;this.menuSelections[this.menuPage]=(this.menuSelections[this.menuPage]+count-1)%count}else if(this.screen==='changelog')this.changelogLineOffset=Math.max(0,this.changelogLineOffset-1);else if(this.screen==='rooms')this.roomSelection=Math.max(0,this.roomSelection-1);else if(this.screen==='networks')this.networkSelection=Math.max(0,this.networkSelection-1);else if(this.screen==='voice-messages')this.voiceSelection=Math.max(0,this.voiceSelection-1);this.tone()}
  down(){if(this.screen==='menu'){const count=menuPages[this.menuPage].length;this.menuSelections[this.menuPage]=(this.menuSelections[this.menuPage]+1)%count}else if(this.screen==='changelog')this.changelogLineOffset=Math.min(Math.max(0,changelog[this.changelogSelection].lines.length-4),this.changelogLineOffset+1);else if(this.screen==='rooms')this.roomSelection=Math.min(this.rooms.length-1,this.roomSelection+1);else if(this.screen==='networks')this.networkSelection=Math.min(networks.length-1,this.networkSelection+1);else if(this.screen==='voice-messages')this.voiceSelection=Math.min(this.messages.filter(m=>m.voice).length-1,this.voiceSelection+1);this.tone()}
  right(){if(this.screen==='chat')this.switchRoom(1);else if(this.screen==='menu')this.menuPage=(this.menuPage+1)%3;else if(this.screen==='status')this.statusPage=1-this.statusPage;else if(this.screen==='changelog'){this.changelogSelection=Math.min(changelog.length-1,this.changelogSelection+1);this.changelogLineOffset=0}else if(this.screen==='networks')this.selectNetwork();else if(this.screen==='volume')this.volume=Math.min(4,this.volume+1);else if(this.screen==='language')this.cycleLanguage(1);this.tone()}
  applyRawKeys(keys){if(this.inputQuarantined)return'ignored';if(this.charging&&this.chargingDimmed){this.chargingDimmed=false;this.chargingScreenIdleMs=0;return'wake'}const action=interpretRawKeys(this.screen,keys);if(this.charging)this.chargingScreenIdleMs=0;if(action.kind==='navigation')this[action.direction]();else if(action.kind==='text')this.type(action.text,{shift:keys.shift});else if(action.kind==='enter')this.enter();return action.kind}
  physicalKey(direction,printable,{fn=false,shift=false}={}){const[x,y]=navPositions[direction];return this.applyRawKeys({fn,shift,word:printable?[printable]:[],keyList:[{x,y}]})}
  enter(){
    if(this.gravePending&&(this.screen==='chat'||this.screen==='password')){this[this.screen==='chat'?'draft':'password']+="'";this.gravePending=false}
    if(this.screen==='menu')this.open(menuPages[this.menuPage][this.menuSelections[this.menuPage]]);
    else if(this.screen==='voice-messages'){this.replayAudible=!this.replayAudible;this.walkie=this.replayAudible?'PLAYING':'STOPPED'}
    else if(this.screen==='rooms'){this.currentRoom=this.rooms[this.roomSelection];this.messages=[];this.screen='chat'}
    else if(this.screen==='charging-confirm')this.startCharging()
    else if(this.screen==='charging')this.chargingDimmed=true
    else if(this.screen==='fox-finding'&&this.foxState==='selecting')(this.foxPackInvite?this.joinPack():this.startFox())
    else if(this.screen==='language'||this.screen==='changelog'||this.screen==='status'){this.statusPage=0;this.screen='menu'}
    else if(this.screen==='networks')this.selectNetwork();
    else if(this.screen==='password'){this.network='SAVED + CONNECTED';this.ssid=this.selectedSsid;this.screen='networks'}
    else if(this.screen==='chat'&&this.draft){this.messages.push({id:this.name.toLowerCase(),author:this.name,body:this.draft,state:'queued'});this.draft=''}
    this.tone();
  }
  open(item){if(item==='BACK TO CHAT')this.screen='chat';else if(item==='SYNC NOW'){if(!this.localOnly)this.sync()}else if(item==='VOICE MESSAGES')this.screen='voice-messages';else if(item==='WALKIE-TALKIE')this.screen='walkie';else if(item==='ESP-NOW LOCAL')this.setLocalOnly(!this.localOnly);else if(item==='CHARGING MODE')this.screen='charging-confirm';else if(item==='FOX FINDING'){this.screen='fox-finding';this.foxState='selecting';if(!this.localOnly){this.foxForcedLocalOnly=true;this.setLocalOnly(true)}}else if(item==='EMOJI RECIPES')this.screen='emoji-recipes';else this.screen=item.toLowerCase()}
  startFox(){const peer=this.foxPeers[this.foxSelection];this.foxTarget=peer.name;this.foxRssi=peer.rssi;this.foxState='requesting';this.setLocalOnly(true)}
  foxAck(){this.foxState='acquiring'}
  foxPlaceResult({similarity=76,overlap=7,confidence=3,rssi=-55,guidance='WARMER'}={}){this.foxPlaceSimilarity=similarity;this.foxPlaceOverlap=overlap;this.foxConfidence=confidence;this.foxRssi=rssi;this.foxGuidance=guidance;this.foxState='guiding'}
  foxLoseSignal(){this.foxState='signal-lost';this.foxGuidance='SIGNAL LOST'}
  offerPack(hunter='Papa',quarry='Juju',session=42){this.foxPackInvite={hunter,quarry,session};this.foxState='selecting'}
  joinPack(){this.foxTarget=this.foxPackInvite.quarry;this.foxJoinedPack=true;this.foxState='acquiring';this.setLocalOnly(true)}
  packObservation(hunter,{rssi,similarity,confidence,trend=0}){const previous=this.foxPackEvidence.find(e=>e.hunter===hunter),next={hunter,rssi,similarity,confidence,trend};if(previous)Object.assign(previous,next);else this.foxPackEvidence.push(next)}
  packLeader(){return this.foxPackEvidence.toSorted((a,b)=>(b.rssi*3+b.similarity*b.confidence/3)-(a.rssi*3+a.similarity*a.confidence/3))[0]?.hunter||''}
  startCharging(){this.charging=true;this.screen='charging';this.chargingElapsedMs=0;this.chargingScreenIdleMs=0;this.chargingStartMv=this.batteryVoltageMv;this.chargeNextSampleMs=300000;this.chargeTrend='WAITING';this.chargePoints=[{elapsed:0,mv:this.batteryVoltageMv,pct:this.batteryLevel}];this.chargeEvents=['MODE_ENTER','SAMPLE'];this.wifiEnabled=false;this.espNowEnabled=false;this.syncEnabled=false;this.audioEnabled=false;this.notifications=0;this.network='CHARGE MODE';this.ssid='';this.inputQuarantined=true}
  stopCharging(){this.chargeEvents.push('MODE_EXIT');this.charging=false;this.screen='menu';this.wifiEnabled=true;this.espNowEnabled=true;this.syncEnabled=true;this.audioEnabled=true;this.network='RESUMING';this.inputQuarantined=true}
  classifyCharge(){const values=this.chargePoints.slice(-5).map(point=>point.mv).concat(this.batteryVoltageMv);if(values.length<3)return'WAITING';const delta=values.at(-1)-values[0],span=Math.max(...values)-Math.min(...values);if(delta>=12)return'RISING';if(delta<=-12)return'FALLING';if(span<=12)return'FLAT';return'UNSTABLE'}
  chargeSample(){const previous=this.chargeTrend;this.chargeTrend=this.classifyCharge();this.chargePoints.push({elapsed:this.chargingElapsedMs,mv:this.batteryVoltageMv,pct:this.batteryLevel});if(this.chargePoints.length>144)this.chargePoints.shift();this.chargeEvents.push('SAMPLE');if(this.chargeTrend!==previous&&this.chargeTrend!=='WAITING')this.chargeEvents.push(`TREND_${this.chargeTrend}`)}
  setLocalOnly(enabled){this.localOnly=enabled;if(enabled){this.ssid='';this.network='ESPNOW LOCAL';this.transport='ESP-NOW'}else{this.network='WIFI RESUME';this.transport='OFFLINE'}}
  cycleLanguage(direction){const values=['auto','en','fr'];this.languageOverride=values[(values.indexOf(this.languageOverride)+direction+3)%3];if(this.languageOverride!=='auto')this.language=this.languageOverride}
  selectNetwork(){this.selectedSsid=networks[this.networkSelection].ssid;this.password='';this.network='TYPE PASSWORD';this.screen='password'}
  type(text,{shift=false}={}){if(this.screen!=='chat'&&this.screen!=='password')return;const target=this.screen==='chat'?'draft':'password';for(const character of text){if(this.language==='fr'&&this.gravePending){this.gravePending=false;if(character==='a'){this[target]+='à';continue}if(character==='e'){this[target]+='è';continue}this[target]+="'"}if(this.language==='fr'&&character==="'"){this.gravePending=true;continue}this[target]+=this.language==='fr'&&character==='?'?'é':character}this.tone();return shift&&text==='?'?'punctuation':'text'}
  backspace(){const target=this.screen==='chat'?'draft':this.screen==='password'?'password':null;if(!target)return;if(this.gravePending)this.gravePending=false;else this[target]=[...this[target]].slice(0,-1).join('')}
  holdSpace(){if(!['voice-messages','walkie'].includes(this.screen)||this.spaceHeld)return;this.spaceHeld=true;this.recording=true;this.recordedMs=0;this.walkie=this.screen==='walkie'?'TRANSMITTING':'RECORDING'}
  releaseSpace(){this.spaceHeld=false;if(!this.recording)return;this.recording=false;if(this.screen==='walkie'){this.clipReady=false;this.clipPending=false;this.walkie='READY TO TALK'}else{this.clipReady=this.recordedMs>=40;this.clipPending=this.clipReady;this.walkie=this.clipReady?'RECORDED - ENTER PLAYS':'READY'}}
  tick(ms){if(this.charging){this.chargingElapsedMs+=ms;if(!this.chargingDimmed){this.chargingScreenIdleMs+=ms;if(this.chargingScreenIdleMs>=20000)this.chargingDimmed=true}while(this.chargingElapsedMs>=this.chargeNextSampleMs){this.chargeSample();this.chargeNextSampleMs+=300000}return}if(!this.recording)return;this.recordedMs+=ms;if(this.recordedMs>=30000)this.releaseSpace()}
  sync(result='ok'){
    if(!this.syncEnabled)return;
    if(result==='io'){this.network='SYNC IO -1';this.transport='OFFLINE';this.http=-1;this.recordSyncError('SYNC IO','-1',-1,0)}
    else if(result==='auth'){this.network='SYNC HTTP 401';this.transport='OFFLINE';this.http=401;this.recordSyncError('SYNC HTTP','401',401,52)}
    else if(result==='bad-data'){this.network='BAD JSON';this.transport='HETZNER';this.http=200;this.recordSyncError('BAD JSON','IncompleteInput',200,6144)}
    else{this.network='SYNCED';this.transport='HETZNER';this.http=200;this.screen='chat'}
  }
  recordSyncError(kind,detail,http,bytes){this.lastSyncError=kind;this.lastSyncDetail=detail;this.lastSyncErrorHttp=http;this.lastSyncResponseBytes=bytes;this.lastSyncErrorHeap=this.heap;this.lastSyncErrorRoom=this.currentRoom;this.lastSyncErrorPhase='LIVE';this.lastSyncErrorAgeSeconds=0}
}
