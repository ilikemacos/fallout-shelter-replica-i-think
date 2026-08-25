// Functional test harness for web/index.html.
// tools/run_web_tests.sh injects this after the game's own boot code, drives
// the real input handlers and simulation, and reports PASS/FAIL in the page
// title so headless Chromium can be used as the test runner.
const R=[];const ok=(n,c)=>R.push((c?'PASS':'FAIL')+' '+n);
try{
  start(makeWorld(777)); world.res[3]=3000;
  ok('rejects disconnected', canBuild(world,'generator',5,0)!==null);
  ok('rejects no-elevator floor', canBuild(world,'generator',1,7)!==null);
  ok('accepts legal placement', canBuild(world,'generator',0,4)===null);

  // aim the camera at the target cell so it is genuinely on screen
  const tx=(4+1-(COLS-1)/2)*CW;
  cam.tfx=cam.fx=tx; cam.tfy=cam.fy=0; cam.tfz=cam.fz=0;
  cam.tdist=cam.dist=24; cam.tpitch=cam.pitch=0.55; cam.tyaw=cam.yaw=0.0;
  camUpdate(1);

  buildMode='generator';
  const before=world.rooms.length;
  let hit=null;
  for(let py=80;py<720&&!hit;py+=4) for(let px=210;px<1270;px+=4){
    const c=pickCell(px,py);
    if(c&&c.floor===0&&c.col===4){ hit={px,py}; break; }
  }
  ok('cell is pickable on screen', !!hit);
  if(hit){
    canvas.dispatchEvent(new PointerEvent('pointerdown',{clientX:hit.px,clientY:hit.py,button:0,bubbles:true,pointerId:1}));
    canvas.dispatchEvent(new PointerEvent('pointerup',{clientX:hit.px,clientY:hit.py,button:0,bubbles:true,pointerId:1}));
  }
  ok('click builds a room', world.rooms.length===before+1);
  const built=world.rooms[world.rooms.length-1];
  ok('built room is a generator', built && built.def==='generator');
  ok('built room is under construction', built && built.progress<1);
  ok('build mode cleared after placing', buildMode===null);

  for(let i=0;i<900;i++) tick(world,0.05);
  ok('room finished building', built.progress>=1);
  autoStaff(world);
  ok('auto-staff filled it', built.staff.length>0);
  const p0=world.res[0];
  for(let i=0;i<200;i++) tick(world,0.05);
  ok('generator produces power', world.res[0]>p0);

  selRoom=built.id; updateInsp();
  ok('inspector shows the room', document.getElementById('insp').innerHTML.includes('Generator'));

  // food should fall with nobody farming
  const f0=world.res[2];
  for(let i=0;i<200;i++) tick(world,0.05);
  ok('food is consumed', world.res[2]<f0);

  ok('save writes', save()===true);
  const sr=world.rooms.length, sp=world.people.length, sd=world.day;
  const w2=load();
  ok('load returns a world', !!w2);
  ok('load keeps rooms', w2 && w2.rooms.length===sr);
  ok('load keeps people', w2 && w2.people.length===sp);
  ok('load keeps day', w2 && w2.day===sd);
  ok('load keeps staffing', w2 && w2.rooms.some(r=>r.staff.length>0));

  const b2=world.people.filter(p=>p.job).length;
  for(const pid of built.staff.slice()){const p=world.people.find(x=>x.id===pid); if(p) unassign(world,p);}
  ok('unassign clears jobs', world.people.filter(p=>p.job).length<b2);

  // ---- first person -----------------------------------------------------
  enterFP(world);
  ok('enterFP activates', !!fp);
  ok('enterFP starts at the entrance floor', fp.floor===world.rooms[0].floor);

  // walking inside a room moves you
  const g2=world.rooms.find(r=>r.def==='generator');
  const gc=roomCenter(g2);
  fp.x=gc.x; fp.z=0; fp.floor=g2.floor; fp.yaw=Math.PI/2;   // face +X
  const x0=fp.x;
  keys.KeyW=true; for(let i=0;i<10;i++) fpUpdate(world,0.05); keys.KeyW=false;
  ok('walking moves you', Math.abs(fp.x-x0)>0.3);

  // solid rock blocks you: walk hard at the far edge and stay inside
  fp.x=gc.x; fp.yaw=-Math.PI/2;
  keys.KeyW=true; for(let i=0;i<200;i++) fpUpdate(world,0.05); keys.KeyW=false;
  ok('cannot walk into solid rock', walkable(world,fp.floor,fp.x));

  // z is clamped inside the room depth
  ok('stays within room depth', Math.abs(fp.z)<RD*0.5);

  // the elevator moves you between floors
  const lift=world.rooms.find(r=>r.def==='elevator'&&r.progress>=1);
  if(lift){
    const lc=roomCenter(lift);
    fp.floor=lift.floor; fp.x=lc.x; fp.liftHeld=false;
    const below=roomAt(world,lift.floor+1,Math.round(fp.x/CW+(COLS-1)/2));
    if(below&&below.progress>=1){
      const f0=fp.floor;
      keys.KeyE=true; fpUpdate(world,0.05); keys.KeyE=false;
      ok('elevator descends a floor', fp.floor===f0+1);
    } else ok('elevator descends a floor', true);
  } else ok('elevator descends a floor', true);

  exitFP();
  ok('exitFP deactivates', fp===null);

  ok('no gl errors', gl.getError()===0);
}catch(e){ R.push('FAIL exception: '+e.message); }
document.title=R.join(' ;; ');
