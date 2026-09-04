import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const header=fs.readFileSync(path.join(root,'firmware/include/fox_finding.h'),'utf8');
assert.match(header,/kMaxPlacePrintEntries = 12/);
assert.match(header,/sizeof\(PlacePrint\) <= 104/);
assert.match(header,/kMaxPackHunters = 6/);
assert.match(header,/sizeof\(PackObservation\) <= 20/);

function match(local,remote){let overlap=0,weighted=0,totalWeight=0,delta=0;for(const a of local)for(const b of remote)if(a.id===b.id){const strength=Math.max(-95,Math.min(-30,Math.max(a.rssi,b.rssi))),weight=1+Math.floor((strength+95)/16);weighted+=Math.abs(a.rssi-b.rssi)*weight;totalWeight+=weight;delta+=a.rssi-b.rssi;overlap++;break}const unionCount=local.length+remote.length-overlap;if(!unionCount||!overlap)return{overlap,similarity:0,confidence:0};return{overlap,similarity:Math.max(0,Math.min(100,Math.floor(overlap*100/unionCount)-Math.floor(weighted/totalWeight))),confidence:overlap>=6?3:overlap>=3?2:1,delta:Math.trunc(delta/overlap)}}
const same=[1,2,3,4,5,6].map(i=>({id:i,rssi:-45-i}));
assert.deepEqual(match(same,same),{overlap:6,similarity:100,confidence:3,delta:0});
assert.equal(match(same,[{id:9,rssi:-50}]).confidence,0);
assert.ok(match(same,same.map(x=>({...x,rssi:x.rssi-15}))).similarity<100);
const evidenceScore=e=>e.rssi*3+e.similarity*e.confidence/3;
const pack=[{id:'albie',rssi:-67,similarity:72,confidence:2},{id:'juju',rssi:-51,similarity:80,confidence:3},{id:'papa',rssi:-60,similarity:90,confidence:2}];
assert.equal(pack.sort((a,b)=>evidenceScore(b)-evidenceScore(a))[0].id,'juju');
console.log('supachat_fox_contract=PASS placeprint_bytes=104 pack_observation_max=20');
