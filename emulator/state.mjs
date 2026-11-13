export const menuItems = ['BACK TO CHAT','ROOMS','SYNC NOW','VOICE MESSAGES','WALKIE-TALKIE','EMOJI RECIPES','FOX FINDING'];
export const homeApps = ['SUPACHAT','MUSIC','BOOKS','R/C','TIMER','ALARM','SCREEN','STOPWATCH','WI-FI','VOLUME','STATUS','LANGUAGE','CHARGING','LOCAL OFF','CHANGELOG','SYSTEM'];
const clockScreens = ['timer','alarm','screen-time','stopwatch'];
const settingsScreens = ['networks','password','volume','status','language','charging-confirm','changelog'];
export const changelog = [
  {version:'v0.60',lines:['Big clock after 2 min idle','Sleep timer: T in players','Books at bedtime: 30 min','Alarm can play a song']},
  {version:'v0.59',lines:['Every book on the card shows','No crash listing at low RAM','MP3s with cover art play','VBR songs: right length','Says why a file won\'t play']},
  {version:'v0.58',lines:['Up to four alarms','Weekday and weekend switches','Fixed: a play could be lost','French keyboard: CSA layout','Tab in chat: French keys','Alarm time typed from 00:00','Hints name the real exit key']},
  {version:'v0.57',lines:['Unsent texts kept and retried','Sent/delivered/read marks','ONLINE or NEARBY shown','STATUS names the failure','Survives cert renewals','Clearer nearby walkie','Nearby texts confirmed']},
  {version:'v0.56',lines:['Timer and stopwatch','Alarm clock','Screen time countdown']},
  {version:'v0.55',lines:['Settings moved to Home','Pages of apps on Home','Bump at the last page']},
  {version:'v0.54',lines:['Home screen with apps','Music player','Audiobook player','Current server pin']},
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
// A copy of firmware/include/french_keyboard.h's kKeys; device-contract.mjs checks that it matches.
// Column, row, keycap, Shift keycap, then what the key types alone, with Shift, with Alt, with Alt+Shift.
export const frenchKeys = [[0,0,"`","~","/","\\","|",null],[1,0,"1","!","1","!",null,null],[2,0,"2","@","2","@",null,null],[3,0,"3","#","3","#",null,null],[4,0,"4","$","4","$",null,null],[5,0,"5","%","5","%",null,null],[6,0,"6","^","6","?",null,null],[7,0,"7","&","7","&","{",null],[8,0,"8","*","8","*","}",null],[9,0,"9","(","9","(","[",null],[10,0,"0",")","0",")","]",null],[11,0,"-","_","-","_",null,null],[12,0,"=","+","=","+",null,null],[1,1,"q","Q","q","Q",null,null],[2,1,"w","W","w","W",null,null],[3,1,"e","E","e","E",null,null],[4,1,"r","R","r","R",null,null],[5,1,"t","T","t","T",null,null],[6,1,"y","Y","y","Y",null,null],[7,1,"u","U","u","U","ù","Ù"],[8,1,"i","I","i","I",null,null],[9,1,"o","O","o","O",null,null],[10,1,"p","P","p","P",null,null],[11,1,"[","{",{"dead":"Circumflex"},{"dead":"Diaeresis"},{"dead":"Grave"},null],[12,1,"]","}","ç","Ç","~",null],[13,1,"\\","|","à","À","}",null],[2,2,"a","A","a","A",null,null],[3,2,"s","S","s","S",null,null],[4,2,"d","D","d","D",null,null],[5,2,"f","F","f","F",null,null],[6,2,"g","G","g","G",null,null],[7,2,"h","H","h","H",null,null],[8,2,"j","J","j","J",null,null],[9,2,"k","K","k","K",null,null],[10,2,"l","L","l","L",null,null],[11,2,";",":",";",":",null,null],[12,2,"'","\"","è","È","{",null],[3,3,"z","Z","z","Z","«",null],[4,3,"x","X","x","X","»",null],[5,3,"c","C","c","C",null,null],[6,3,"v","V","v","V",null,null],[7,3,"b","B","b","B",null,null],[8,3,"n","N","n","N",null,null],[9,3,"m","M","m","M",null,null],[10,3,",","<",",","'","<",null],[11,3,".",">",".","\"",">",null],[12,3,"/","?","é","É",null,null]];
export const frenchCompositions = {"Circumflex":{"a":"â","e":"ê","i":"î","o":"ô","u":"û","A":"Â","E":"Ê","I":"Î","O":"Ô","U":"Û"," ":"^"},"Diaeresis":{"e":"ë","i":"ï","u":"ü","y":"ÿ","E":"Ë","I":"Ï","U":"Ü"," ":"¨"},"Grave":{"a":"à","e":"è","u":"ù","A":"À","E":"È","U":"Ù"," ":"`"}};
const deadText={Circumflex:'^',Diaeresis:'¨',Grave:'`'};
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
    Object.assign(this,{name,screen:'chat',homeSelection:0,homeBump:0,homeNotice:'',deniedTones:0,menuSelection:0,changelogSelection:0,changelogLineOffset:0,statusPage:0,localOnly:false,networkSelection:0,network:'SYNCED',ssid:'Plossco Family',
      walkie:'READY',transport:'HETZNER',recording:false,spaceHeld:false,recordedMs:0,clipReady:false,clipPending:false,
      replayAudible:false,voiceSelection:0,volume:3,draft:'',password:'',selectedSsid:'',tones:0,notifications:0,http:200,heap:181432,
      batteryLevel:79,batteryVoltageMv:3986,externalPowerDetected:false,roomSelection:0,currentRoom:'Family',rooms:['Family','K-BUDS','Wolfpack'],roomNew:[false,true,false],
      easternTime:'3:42P',language,languageOverride:'auto',frenchDead:null,
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
  menu(){if(settingsScreens.includes(this.screen))this.leaveSettings();else if(clockScreens.includes(this.screen))this.screen='home';else this.screen=this.screen==='menu'?'home':this.screen==='home'?'home':'menu';this.tone()}
  leaveSettings(){if(this.screen==='password'){this.screen='networks';return}this.statusPage=0;this.screen='home'}
  homeMove(direction){const page=Math.floor(this.homeSelection/4),slot=this.homeSelection%4,onLeft=slot%2===0;if(direction==='up'||direction==='down')this.homeSelection=page*4+(slot+2)%4;else if(direction==='left'&&onLeft){if(page===0)return this.bump(-1);this.homeSelection=(page-1)*4+slot+1}else if(direction==='right'&&!onLeft){if((page+1)*4===homeApps.length)return this.bump(1);this.homeSelection=(page+1)*4+slot-1}else this.homeSelection+=onLeft?1:-1;this.tone()}
  bump(direction){this.homeBump=direction;this.deniedTones++}
  homeOpen(){const app=homeApps[this.homeSelection];if(app==='WI-FI'&&this.localOnly){this.homeNotice='TURN LOCAL OFF FIRST';return this.bump(0)}if(this.homeSelection<8)this.screen=['chat','music','books','remote','timer','alarm','screen-time','stopwatch'][this.homeSelection];else if(app==='WI-FI')this.screen='networks';else if(app==='CHARGING')this.open('CHARGING MODE');else if(app==='LOCAL OFF')this.open('ESP-NOW LOCAL');else this.open(app);this.tone()}
  escape(){if(this.screen==='keys'){this.screen='chat';return this.tone()}if(settingsScreens.includes(this.screen)){this.leaveSettings();this.tone()}else if(['chat','menu','music','books','remote','system',...clockScreens].includes(this.screen)){this.screen='home';this.tone()}}
  switchRoom(direction){this.roomSelection=(this.roomSelection+direction+this.rooms.length)%this.rooms.length;this.currentRoom=this.rooms[this.roomSelection];this.roomNew[this.roomSelection]=false;this.messages=[];this.network='SWITCHING'}
  left(){if(this.screen==='home')return this.homeMove('left');if(this.screen==='chat')this.switchRoom(-1);else if(this.screen==='menu')this.screen='chat';else if(this.screen==='fox-finding'){this.foxState='selecting';this.screen='menu';if(this.foxForcedLocalOnly){this.foxForcedLocalOnly=false;this.setLocalOnly(false)}}else if(this.screen==='charging')this.stopCharging();else if(this.screen==='charging-confirm')this.screen='home';else if(this.screen==='networks'||this.screen==='password')this.leaveSettings();else if(this.screen==='volume')this.volume=Math.max(0,this.volume-1);else if(this.screen==='language')this.cycleLanguage(-1);else if(this.screen==='status')this.statusPage=(this.statusPage+2)%3;else if(this.screen==='changelog'){this.changelogSelection=Math.max(0,this.changelogSelection-1);this.changelogLineOffset=0}else this.screen='menu';this.tone()}
  up(){if(this.screen==='home')return this.homeMove('up');if(this.screen==='menu'){this.menuSelection=(this.menuSelection+menuItems.length-1)%menuItems.length}else if(this.screen==='changelog')this.changelogLineOffset=Math.max(0,this.changelogLineOffset-1);else if(this.screen==='rooms')this.roomSelection=Math.max(0,this.roomSelection-1);else if(this.screen==='networks')this.networkSelection=Math.max(0,this.networkSelection-1);else if(this.screen==='voice-messages')this.voiceSelection=Math.max(0,this.voiceSelection-1);this.tone()}
  down(){if(this.screen==='home')return this.homeMove('down');if(this.screen==='menu'){this.menuSelection=(this.menuSelection+1)%menuItems.length}else if(this.screen==='changelog')this.changelogLineOffset=Math.min(Math.max(0,changelog[this.changelogSelection].lines.length-4),this.changelogLineOffset+1);else if(this.screen==='rooms')this.roomSelection=Math.min(this.rooms.length-1,this.roomSelection+1);else if(this.screen==='networks')this.networkSelection=Math.min(networks.length-1,this.networkSelection+1);else if(this.screen==='voice-messages')this.voiceSelection=Math.min(this.messages.filter(m=>m.voice).length-1,this.voiceSelection+1);this.tone()}
  right(){if(this.screen==='home')return this.homeMove('right');if(this.screen==='chat')this.switchRoom(1);else if(this.screen==='status')this.statusPage=(this.statusPage+1)%3;else if(this.screen==='changelog'){this.changelogSelection=Math.min(changelog.length-1,this.changelogSelection+1);this.changelogLineOffset=0}else if(this.screen==='networks')this.selectNetwork();else if(this.screen==='volume')this.volume=Math.min(4,this.volume+1);else if(this.screen==='language')this.cycleLanguage(1);this.tone()}
  applyRawKeys(keys){if(this.inputQuarantined)return'ignored';if(this.charging&&this.chargingDimmed){this.chargingDimmed=false;this.chargingScreenIdleMs=0;return'wake'}const action=interpretRawKeys(this.screen,keys);if(this.charging)this.chargingScreenIdleMs=0;if(action.kind==='navigation')this[action.direction]();else if(action.kind==='text')this.type(action.text,{shift:keys.shift});else if(action.kind==='enter')this.enter();return action.kind}
  physicalKey(direction,printable,{fn=false,shift=false}={}){const[x,y]=navPositions[direction];return this.applyRawKeys({fn,shift,word:printable?[printable]:[],keyList:[{x,y}]})}
  enter(){if(this.screen==='home')return this.homeOpen();
    if(this.screen==='chat'&&this.frenchDead){this.draft+=deadText[this.frenchDead];this.frenchDead=null}
    if(this.screen==='keys'){this.screen='chat';return this.tone()}
    if(this.screen==='menu')this.open(menuItems[this.menuSelection]);
    else if(this.screen==='voice-messages'){this.replayAudible=!this.replayAudible;this.walkie=this.replayAudible?'PLAYING':'STOPPED'}
    else if(this.screen==='rooms'){this.currentRoom=this.rooms[this.roomSelection];this.messages=[];this.screen='chat'}
    else if(this.screen==='charging-confirm')this.startCharging()
    else if(this.screen==='charging')this.chargingDimmed=true
    else if(this.screen==='fox-finding'&&this.foxState==='selecting')(this.foxPackInvite?this.joinPack():this.startFox())
    else if(this.screen==='language'||this.screen==='changelog'||this.screen==='status'||this.screen==='volume')this.leaveSettings()
    else if(this.screen==='networks')this.selectNetwork();
    else if(this.screen==='password'){this.network='SAVED + CONNECTED';this.ssid=this.selectedSsid;this.screen='networks'}
    else if(this.screen==='chat'&&this.draft){this.messages.push({id:this.name.toLowerCase(),author:this.name,body:this.draft,state:'queued'});this.draft=''}
    this.tone();
  }
  open(item){if(item==='BACK TO CHAT')this.screen='chat';else if(item==='SYNC NOW'){if(!this.localOnly)this.sync()}else if(item==='VOICE MESSAGES')this.screen='voice-messages';else if(item==='WALKIE-TALKIE')this.screen='walkie';else if(item==='ESP-NOW LOCAL')this.setLocalOnly(!this.localOnly);else if(item==='CHARGING MODE')this.screen='charging-confirm';else if(item==='FOX FINDING'){this.screen='fox-finding';this.foxState='selecting';if(!this.localOnly){this.foxForcedLocalOnly=true;this.setLocalOnly(true)}}else if(item==='EMOJI RECIPES')this.screen='emoji-recipes';else if(item==='NETWORKS')this.screen='networks';else this.screen=item.toLowerCase()}
  startFox(){const peer=this.foxPeers[this.foxSelection];this.foxTarget=peer.name;this.foxRssi=peer.rssi;this.foxState='requesting';this.setLocalOnly(true)}
  foxAck(){this.foxState='acquiring'}
  foxPlaceResult({similarity=76,overlap=7,confidence=3,rssi=-55,guidance='WARMER'}={}){this.foxPlaceSimilarity=similarity;this.foxPlaceOverlap=overlap;this.foxConfidence=confidence;this.foxRssi=rssi;this.foxGuidance=guidance;this.foxState='guiding'}
  foxLoseSignal(){this.foxState='signal-lost';this.foxGuidance='SIGNAL LOST'}
  offerPack(hunter='Papa',quarry='Juju',session=42){this.foxPackInvite={hunter,quarry,session};this.foxState='selecting'}
  joinPack(){this.foxTarget=this.foxPackInvite.quarry;this.foxJoinedPack=true;this.foxState='acquiring';this.setLocalOnly(true)}
  packObservation(hunter,{rssi,similarity,confidence,trend=0}){const previous=this.foxPackEvidence.find(e=>e.hunter===hunter),next={hunter,rssi,similarity,confidence,trend};if(previous)Object.assign(previous,next);else this.foxPackEvidence.push(next)}
  packLeader(){return this.foxPackEvidence.toSorted((a,b)=>(b.rssi*3+b.similarity*b.confidence/3)-(a.rssi*3+a.similarity*a.confidence/3))[0]?.hunter||''}
  startCharging(){this.charging=true;this.screen='charging';this.chargingElapsedMs=0;this.chargingScreenIdleMs=0;this.chargingStartMv=this.batteryVoltageMv;this.chargeNextSampleMs=300000;this.chargeTrend='WAITING';this.chargePoints=[{elapsed:0,mv:this.batteryVoltageMv,pct:this.batteryLevel}];this.chargeEvents=['MODE_ENTER','SAMPLE'];this.wifiEnabled=false;this.espNowEnabled=false;this.syncEnabled=false;this.audioEnabled=false;this.notifications=0;this.network='CHARGE MODE';this.ssid='';this.inputQuarantined=true}
  stopCharging(){this.chargeEvents.push('MODE_EXIT');this.charging=false;this.screen='home';this.wifiEnabled=true;this.espNowEnabled=true;this.syncEnabled=true;this.audioEnabled=true;this.network='RESUMING';this.inputQuarantined=true}
  classifyCharge(){const values=this.chargePoints.slice(-5).map(point=>point.mv).concat(this.batteryVoltageMv);if(values.length<3)return'WAITING';const delta=values.at(-1)-values[0],span=Math.max(...values)-Math.min(...values);if(delta>=12)return'RISING';if(delta<=-12)return'FALLING';if(span<=12)return'FLAT';return'UNSTABLE'}
  chargeSample(){const previous=this.chargeTrend;this.chargeTrend=this.classifyCharge();this.chargePoints.push({elapsed:this.chargingElapsedMs,mv:this.batteryVoltageMv,pct:this.batteryLevel});if(this.chargePoints.length>144)this.chargePoints.shift();this.chargeEvents.push('SAMPLE');if(this.chargeTrend!==previous&&this.chargeTrend!=='WAITING')this.chargeEvents.push(`TREND_${this.chargeTrend}`)}
  setLocalOnly(enabled){this.localOnly=enabled;if(enabled){this.ssid='';this.network='ESPNOW LOCAL';this.transport='ESP-NOW'}else{this.network='WIFI RESUME';this.transport='OFFLINE'}}
  cycleLanguage(direction){const values=['auto','en','fr'];this.languageOverride=values[(values.indexOf(this.languageOverride)+direction+3)%3];if(this.languageOverride!=='auto')this.language=this.languageOverride}
  selectNetwork(){this.selectedSsid=networks[this.networkSelection].ssid;this.password='';this.network='TYPE PASSWORD';this.screen='password'}
  // Chat in French types the CSA layout (french_keyboard.h): each character names the keycap it came from.
  type(text,{shift=false,alt=false}={}){if(this.screen!=='chat'&&this.screen!=='password')return;const target=this.screen==='chat'?'draft':'password';for(const character of text){if(target==='draft'&&this.language==='fr')this.typeFrench(character,alt);else this[target]+=character}this.tone();return shift&&text==='?'?'punctuation':'text'}
  typeFrench(character,alt){if(character===' '){if(this.frenchDead){this.draft+=frenchCompositions[this.frenchDead][' '];this.frenchDead=null}else this.draft+=' ';return}const key=frenchKeys.find(entry=>entry[2]===character||entry[3]===character);if(!key)return;const shift=key[3]===character&&key[2]!==character;const result=key[4+(alt?2:0)+(shift?1:0)];if(!result)return;if(result.dead){if(!this.frenchDead){this.frenchDead=result.dead;return}const first=this.frenchDead;this.draft+=deadText[first];this.frenchDead=first===result.dead?null:result.dead;return}if(this.frenchDead){const composed=frenchCompositions[this.frenchDead][result];this.draft+=composed??deadText[this.frenchDead]+result;this.frenchDead=null;return}this.draft+=result}
  tab(){if(this.screen==='chat'&&this.language==='fr'){this.screen='keys';this.tone()}else if(this.screen==='keys'){this.screen='chat';this.tone()}}
  backspace(){const target=this.screen==='chat'?'draft':this.screen==='password'?'password':null;if(!target)return;if(target==='draft'&&this.frenchDead)this.frenchDead=null;else this[target]=[...this[target]].slice(0,-1).join('')}
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
