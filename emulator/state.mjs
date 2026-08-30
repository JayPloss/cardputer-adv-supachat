export const menuItems = ['BACK TO CHAT','ROOMS','SYNC NOW','VOICE MESSAGES','VOLUME','LANGUAGE','NETWORKS','STATUS'];
export const networks = [
  {ssid:'Plossco Family',rssi:-41,open:false},{ssid:'Papa Hotspot',rssi:-57,open:false},
  {ssid:'Library Guest',rssi:-68,open:true},{ssid:'NETGEAR-2G',rssi:-76,open:false}
];
export class SupaChatState {
  constructor(name='Albie',language='en') {
    Object.assign(this,{name,screen:'chat',menuSelection:0,networkSelection:0,network:'SYNCED',ssid:'Plossco Family',
      walkie:'READY',transport:'HETZNER',recording:false,spaceHeld:false,recordedMs:0,clipReady:false,clipPending:false,
      replayAudible:false,voiceSelection:0,volume:3,draft:'',password:'',selectedSsid:'',tones:0,notifications:0,http:200,heap:181432,
      batteryLevel:73,batteryVoltageMv:3986,externalPowerDetected:true,roomSelection:0,currentRoom:'Family',rooms:['Family','K-BUDS','Sunday Crew'],roomNew:[false,true,false],
      easternTime:'3:42P',language,languageOverride:'auto',gravePending:false,
      bootElapsedMs:0,bootSkipped:false,bootActive:true,
      messages:[{id:'papa',messageId:41,author:'Papa',body:'Hey Albie — testing SupaChat.',state:'read'},{id:'juju',messageId:42,author:'Julien',body:'I found the orange channel.',state:'saved'},{id:'papa',messageId:43,author:'Papa',body:'',voice:true,state:'saved'},{id:'albie',messageId:44,author:name,body:'I am online!',state:'saved'}]});
  }
  tone(){if(this.volume>0)this.tones++}
  bootTick(ms){if(!this.bootSkipped)this.bootElapsedMs+=ms}
  skipBoot(){this.bootSkipped=true;this.bootActive=false}
  receive(author,body,id=author.toLowerCase(),room=this.currentRoom){if(room!==this.currentRoom)return false;this.messages.push({id,author,body,state:'saved'});if(author!==this.name&&this.volume>0)this.notifications++;return true}
  menu(){this.screen=this.screen==='menu'?'chat':'menu';this.tone()}
  switchRoom(direction){this.roomSelection=(this.roomSelection+direction+this.rooms.length)%this.rooms.length;this.currentRoom=this.rooms[this.roomSelection];this.roomNew[this.roomSelection]=false;this.messages=[]}
  left(){if(this.screen==='chat')this.switchRoom(-1);else if(this.screen==='menu')this.screen='chat';else if(this.screen==='language')this.cycleLanguage(-1);else this.screen='menu';this.tone()}
  up(){if(this.screen==='menu')this.menuSelection=(this.menuSelection+7)%8;else if(this.screen==='rooms')this.roomSelection=Math.max(0,this.roomSelection-1);else if(this.screen==='networks')this.networkSelection=Math.max(0,this.networkSelection-1);else if(this.screen==='walkie')this.voiceSelection=Math.max(0,this.voiceSelection-1);this.tone()}
  down(){if(this.screen==='menu')this.menuSelection=(this.menuSelection+1)%8;else if(this.screen==='rooms')this.roomSelection=Math.min(this.rooms.length-1,this.roomSelection+1);else if(this.screen==='networks')this.networkSelection=Math.min(networks.length-1,this.networkSelection+1);else if(this.screen==='walkie')this.voiceSelection=Math.min(this.messages.filter(m=>m.voice).length-1,this.voiceSelection+1);this.tone()}
  right(){if(this.screen==='chat')this.switchRoom(1);else if(this.screen==='menu')this.open(menuItems[this.menuSelection]);else if(this.screen==='networks')this.selectNetwork();else if(this.screen==='volume')this.volume=Math.min(4,this.volume+1);else if(this.screen==='language')this.cycleLanguage(1);this.tone()}
  physicalKey(direction,printable,{fn=false}={}){const textEntry=this.screen==='chat'||this.screen==='password';if((textEntry&&fn)||(!textEntry&&!fn)){this[direction]?.();return'navigation'}if(textEntry){this.type(printable);return'text'}return'ignored'}
  enter(){
    if(this.gravePending&&(this.screen==='chat'||this.screen==='password')){this[this.screen==='chat'?'draft':'password']+="'";this.gravePending=false}
    if(this.screen==='menu')this.open(menuItems[this.menuSelection]);
    else if(this.screen==='walkie'){this.replayAudible=!this.replayAudible;this.walkie=this.replayAudible?'PLAYING':'STOPPED'}
    else if(this.screen==='rooms'){this.currentRoom=this.rooms[this.roomSelection];this.messages=[];this.screen='chat'}
    else if(this.screen==='language')this.screen='menu'
    else if(this.screen==='networks')this.selectNetwork();
    else if(this.screen==='password'){this.network='SAVED + CONNECTED';this.ssid=this.selectedSsid;this.screen='networks'}
    else if(this.screen==='chat'&&this.draft){this.messages.push({id:this.name.toLowerCase(),author:this.name,body:this.draft,state:'queued'});this.draft=''}
    this.tone();
  }
  open(item){if(item==='BACK TO CHAT')this.screen='chat';else if(item==='SYNC NOW')this.sync();else if(item==='VOICE MESSAGES')this.screen='walkie';else this.screen=item.toLowerCase()}
  cycleLanguage(direction){const values=['auto','en','fr'];this.languageOverride=values[(values.indexOf(this.languageOverride)+direction+3)%3];if(this.languageOverride!=='auto')this.language=this.languageOverride}
  selectNetwork(){this.selectedSsid=networks[this.networkSelection].ssid;this.password='';this.network='TYPE PASSWORD';this.screen='password'}
  type(text,{shift=false}={}){if(this.screen!=='chat'&&this.screen!=='password')return;const target=this.screen==='chat'?'draft':'password';for(const character of text){if(this.language==='fr'&&this.gravePending){this.gravePending=false;if(character==='a'){this[target]+='à';continue}if(character==='e'){this[target]+='è';continue}this[target]+="'"}if(this.language==='fr'&&character==="'"){this.gravePending=true;continue}this[target]+=this.language==='fr'&&character==='?'?'é':character}this.tone();return shift&&text==='?'?'punctuation':'text'}
  backspace(){const target=this.screen==='chat'?'draft':this.screen==='password'?'password':null;if(!target)return;if(this.gravePending)this.gravePending=false;else this[target]=[...this[target]].slice(0,-1).join('')}
  holdSpace(){if(this.screen!=='walkie'||this.spaceHeld)return;this.spaceHeld=true;this.recording=true;this.recordedMs=0;this.walkie='RECORDING'}
  releaseSpace(){this.spaceHeld=false;if(!this.recording)return;this.recording=false;this.clipReady=this.recordedMs>=40;this.clipPending=this.clipReady;this.walkie=this.clipReady?'RECORDED - ENTER PLAYS':'READY'}
  tick(ms){if(!this.recording)return;this.recordedMs+=ms;if(this.recordedMs>=30000)this.releaseSpace()}
  sync(result='ok'){if(result==='io'){this.network='SYNC IO -1';this.transport='OFFLINE';this.http=-1}else if(result==='auth'){this.network='SYNC HTTP 401';this.transport='OFFLINE';this.http=401}else{this.network='SYNCED';this.transport='HETZNER';this.http=200;this.screen='chat'}}
}
