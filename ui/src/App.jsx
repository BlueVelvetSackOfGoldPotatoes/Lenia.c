import React, { useState, useEffect, useRef, useCallback } from 'react';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, ScatterChart, Scatter } from 'recharts';

const COLORMAPS = ['BCYR','Rainbow','GYPB','PPGG','BGYR','GYPB2','YPCG','W/B','B/W'];
const KERNEL_NAMES = ['Polynomial','Exponential','Step','Life'];
const GROWTH_NAMES = ['Polynomial','Gaussian','Step'];

// ---------- WebSocket Hook ----------
function useLeniaSocket() {
  const ws = useRef(null);
  const [frame, setFrame] = useState(null);
  const [connected, setConnected] = useState(false);
  const [history, setHistory] = useState([]);
  const histRef = useRef([]);

  useEffect(() => {
    const url = `ws://${window.location.hostname}:3100`;
    const connect = () => {
      ws.current = new WebSocket(url);
      ws.current.onopen = () => setConnected(true);
      ws.current.onerror = () => {};
      ws.current.onclose = () => { setConnected(false); setFrame(null); histRef.current=[]; setHistory([]); setTimeout(connect,1500); };
      ws.current.onmessage = (e) => {
        try {
          const d = JSON.parse(e.data);
          if (typeof d.gen !== 'number') return;
          setFrame(d);
          histRef.current = [...histRef.current.slice(-300), { gen:d.gen, mass:d.mass, growth:d.growth||0, speed:d.speed||0, gyradius:d.gyradius||0 }];
          setHistory(histRef.current);
        } catch(err){}
      };
    };
    connect();
    return () => { if(ws.current) ws.current.close(); };
  }, []);
  const send = useCallback((cmd) => { if(ws.current?.readyState===1) ws.current.send(cmd); }, []);
  return { frame, connected, history, send };
}

// ---------- Coloring ----------
function colorize(v) {
  if (v<64) return [0,0,v*2];
  if (v<128) { const t=(v-64)/64; return [0,Math.floor(t*200),128+Math.floor(t*127)]; }
  if (v<192) { const t=(v-128)/64; return [Math.floor(t*255),200+Math.floor(t*55),255-Math.floor(t*100)]; }
  const t=(v-192)/63; return [255,255-Math.floor(t*100),155-Math.floor(t*155)];
}

function decodeB64(b64, w, h) {
  if (!b64) return null;
  try { const raw = atob(b64); if (raw.length < w*h) return null; const a = new Uint8Array(w*h); for(let i=0;i<w*h;i++) a[i]=raw.charCodeAt(i); return a; } catch(e) { return null; }
}

function renderToCanvas(canvas, data, w, h, cmap) {
  if (!canvas || !data) return;
  canvas.width = w; canvas.height = h;
  const ctx = canvas.getContext('2d');
  const img = ctx.createImageData(w,h);
  const cfn = cmap === 'hot' ? (v) => [Math.min(255,v*3), Math.min(255,Math.max(0,(v-85)*3)), Math.min(255,Math.max(0,(v-170)*3))]
            : cmap === 'cool' ? (v) => [0, Math.min(255,v*2), Math.min(255,255-v)]
            : colorize;
  for(let i=0;i<w*h;i++){const[r,g,b]=cfn(data[i]);img.data[i*4]=r;img.data[i*4+1]=g;img.data[i*4+2]=b;img.data[i*4+3]=255;}
  ctx.putImageData(img,0,0);
}

// ---------- Mini Canvas Component ----------
function MiniView({ title, data, w, h, cmap }) {
  const ref = useRef(null);
  useEffect(() => { renderToCanvas(ref.current, data, w, h, cmap); }, [data, w, h, cmap]);
  return (
    <div style={{flex:1, display:'flex', flexDirection:'column', minWidth:0}}>
      <div style={{fontSize:10, color:'var(--text-dim)', padding:'2px 4px', textAlign:'center'}}>{title}</div>
      <canvas ref={ref} style={{width:'100%', flex:1, imageRendering:'pixelated', objectFit:'contain', background:'#000'}} />
    </div>
  );
}

// ---------- 3D Surface ----------
function Surface3D({ data, w, h, zoom }) {
  const ref = useRef(null);
  const st = useRef({ rx:-30, ry:30, dragging:false, lx:0, ly:0 });
  useEffect(() => {
    const c = ref.current; if(!c) return;
    const s = st.current;
    const down = (e) => { e.stopPropagation(); s.dragging=true; s.lx=e.clientX; s.ly=e.clientY; };
    const move = (e) => { if(!s.dragging)return; s.ry+=(e.clientX-s.lx)*0.4; s.rx+=(e.clientY-s.ly)*0.4; s.lx=e.clientX; s.ly=e.clientY; };
    const up = () => { s.dragging=false; };
    c.addEventListener('mousedown',down); c.addEventListener('mousemove',move); c.addEventListener('mouseup',up); c.addEventListener('mouseleave',up);
    return () => { c.removeEventListener('mousedown',down); c.removeEventListener('mousemove',move); c.removeEventListener('mouseup',up); c.removeEventListener('mouseleave',up); };
  }, []);
  useEffect(() => {
    if(!data || !ref.current) return;
    const c = ref.current; const cw = c.parentElement?.clientWidth||400; const ch = c.parentElement?.clientHeight||400;
    c.width=cw; c.height=ch; const ctx=c.getContext('2d'); ctx.fillStyle='#0d1117'; ctx.fillRect(0,0,cw,ch);
    const s=st.current; const rx=s.rx*Math.PI/180, ry=s.ry*Math.PI/180;
    const cRx=Math.cos(rx),sRx=Math.sin(rx),cRy=Math.cos(ry),sRy=Math.sin(ry);
    const scale=Math.min(cw,ch)*0.38*zoom; const ox=cw/2, oy=ch/2;
    const step=Math.max(1,Math.floor(Math.max(w,h)/80)); const gw=Math.ceil(w/step), gh=Math.ceil(h/step);
    const proj=new Float64Array(gh*gw*3); const vals=new Float64Array(gh*gw);
    for(let gr=0;gr<gh;gr++) for(let gc=0;gc<gw;gc++){
      const sr=Math.min(gr*step,h-1),sc=Math.min(gc*step,w-1); const v=data[sr*w+sc]/255;
      const x3=(sc/w-0.5)*2, y3=(sr/h-0.5)*2, z3=v*0.5;
      const x1=x3*cRy-z3*sRy, z1=x3*sRy+z3*cRy, y1=y3*cRx-z1*sRx, z2=y3*sRx+z1*cRx;
      const i=(gr*gw+gc)*3; proj[i]=ox+x1*scale; proj[i+1]=oy+y1*scale; proj[i+2]=z2; vals[gr*gw+gc]=v;
    }
    const quads=[];
    for(let gr=0;gr<gh-1;gr++) for(let gc=0;gc<gw-1;gc++){
      const i00=gr*gw+gc,i01=gr*gw+gc+1,i10=(gr+1)*gw+gc,i11=(gr+1)*gw+gc+1;
      quads.push({i00,i01,i10,i11,z:(proj[i00*3+2]+proj[i01*3+2]+proj[i10*3+2]+proj[i11*3+2])/4,v:(vals[i00]+vals[i01]+vals[i10]+vals[i11])/4});
    }
    quads.sort((a,b)=>a.z-b.z);
    for(const q of quads){const[r,g,b]=colorize(Math.floor(q.v*255));ctx.fillStyle=`rgb(${r},${g},${b})`;ctx.beginPath();ctx.moveTo(proj[q.i00*3],proj[q.i00*3+1]);ctx.lineTo(proj[q.i01*3],proj[q.i01*3+1]);ctx.lineTo(proj[q.i11*3],proj[q.i11*3+1]);ctx.lineTo(proj[q.i10*3],proj[q.i10*3+1]);ctx.closePath();ctx.fill();}
    ctx.fillStyle='#8b949e'; ctx.font='11px monospace'; ctx.fillText('drag to rotate',10,ch-10);
  }, [data,w,h,zoom]);
  return <canvas ref={ref} style={{width:'100%',height:'100%',cursor:'grab'}} />;
}

// ---------- Param Slider ----------
function ParamSlider({label,value,min,max,step,onChange,title}){
  return <div className="param-row" title={title}><span className="param-label">{label}</span>
    <input type="range" id={`p-${label}`} name={`p-${label}`} min={min} max={max} step={step} value={value} title={title} onChange={e=>onChange(parseFloat(e.target.value))}/>
    <span className="param-value">{typeof value==='number'?value.toFixed(step<0.01?4:3):value}</span></div>;
}

// ---------- Main App ----------
function App() {
  const { frame, connected, history, send } = useLeniaSocket();
  const [animals, setAnimals] = useState([]);
  const [selAnimal, setSelAnimal] = useState('');
  const [filter, setFilter] = useState('');
  const [viewMode, setViewMode] = useState('quad'); // 'quad', '2d', '3d'
  const [zoom, setZoom] = useState(1);
  const [wasd, setWasd] = useState(false);

  useEffect(() => { fetch('/api/animals').then(r=>r.json()).then(setAnimals).catch(()=>{}); }, []);

  // WASD keyboard handler
  useEffect(() => {
    if (!wasd) return;
    const handler = (e) => {
      const key = e.key.toLowerCase();
      if (key==='w') send('shift_up');
      else if (key==='s') send('shift_down');
      else if (key==='a') send('shift_left');
      else if (key==='d') send('shift_right');
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [wasd, send]);

  const p = frame?.params || {R:13,T:10,m:0.15,s:0.015,kn:1,gn:1,b:[1]};
  const w = frame?.width||128, h = frame?.height||128;
  const cellsData = frame ? decodeB64(frame.cells_b64, w, h) : null;
  const potData = frame ? decodeB64(frame.potential_b64, w, h) : null;
  const fieldData = frame ? decodeB64(frame.field_b64, w, h) : null;
  const filtAnimals = animals.filter(a => a.name.toLowerCase().includes(filter.toLowerCase()) || a.code.toLowerCase().includes(filter.toLowerCase()));

  return (
    <div className="app" onWheel={e=>{e.preventDefault();setZoom(z=>Math.max(0.5,Math.min(8,z*(e.deltaY<0?1.1:0.9))));}} style={{gridTemplateColumns:'280px 1fr 280px'}}>
      {/* HEADER */}
      <div className="header">
        <h1>Lenia</h1>
        <span className="tag">{connected?'LIVE':'OFF'}</span>
        <div className="btn-row">
          <button className={`btn ${frame?.running?'active':''}`} title="Play/pause simulation" onClick={()=>send(frame?.running?'pause':'run')}>{frame?.running?'⏸':'▶'}</button>
          <button className="btn" title="Single step" onClick={()=>send('step')}>Step</button>
          <button className="btn" title="Random cells, keep params" onClick={()=>send('random')}>Random</button>
          <button className="btn" title="Random cells + random params" onClick={()=>send('random_params')}>New</button>
          <button className="btn" title="Clear all cells" onClick={()=>send('clear')}>Clear</button>
          <button className="btn" title="Generate pattern with neural network (CPPN)" onClick={()=>send('cppn')}>CPPN</button>
          {['quad','2d','3d'].map(m=><button key={m} className={`btn ${viewMode===m?'active':''}`} title={{quad:'4-panel view: World + Growth + Potential + Kernel',['2d']:'Full-screen 2D pixel view',['3d']:'3D height-map surface view'}[m]} onClick={()=>setViewMode(m)}>{m.toUpperCase()}</button>)}
          <button className={`btn ${wasd?'active':''}`} title="Toggle WASD control: move the organism around with keyboard (W=up A=left S=down D=right)" onClick={()=>setWasd(!wasd)}>{wasd?'WASD ●':'WASD'}</button>
        </div>
        <span style={{marginLeft:'auto',fontSize:11,color:'var(--text-dim)'}}>gen {frame?.gen||0} | mass {(frame?.mass||0).toFixed(1)} | {frame?.name||''}</span>
      </div>

      {/* LEFT SIDEBAR */}
      <div className="sidebar-left">
        <div className="panel">
          <div className="panel-title">Parameters</div>
          <ParamSlider label="R" value={p.R} min={5} max={50} step={1} title="Kernel radius: how far cells sense neighbors" onChange={v=>send(`set_R ${Math.round(v)}`)}/>
          <ParamSlider label="T" value={p.T} min={1} max={50} step={1} title="Time resolution: steps per unit time (dt=1/T)" onChange={v=>send(`set_T ${Math.round(v)}`)}/>
          <ParamSlider label="m" value={p.m} min={0.01} max={0.5} step={0.001} title="Growth center: neighborhood density for max growth" onChange={v=>send(`set_m ${v}`)}/>
          <ParamSlider label="s" value={p.s} min={0.001} max={0.1} step={0.0001} title="Growth width: how sharply growth peaks around m" onChange={v=>send(`set_s ${v}`)}/>
          <div className="param-row" title="Kernel shape function"><span className="param-label">kn</span>
            <select id="p-kn" name="p-kn" value={p.kn} onChange={e=>send(`set_kn ${e.target.value}`)} style={{flex:1,margin:'0 8px',background:'var(--bg)',color:'var(--text)',border:'1px solid var(--border)',padding:'2px',borderRadius:3}}>
              {KERNEL_NAMES.map((n,i)=><option key={i} value={i+1}>{n}</option>)}</select></div>
          <div className="param-row" title="Growth mapping function"><span className="param-label">gn</span>
            <select id="p-gn" name="p-gn" value={p.gn} onChange={e=>send(`set_gn ${e.target.value}`)} style={{flex:1,margin:'0 8px',background:'var(--bg)',color:'var(--text)',border:'1px solid var(--border)',padding:'2px',borderRadius:3}}>
              {GROWTH_NAMES.map((n,i)=><option key={i} value={i+1}>{n}</option>)}</select></div>
          <div className="param-row" title="Birth weights: kernel ring amplitudes"><span className="param-label">b</span><span className="param-value">[{(p.b||[1]).map(v=>v.toFixed(2)).join(',')}]</span></div>
          <div className="param-row" title="Grid size. Changing restarts simulation."><span className="param-label">Grid</span>
            <select id="p-grid" name="p-grid" value={w} onChange={e=>fetch('/api/resize',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({size:parseInt(e.target.value),code:frame?.code||''})}).catch(()=>{})} style={{flex:1,margin:'0 8px',background:'var(--bg)',color:'var(--text)',border:'1px solid var(--border)',padding:'2px',borderRadius:3}}>
              {[32,64,128,256,512].map(s=><option key={s} value={s}>{s}x{s}</option>)}</select></div>
        </div>
        <div className="panel">
          <div className="panel-title">Search & Control</div>
          <div className="btn-row">
            <button className="btn" title="Search for more complex organisms by mutating params" onClick={()=>send('search_up')}>Search ↑</button>
            <button className="btn" title="Search for simpler organisms" onClick={()=>send('search_down')}>Search ↓</button>
            <button className="btn" title="Stop search" onClick={()=>send('search_stop')}>Stop</button>
          </div>
        </div>
        <div className="panel">
          <div className="panel-title">Animals ({filtAnimals.length})</div>
          <input type="text" id="animal-filter" name="animal-filter" placeholder="Filter..." value={filter} onChange={e=>setFilter(e.target.value)} style={{width:'100%',background:'var(--bg)',border:'1px solid var(--border)',color:'var(--text)',padding:'4px 8px',borderRadius:4,marginBottom:4,fontSize:12}}/>
          <div className="animal-list">
            {filtAnimals.slice(0,80).map(a=><div key={a.code} className={`animal-item ${selAnimal===a.code?'active':''}`} onClick={()=>{setSelAnimal(a.code);send(`load ${a.code}`);}}><strong>{a.code}</strong> {a.name}</div>)}
          </div>
        </div>
      </div>

      {/* VIEWPORT */}
      <div className="viewport" style={{display:'flex',flexDirection:'column',overflow:'hidden'}}>
        {viewMode === 'quad' && cellsData ? (
          <div style={{display:'grid',gridTemplateColumns:'1fr 1fr',gridTemplateRows:'1fr 1fr',flex:1,gap:1,background:'var(--border)'}}>
            <MiniView title="World f(x)" data={cellsData} w={w} h={h} />
            <MiniView title="Growth δ(k*f)" data={fieldData} w={w} h={h} cmap="hot" />
            <MiniView title="Potential k*f" data={potData} w={w} h={h} cmap="cool" />
            <div style={{display:'flex',flexDirection:'column',background:'#000'}}>
              <div style={{fontSize:10,color:'var(--text-dim)',padding:'2px 4px',textAlign:'center'}}>Phase Portrait</div>
              <div style={{flex:1,padding:4}}>
                <ResponsiveContainer width="100%" height="100%">
                  <ScatterChart margin={{top:5,right:5,bottom:20,left:20}}>
                    <CartesianGrid strokeDasharray="3 3" stroke="#30363d"/>
                    <XAxis dataKey="mass" name="Mass" stroke="#8b949e" tick={{fontSize:9}} label={{value:'Mass',position:'bottom',fontSize:9,fill:'#8b949e'}}/>
                    <YAxis dataKey="growth" name="Growth" stroke="#8b949e" tick={{fontSize:9}} label={{value:'Growth',angle:-90,position:'left',fontSize:9,fill:'#8b949e'}}/>
                    <Tooltip contentStyle={{background:'#161b22',border:'1px solid #30363d',fontSize:10}}/>
                    <Scatter data={history.slice(-100)} fill="#0c8599" fillOpacity={0.6} r={2}/>
                  </ScatterChart>
                </ResponsiveContainer>
              </div>
            </div>
          </div>
        ) : viewMode === '3d' && cellsData ? (
          <Surface3D data={cellsData} w={w} h={h} zoom={zoom} />
        ) : cellsData ? (
          <MiniView title="" data={cellsData} w={w} h={h} />
        ) : (
          <div style={{color:'var(--text-dim)',fontSize:14,margin:'auto'}}>Waiting for simulation...</div>
        )}
        <div className="overlay-info">gen={frame?.gen||0} mass={( frame?.mass||0).toFixed(1)} {frame?.is_empty?'[EMPTY]':frame?.is_full?'[FULL]':''} {wasd?'[WASD ON]':''}</div>
      </div>

      {/* RIGHT SIDEBAR */}
      <div className="sidebar-right">
        <div className="panel">
          <div className="panel-title">Statistics</div>
          {[['Generation',frame?.gen||0,'Simulation steps completed'],
            ['Mass',(frame?.mass||0).toFixed(2),'Sum of all cell values'],
            ['Growth',(frame?.growth||0).toFixed(2),'Positive growth field sum'],
            ['Speed',(frame?.speed||0).toFixed(4),'Center-of-mass velocity'],
            ['Gyradius',(frame?.gyradius||0).toFixed(2),'Spatial spread from center'],
            ['Lyapunov',(frame?.lyapunov||0).toFixed(4),'Chaos sensitivity'],
            ['Symmetry',`${frame?.symmetry||0}-fold`,'Rotational symmetry order'],
            ['Entropy',frame?.entropy>=0?frame.entropy.toFixed(3):'-','Pattern complexity'],
            ['Components',frame?.components>=0?frame.components:'-','Separate bodies'],
            ['Status',frame?.is_empty?'EMPTY':frame?.is_full?'FULL':'ALIVE','Organism state'],
          ].map(([label,val,tip])=><div key={label} className="stat-row" title={tip}><span className="stat-label">{label}</span><span className="stat-value">{val}</span></div>)}
        </div>

        {/* Mass chart */}
        <div className="panel">
          <div className="panel-title">Mass</div>
          <div className="chart-container"><ResponsiveContainer width="100%" height="100%"><LineChart data={history}>
            <CartesianGrid strokeDasharray="3 3" stroke="#30363d"/><XAxis dataKey="gen" stroke="#8b949e" tick={{fontSize:9}}/>
            <YAxis stroke="#8b949e" tick={{fontSize:9}}/><Tooltip contentStyle={{background:'#161b22',border:'1px solid #30363d',fontSize:10}}/>
            <Line type="monotone" dataKey="mass" stroke="#0c8599" dot={false} strokeWidth={1.5}/>
          </LineChart></ResponsiveContainer></div>
        </div>

        {/* Speed + Growth */}
        <div className="panel">
          <div className="panel-title">Growth & Speed</div>
          <div className="chart-container"><ResponsiveContainer width="100%" height="100%"><LineChart data={history}>
            <CartesianGrid strokeDasharray="3 3" stroke="#30363d"/><XAxis dataKey="gen" stroke="#8b949e" tick={{fontSize:9}}/>
            <YAxis stroke="#8b949e" tick={{fontSize:9}}/><Tooltip contentStyle={{background:'#161b22',border:'1px solid #30363d',fontSize:10}}/>
            <Line type="monotone" dataKey="growth" stroke="#2b8a3e" dot={false} strokeWidth={1.5} name="Growth"/>
            <Line type="monotone" dataKey="speed" stroke="#e67700" dot={false} strokeWidth={1.5} name="Speed"/>
          </LineChart></ResponsiveContainer></div>
        </div>

        {/* Governing equation */}
        <div className="panel">
          <div className="panel-title" title="The dynamical equation governing this organism">Equation</div>
          <div style={{fontSize:9,fontFamily:'monospace',color:'var(--text)',lineHeight:1.6,padding:'2px 0'}}>
            <div style={{color:'var(--accent)'}}>A(t+dt) = clip(A + dt·G(K∗A), 0, 1)</div>
            <div>dt = 1/{p.T} = {(1/p.T).toFixed(3)}</div>
            <div style={{color:'#8b949e',marginTop:2}}>G(u) = {p.gn===1?`exp(-(u-${p.m?.toFixed(3)})²/${(2*p.s*p.s).toFixed(6)})·2-1`:p.gn===2?`max(0,1-(u-${p.m?.toFixed(3)})²/${(9*p.s*p.s).toFixed(6)})⁴·2-1`:`|u-${p.m?.toFixed(3)}|≤${p.s?.toFixed(4)}?1:-1`}</div>
            <div style={{color:'#8b949e'}}>K(r) = {p.kn<=1?'(4r(1-r))⁴':'exp(4-1/(r(1-r)))'} · b[⌊{(p.b||[1]).length}r⌋], r=|x|/{p.R}</div>
            <div>b=[{(p.b||[1]).map(v=>v.toFixed(2)).join(',')}]</div>
          </div>
        </div>

        {/* Colormap */}
        <div className="panel">
          <div className="panel-title">Display</div>
          <div className="btn-row">{COLORMAPS.map((name,i)=><button key={i} className="btn" onClick={()=>send(`colormap ${i}`)} style={{fontSize:9,padding:'1px 5px'}}>{name}</button>)}</div>
        </div>
      </div>

      {/* STATUS BAR */}
      <div className="statusbar">
        <span>{connected?'● Connected':'○ Disconnected'}</span>
        <span>|</span><span>{w}×{h}</span>
        <span>|</span><span>R={p.R} T={p.T} m={p.m?.toFixed(3)} s={p.s?.toFixed(4)}</span>
        <span>|</span><span>View: {viewMode.toUpperCase()}</span>
        {wasd && <span>| WASD: W↑ A← S↓ D→</span>}
      </div>
    </div>
  );
}

export default App;
