const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync, spawn} = require('node:child_process');
const root = path.resolve(__dirname, '..');
const helper = path.join(root, 'plugins/usb-storage/scripts/usb-storage');
const model = require('../plugins/usb-storage/Model.js');
const D = 'org.freedesktop.UDisks2.Drive';
const B = 'org.freedesktop.UDisks2.Block';
const F = 'org.freedesktop.UDisks2.Filesystem';
const base = '/org/freedesktop/UDisks2/';
const drivePath = base + 'drives/Test_Drive';
const variant = (data) => ({data});
const props = (values) => Object.fromEntries(Object.entries(values).map(([k,v]) => [k,variant(v)]));
const bytes = (s) => [...Buffer.from(s),0];
function disk(name='Test_Drive', extra={}) {
  return {[D]: props({Id: name, TimeDetected: 123456789, Vendor:'Example', Model:'USB drive', Size:1e12,
    ConnectionBus:'usb', MediaAvailable:true, CanPowerOff:true, SiblingId:'', ...extra})};
}
function volume(name, drive=drivePath, extra={}) {
  return {[B]: props({Drive:drive, Device:bytes('/dev/'+name), IdLabel:'Photos & notes', IdType:'ext4', IdUsage:'filesystem', HintSystem:false, CryptoBackingDevice:'/', ...extra}),
    [F]: props({MountPoints:[bytes('/media/test drive')]})};
}
function state(objects) { return {type:'a{oa{sa{sv}}}',data:[objects]}; }
function standard() {
 return state({[drivePath]:disk(),[base+'block_devices/sdz1']:volume('sdz1')});
}
const fake = `#!/usr/bin/env node
const fs=require('node:fs'),path=require('node:path');
const dir=process.env.FIXTURE_DIR, file=path.join(dir,'state.json');
let state=JSON.parse(fs.readFileSync(file)), config=JSON.parse(fs.readFileSync(path.join(dir,'config.json')));
const args=process.argv.slice(2), exe=path.basename(process.argv[1]);
if(exe==='busctl') {
 const n=Number(fs.readFileSync(path.join(dir,'reads'),'utf8'))+1; fs.writeFileSync(path.join(dir,'reads'),String(n));
 if(config.busFail){process.exit(1);}
 if(config.changeOnRead===n) {state.data[0]['${drivePath}']['${D}'].TimeDetected.data++;fs.writeFileSync(file,JSON.stringify(state));}
 if(config.siblingOnRead===n) {state.data[0]['${drivePath}']['${D}'].SiblingId.data='changed';fs.writeFileSync(file,JSON.stringify(state));}
 console.log(JSON.stringify(state)); process.exit(0);
}
fs.appendFileSync(path.join(dir,'actions'),JSON.stringify(args)+'\\n');
const relative=args[args.indexOf('--object-path')+1];
if(!relative || relative.startsWith('/') || relative.includes('..')) {console.error('udisksctl expects a relative object path');process.exit(1);}
const object='${base}'+relative;
if(args[0]==='unmount') {
 if(config.busy===object){console.error('Error unmounting: target is busy');process.exit(1);}
 if(config.denied){console.error('Not authorized to perform operation');process.exit(1);}
 state.data[0][object]['${F}'].MountPoints.data=[];
 if(config.remount) state.data[0][object]['${F}'].MountPoints.data=[[47,109,101,100,105,97,0]];
}
if(args[0]==='power-off') {
 if(config.powerFail){console.error('Not authorized');process.exit(1);}
 const group=state.data[0][object]['${D}'].SiblingId.data;
 const removed=Object.keys(state.data[0]).filter(k=>k===object||(group&&state.data[0][k]['${D}']?.SiblingId.data===group));
 for(const k of Object.keys(state.data[0])) if(removed.includes(k)||removed.includes(state.data[0][k]['${B}']?.Drive.data)) delete state.data[0][k];
}
fs.writeFileSync(file,JSON.stringify(state));
console.log('Done');
`;
function fixture(t,s=standard(),config={}) {
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'usb-storage-test-'));
 fs.mkdirSync(path.join(dir,'bin'));fs.mkdirSync(path.join(dir,'runtime'),{mode:0o700});
 for(const name of ['busctl','udisksctl']) fs.writeFileSync(path.join(dir,'bin',name),fake,{mode:0o755});
 fs.writeFileSync(path.join(dir,'state.json'),JSON.stringify(s));fs.writeFileSync(path.join(dir,'config.json'),JSON.stringify(config));
 fs.writeFileSync(path.join(dir,'actions'),'');fs.writeFileSync(path.join(dir,'reads'),'0');
 t.after(()=>fs.rmSync(dir,{recursive:true,force:true}));
 const env={...process.env,FIXTURE_DIR:dir,XDG_RUNTIME_DIR:path.join(dir,'runtime'),PATH:path.join(dir,'bin')+':'+process.env.PATH};
 return {dir, env, run:(args)=>spawnSync(helper,args,{env,encoding:'utf8'}),
 actions:()=>fs.readFileSync(path.join(dir,'actions'),'utf8').trim().split('\n').filter(Boolean).map(JSON.parse)};
}
const remove=['remove',drivePath,'Test_Drive','123456789'];
test('discovers USB filesystem with spaces and preserves identity strings',t=>{
 const f=fixture(t),r=f.run(['snapshot']);assert.equal(r.status,0,r.stderr);
 const [d]=JSON.parse(r.stdout);assert.equal(d.id,'Test_Drive');assert.equal(d.attached,'123456789');assert.equal(d.reason,'');assert.equal(d.volumes[0].label,'Photos & notes');assert.equal(d.volumes[0].mounted,true);
});
test('normal removal unmounts before powering off',t=>{
 const f=fixture(t),r=f.run(remove);assert.equal(r.status,0,r.stdout+r.stderr);assert.equal(JSON.parse(r.stdout).ok,true);
 assert.deepEqual(f.actions().map(a=>a[0]),['unmount','power-off']);
 assert.equal(f.actions()[0][2],'block_devices/sdz1');assert.equal(f.actions()[1][2],'drives/Test_Drive');
});
test('all partitions unmount before power-off',t=>{
 const s=standard();s.data[0][base+'block_devices/sdz2']=volume('sdz2');const f=fixture(t,s),r=f.run(remove);
 assert.equal(r.status,0,r.stdout+r.stderr);assert.deepEqual(f.actions().map(a=>a[0]),['unmount','unmount','power-off']);
});
test('busy volume stops without power-off',t=>{
 const f=fixture(t,standard(),{busy:base+'block_devices/sdz1'}),r=f.run(remove);
 assert.equal(r.status,1);assert.match(JSON.parse(r.stdout).message,/busy/);assert.equal(f.actions().length,1);
});
test('authorization denied stops without power-off',t=>{
 const f=fixture(t,standard(),{denied:true}),r=f.run(remove);assert.equal(r.status,1);assert.match(r.stdout,/Not authorized/);assert.equal(f.actions().length,1);
});
test('unmounted volumes need no extra unmount',t=>{
 const s=standard();s.data[0][base+'block_devices/sdz1'][F].MountPoints.data=[];const f=fixture(t,s),r=f.run(remove);
 assert.equal(r.status,0,r.stdout+r.stderr);assert.deepEqual(f.actions().map(a=>a[0]),['power-off']);
});
test('attachment change before mutation refuses stale selection',t=>{
 const f=fixture(t,standard(),{changeOnRead:2}),r=f.run(remove);assert.equal(r.status,1);assert.equal(f.actions().length,0);
});
test('attachment change after unmount prevents power-off',t=>{
 const f=fixture(t,standard(),{changeOnRead:3}),r=f.run(remove);assert.equal(r.status,1);assert.deepEqual(f.actions().map(a=>a[0]),['unmount']);
});
test('remounted filesystem prevents power-off',t=>{
 const f=fixture(t,standard(),{remount:true}),r=f.run(remove);assert.equal(r.status,1);assert.match(r.stdout,/still mounted/);assert.deepEqual(f.actions().map(a=>a[0]),['unmount']);
});
test('internal and system-mounted USB drives are excluded',t=>{
 const s=standard();s.data[0][drivePath][D].ConnectionBus.data='';assert.deepEqual(JSON.parse(fixture(t,s).run(['snapshot']).stdout),[]);
 const u=standard();u.data[0][base+'block_devices/sdz1'][F].MountPoints.data=[bytes('/')];const f=fixture(t,u);assert.deepEqual(JSON.parse(f.run(['snapshot']).stdout),[]);assert.equal(f.run(remove).status,1);assert.equal(f.actions().length,0);
});
test('encrypted and unsupported storage is not mutated',t=>{
 for(const mode of ['crypto','raid']){
  const s=standard();if(mode==='crypto')s.data[0][base+'block_devices/sdz1'][B].CryptoBackingDevice.data=base+'block_devices/sdz';else s.data[0][base+'block_devices/sdz1'][B].IdUsage.data='raid';
  const f=fixture(t,s);assert.notEqual(JSON.parse(f.run(['snapshot']).stdout)[0].reason,'');assert.equal(f.run(remove).status,1);assert.equal(f.actions().length,0);
 }
});
test('devices lacking power-off support are disabled',t=>{
 const s=standard();s.data[0][drivePath][D].CanPowerOff.data=false;const f=fixture(t,s);assert.equal(f.run(remove).status,1);assert.equal(f.actions().length,0);
});
test('linked drives are one group and all volumes are unmounted',t=>{
 const s=standard();s.data[0][drivePath][D].SiblingId.data='bridge';const other=base+'drives/Other';s.data[0][other]=disk('Other',{SiblingId:'bridge'});s.data[0][base+'block_devices/sdy1']=volume('sdy1',other);
 const f=fixture(t,s),list=JSON.parse(f.run(['snapshot']).stdout);assert.equal(list.length,1);assert.equal(list[0].siblingCount,2);
 const d=list[0],r=f.run(['remove',d.path,d.id,d.attached]);assert.equal(r.status,0,r.stdout+r.stderr);assert.deepEqual(f.actions().map(a=>a[0]),['unmount','unmount','power-off']);
});
test('protected sibling blocks the whole group',t=>{
 const s=standard();s.data[0][drivePath][D].SiblingId.data='bridge';const other=base+'drives/Other';s.data[0][other]=disk('Other',{SiblingId:'bridge'});s.data[0][base+'block_devices/sdy1']=volume('sdy1',other,{HintSystem:true});
 const f=fixture(t,s);assert.deepEqual(JSON.parse(f.run(['snapshot']).stdout),[]);assert.equal(f.run(remove).status,1);assert.equal(f.actions().length,0);
});
test('power-off error never claims success',t=>{
 const f=fixture(t,standard(),{powerFail:true}),r=f.run(remove);assert.equal(r.status,1);assert.equal(JSON.parse(r.stdout).ok,false);
});
test('unavailable UDisks fails without storage actions',t=>{
 const f=fixture(t,standard(),{busFail:true});assert.notEqual(f.run(['snapshot']).status,0);assert.equal(f.run(remove).status,1);assert.equal(f.actions().length,0);
});
test('missing or changed drive identity is refused',t=>{
 const f=fixture(t);assert.equal(f.run(['remove',drivePath,'Wrong','123456789']).status,1);assert.equal(f.actions().length,0);
});
test('concurrent removal is refused by runtime lock',async t=>{
 const f=fixture(t);const lock=path.join(f.env.XDG_RUNTIME_DIR,'grankko-usb-storage.lock');
 const child=spawn('flock',[lock,'sh','-c','echo locked; sleep 2']);t.after(()=>child.kill());
 await new Promise(resolve=>child.stdout.once('data',resolve));
 const r=f.run(remove);assert.equal(r.status,1);assert.match(r.stdout,/already in progress/);assert.equal(f.actions().length,0);
});
test('plain-text labels and capacity formatting',()=>{
 assert.equal(model.capacity(1024**4),'1.0 TiB');assert.equal(model.title({volumes:[{label:'<Photos>'}]}),'<Photos>');
 assert.equal(model.notifyText('<Photos> & notes'),'&lt;Photos&gt; &amp; notes');assert.match(model.detail({size:1024,volumes:[{mounted:true}],siblingCount:2}),/linked drives/);
});

test('physical USB grouping change prevents mutation',t=>{
 const f=fixture(t,standard(),{siblingOnRead:2}),r=f.run(remove);
 assert.equal(r.status,1);assert.match(r.stdout,/storage changed/);assert.equal(f.actions().length,0);
});
test('USB HDD is included even when media is not removable',t=>{
 const s=standard();s.data[0][drivePath][D].MediaRemovable=variant(false);
 const f=fixture(t,s);assert.equal(JSON.parse(f.run(['snapshot']).stdout).length,1);
});
test('missing drive and empty bus are handled safely',t=>{
 const f=fixture(t,state({}));assert.deepEqual(JSON.parse(f.run(['snapshot']).stdout),[]);
 assert.equal(f.run(remove).status,1);assert.equal(f.actions().length,0);
});
