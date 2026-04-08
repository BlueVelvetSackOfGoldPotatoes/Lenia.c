import React, { useState, useEffect, useRef, useCallback } from 'react';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, ScatterChart, Scatter } from 'recharts';

const COLORMAPS = ['BCYR','Rainbow','GYPB','PPGG','BGYR','GYPB2','YPCG','W/B','B/W'];
const KERNEL_NAMES = ['Polynomial','Exponential','Step','Life'];
const GROWTH_NAMES = ['Polynomial','Gaussian','Step'];

function useLeniaSocket() {
  const ws = useRef(null);
  const [frame, setFrame] = useState(null);
  const [connected, setConnected] = useState(false);
  const [history, setHistory] = useState([]);
  const hr = useRef([]);
  useEffect(() => {
    const url = `ws://${window.location.hostname}:3100`;
    const connect = () => {
      ws.current = new WebSocket(url);
      ws.current.onopen = () => setConnected(true);
      ws.current.onerror = () => {};
      ws.current.onclose = () => { setConnected(false); setFrame(null); hr.current=[]; setHistory([]); setTimeout(connect,1500); };
      ws.current.onmessage = (e) => {
        try { const d = JSON.parse(e.data); if(typeof d.gen!=='number')return; setFrame(d);
          hr.current=[...hr.current.slice(-300),{gen:d.gen,mass:d.mass,growth:d.growth||0,speed:d.speed||0,gyradius:d.gyradius||0}];
          setHistory(hr.current);
        } catch(err){}
      };
    };
    connect();
    return () => { if(ws.current) ws.current.close(); };
  }, []);
  const send = useCallback((cmd) => { if(ws.current?.readyState===1) ws.current.send(cmd); }, []);
  return { frame, connected, history, send };
}

function colorize(v) {
  if(v<64) return [0,0,v*2];
  if(v<128){const t=(v-64)/64;return [0,Math.floor(t*200),128+Math.floor(t*127)];}
  if(v<192){const t=(v-128)/64;return [Math.floor(t*255),200+Math.floor(t*55),255-Math.floor(t*100)];}
  const t=(v-192)/63;return [255,255-Math.floor(t*100),155-Math.floor(t*155)];
}
function hotmap(v){return [Math.min(255,v*3),Math.min(255,Math.max(0,(v-85)*3)),Math.min(255,Math.max(0,(v-170)*3))];}
function coolmap(v){return [v<128?0:Math.floor((v-128)*2),Math.min(255,v),Math.min(255,255-Math.floor(v*0.3))];}

function decodeB64(b64,w,h){if(!b64)return null;try{const r=atob(b64);if(r.length<w*h)return null;const a=new Uint8Array(w*h);for(let i=0;i<w*h;i++)a[i]=r.charCodeAt(i);return a;}catch(e){return null;}}

function CanvasPanel({data,w,h,cfn,zoom,label}){
  const ref=useRef(null);
  useEffect(()=>{
    if(!ref.current||!data)return;
    const c=ref.current;c.width=w;c.height=h;
    const ctx=c.getContext('2d');const img=ctx.createImageData(w,h);
    for(let i=0;i<w*h;i++){const[r,g,b]=(cfn||colorize)(data[i]);img.data[i*4]=r;img.data[i*4+1]=g;img.data[i*4+2]=b;img.data[i*4+3]=255;}
    ctx.putImageData(img,0,0);
  },[data,w,h,cfn]);
  const z = zoom || 1;
  const pw = Math.round(w * z);
  const ph = Math.round(h * z);
  return <div style={{position:'relative',flex:1,display:'flex',alignItems:'center',justifyContent:'center',overflow:'hidden',minWidth:0,minHeight:0}}>
    {label&&<div style={{position:'absolute',top:2,left:4,fontSize:9,color:'#8b949e',zIndex:1,background:'rgba(0,0,0,0.6)',padding:'0 4px',borderRadius:2}}>{label}</div>}
    <canvas ref={ref} style={{width:pw,height:ph,imageRendering:'pixelated',maxWidth:'100%',maxHeight:'100%'}}/>
  </div>;
}

function Surface3D({data,w,h,zoom}){
  const ref=useRef(null);const st=useRef({rx:-30,ry:30,d:false,lx:0,ly:0});
  useEffect(()=>{const c=ref.current;if(!c)return;const s=st.current;
    const dn=e=>{e.stopPropagation();s.d=true;s.lx=e.clientX;s.ly=e.clientY;};
    const mv=e=>{if(!s.d)return;s.ry+=(e.clientX-s.lx)*0.4;s.rx+=(e.clientY-s.ly)*0.4;s.lx=e.clientX;s.ly=e.clientY;};
    const up=()=>{s.d=false;};
    c.addEventListener('mousedown',dn);c.addEventListener('mousemove',mv);c.addEventListener('mouseup',up);c.addEventListener('mouseleave',up);
    return()=>{c.removeEventListener('mousedown',dn);c.removeEventListener('mousemove',mv);c.removeEventListener('mouseup',up);c.removeEventListener('mouseleave',up);};
  },[]);
  useEffect(()=>{
    if(!data||!ref.current)return;const c=ref.current;const cw=c.parentElement?.clientWidth||400;const ch=c.parentElement?.clientHeight||400;
    c.width=cw;c.height=ch;const ctx=c.getContext('2d');ctx.fillStyle='#0d1117';ctx.fillRect(0,0,cw,ch);
    const s=st.current;const rx=s.rx*Math.PI/180,ry=s.ry*Math.PI/180;
    const cRx=Math.cos(rx),sRx=Math.sin(rx),cRy=Math.cos(ry),sRy=Math.sin(ry);
    const scale=Math.min(cw,ch)*0.4*zoom;const ox=cw/2,oy=ch/2;
    const step=Math.max(1,Math.floor(Math.max(w,h)/100));const gw=Math.ceil(w/step),gh=Math.ceil(h/step);
    const proj=new Float64Array(gh*gw*3);const vals=new Float64Array(gh*gw);
    for(let gr=0;gr<gh;gr++)for(let gc=0;gc<gw;gc++){
      const sr=Math.min(gr*step,h-1),sc=Math.min(gc*step,w-1);const v=data[sr*w+sc]/255;
      const x3=(sc/w-0.5)*2,y3=(sr/h-0.5)*2,z3=v*0.5;
      const x1=x3*cRy-z3*sRy,z1=x3*sRy+z3*cRy,y1=y3*cRx-z1*sRx,z2=y3*sRx+z1*cRx;
      const i=(gr*gw+gc)*3;proj[i]=ox+x1*scale;proj[i+1]=oy+y1*scale;proj[i+2]=z2;vals[gr*gw+gc]=v;
    }
    const quads=[];
    for(let gr=0;gr<gh-1;gr++)for(let gc=0;gc<gw-1;gc++){
      const i00=gr*gw+gc,i01=gr*gw+gc+1,i10=(gr+1)*gw+gc,i11=(gr+1)*gw+gc+1;
      quads.push({i00,i01,i10,i11,z:(proj[i00*3+2]+proj[i01*3+2]+proj[i10*3+2]+proj[i11*3+2])/4,v:(vals[i00]+vals[i01]+vals[i10]+vals[i11])/4});
    }
    quads.sort((a,b)=>a.z-b.z);
    for(const q of quads){const[r,g,b]=colorize(Math.floor(q.v*255));ctx.fillStyle=`rgb(${r},${g},${b})`;ctx.beginPath();ctx.moveTo(proj[q.i00*3],proj[q.i00*3+1]);ctx.lineTo(proj[q.i01*3],proj[q.i01*3+1]);ctx.lineTo(proj[q.i11*3],proj[q.i11*3+1]);ctx.lineTo(proj[q.i10*3],proj[q.i10*3+1]);ctx.closePath();ctx.fill();}
  },[data,w,h,zoom]);
  return <canvas ref={ref} style={{width:'100%',height:'100%',cursor:'grab'}}/>;
}

function PS({label,value,min,max,step,onChange,title}){
  return <div className="param-row" title={title}><span className="param-label">{label}</span>
    <input type="range" id={`p-${label}`} name={`p-${label}`} min={min} max={max} step={step} value={value} onChange={e=>onChange(parseFloat(e.target.value))}/>
    <span className="param-value">{typeof value==='number'?value.toFixed(step<0.01?4:step<1?3:0):value}</span></div>;
}

export default function App() {
  const {frame,connected,history,send}=useLeniaSocket();
  const [animals,setAnimals]=useState([]);
  const [sel,setSel]=useState('');
  const [flt,setFlt]=useState('');
  const [view,setView]=useState('quad');
  const [zoom,setZoom]=useState(1);
  const [wasd,setWasd]=useState(false);

  useEffect(()=>{fetch('/api/animals').then(r=>r.json()).then(setAnimals).catch(()=>{});},[]);
  // WASD: toggle the global flag that the raw handler in index.html checks
  useEffect(()=>{
    window.__leniaWASD = wasd;
  },[wasd]);

  const p=frame?.params||{R:13,T:10,m:0.15,s:0.015,kn:1,gn:1,b:[1]};
  const w=frame?.width||128,h=frame?.height||128;
  const cells=frame?decodeB64(frame.cells_b64,w,h):null;
  const pot=frame?decodeB64(frame.potential_b64,w,h):null;
  const fld=frame?decodeB64(frame.field_b64,w,h):null;
  const fa=animals.filter(a=>a.name.toLowerCase().includes(flt.toLowerCase())||a.code.toLowerCase().includes(flt.toLowerCase()));

  return (
    <div className="app" onWheel={e=>{e.preventDefault();setZoom(z=>Math.max(0.5,Math.min(8,z*(e.deltaY<0?1.1:0.9))));}}>
      {/* HEADER */}
      <div className="header">
        <h1>Lenia</h1>
        <span className="tag">{connected?'LIVE':'OFF'}</span>
        <div className="btn-row">
          <button className={`btn ${frame?.running?'active':''}`} title="Play/pause" onClick={()=>send(frame?.running?'pause':'run')}>{frame?.running?'⏸':'▶'}</button>
          <button className="btn" title="Single step" onClick={()=>send('step')}>Step</button>
          <button className="btn" title="Random cells" onClick={()=>send('random')}>Rand</button>
          <button className="btn" title="Random cells + params" onClick={()=>send('random_params')}>New</button>
          <button className="btn" title="Clear" onClick={()=>send('clear')}>Clr</button>
          <button className="btn" title="CPPN neural pattern" onClick={()=>send('cppn')}>CPPN</button>
          <span style={{width:1,background:'var(--border)',height:20}}/>
          {['quad','2d','3d'].map(m=><button key={m} className={`btn ${view===m?'active':''}`} onClick={()=>setView(m)}>{m.toUpperCase()}</button>)}
          <span style={{width:1,background:'var(--border)',height:20}}/>
          <button className={`btn ${wasd?'active':''}`} title={"WASD Steering — adds a convection (wind) term to the Lenia equation: A' = A + dt·G(K*A) - dt·(v·∇A). " +
            "When you hold a key, a velocity field is applied across the entire grid. This computes the spatial gradient ∇A of the cell values " +
            "and subtracts v·∇A, which physically transports all cell material in the pressed direction. " +
            "The organism stays alive because the growth function G(K*A) keeps running — it heals itself as it moves. " +
            "This is equivalent to the organism swimming through a flowing medium. " +
            "Release the key and the flow stops — the organism continues under its own dynamics. " +
            "W=up A=left S=down D=right. Hold two keys for diagonal flow."} onClick={(e)=>{setWasd(w=>!w);e.target.blur();}}>WASD{wasd?' ●':''}</button>
        </div>
        <span style={{marginLeft:'auto',fontSize:10,color:'var(--text-dim)',fontFamily:'monospace'}}>
          gen {frame?.gen||0} | m={( frame?.mass||0).toFixed(1)} | {frame?.name||''}
        </span>
      </div>

      {/* LEFT SIDEBAR */}
      <div className="sidebar-left">
        <div className="panel">
          <div className="panel-title">Params</div>
          <PS label="R" value={p.R} min={5} max={50} step={1} title="Kernel radius" onChange={v=>send(`set_R ${Math.round(v)}`)}/>
          <PS label="T" value={p.T} min={1} max={50} step={1} title="Time steps (dt=1/T)" onChange={v=>send(`set_T ${Math.round(v)}`)}/>
          <PS label="m" value={p.m} min={0.01} max={0.5} step={0.001} title="Growth center" onChange={v=>send(`set_m ${v}`)}/>
          <PS label="s" value={p.s} min={0.001} max={0.1} step={0.0001} title="Growth width" onChange={v=>send(`set_s ${v}`)}/>
          <div className="param-row"><span className="param-label">kn</span><select id="p-kn" name="p-kn" value={p.kn} onChange={e=>send(`set_kn ${e.target.value}`)}>{KERNEL_NAMES.map((n,i)=><option key={i} value={i+1}>{n}</option>)}</select></div>
          <div className="param-row"><span className="param-label">gn</span><select id="p-gn" name="p-gn" value={p.gn} onChange={e=>send(`set_gn ${e.target.value}`)}>{GROWTH_NAMES.map((n,i)=><option key={i} value={i+1}>{n}</option>)}</select></div>
          <div className="param-row"><span className="param-label">b</span><span className="param-value" style={{fontSize:10}}>[{(p.b||[1]).map(v=>v.toFixed(2)).join(',')}]</span></div>
          <div className="param-row"><span className="param-label">Size</span><select id="p-sz" name="p-sz" value={w} onChange={e=>fetch('/api/resize',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({size:parseInt(e.target.value),code:frame?.code||''})}).catch(()=>{})}>
            {[32,64,128,256,512].map(s=><option key={s} value={s}>{s}²</option>)}</select></div>
        </div>
        <div className="panel">
          <div className="panel-title">Search</div>
          <div className="btn-row">
            <button className="btn" title="Mutate toward complexity" onClick={()=>send('search_up')}>↑ Complex</button>
            <button className="btn" title="Mutate toward simplicity" onClick={()=>send('search_down')}>↓ Simple</button>
            <button className="btn" onClick={()=>send('search_stop')}>Stop</button>
          </div>
        </div>
        <div className="panel">
          <div className="panel-title">Library ({fa.length})</div>
          <input type="text" id="af" name="af" placeholder="Search..." value={flt} onChange={e=>setFlt(e.target.value)} style={{width:'100%',background:'var(--bg)',border:'1px solid var(--border)',color:'var(--text)',padding:'3px 6px',borderRadius:3,fontSize:11,marginBottom:3}}/>
          <div className="animal-list">
            {fa.slice(0,60).map(a=><div key={a.code} className={`animal-item ${sel===a.code?'active':''}`} onClick={()=>{setSel(a.code);send(`load ${a.code}`);}}><b>{a.code}</b> {a.name}</div>)}
          </div>
        </div>
      </div>

      {/* VIEWPORT */}
      <div className="viewport" style={{display:'flex',flexDirection:'column'}}>
        {view==='quad' && cells ? (
          <>
            {/* Main world view — takes 65% */}
            <div style={{flex:7,display:'flex',position:'relative',minHeight:0}}>
              <CanvasPanel data={cells} w={w} h={h} label="World f(x)" zoom={zoom}/>
              <div className="overlay-info">gen={frame?.gen||0} mass={( frame?.mass||0).toFixed(1)} {wasd?'[WASD]':''}</div>
            </div>
            {/* Bottom strip — 35%: growth + potential + phase */}
            <div style={{flex:3,display:'flex',gap:1,background:'var(--border)',minHeight:0}}>
              <CanvasPanel data={fld} w={w} h={h} cfn={hotmap} label="Growth δ(k∗f)"/>
              <CanvasPanel data={pot} w={w} h={h} cfn={coolmap} label="Potential k∗f"/>
              <div style={{flex:1,background:'#000',minWidth:0,display:'flex',flexDirection:'column'}}>
                <div style={{fontSize:9,color:'#8b949e',padding:'2px 4px',textAlign:'center'}}>Phase (mass vs growth)</div>
                <div style={{flex:1,minHeight:0}}>
                  <ResponsiveContainer width="100%" height="100%">
                    <ScatterChart margin={{top:2,right:4,bottom:14,left:14}}>
                      <CartesianGrid strokeDasharray="3 3" stroke="#30363d"/>
                      <XAxis dataKey="mass" stroke="#8b949e" tick={{fontSize:8}} label={{value:'m',position:'bottom',fontSize:8,fill:'#8b949e'}}/>
                      <YAxis dataKey="growth" stroke="#8b949e" tick={{fontSize:8}} label={{value:'g',angle:-90,position:'left',fontSize:8,fill:'#8b949e'}}/>
                      <Scatter data={history.slice(-150)} fill="#0c8599" fillOpacity={0.5} r={1.5}/>
                    </ScatterChart>
                  </ResponsiveContainer>
                </div>
              </div>
            </div>
          </>
        ) : view==='3d' && cells ? (
          <>
            <div style={{flex:7,minHeight:0,position:'relative'}}>
              <Surface3D data={cells} w={w} h={h} zoom={zoom}/>
              <div className="overlay-info">gen={frame?.gen||0} mass={( frame?.mass||0).toFixed(1)} {wasd?'[WASD]':''}</div>
            </div>
            <div style={{flex:3,display:'flex',gap:1,background:'var(--border)',minHeight:0}}>
              <CanvasPanel data={fld} w={w} h={h} cfn={hotmap} label="Growth δ(k∗f)"/>
              <CanvasPanel data={pot} w={w} h={h} cfn={coolmap} label="Potential k∗f"/>
              <div style={{flex:1,background:'#000',minWidth:0,display:'flex',flexDirection:'column'}}>
                <div style={{fontSize:9,color:'#8b949e',padding:'2px 4px',textAlign:'center'}}>Phase (mass vs growth)</div>
                <div style={{flex:1,minHeight:0}}>
                  <ResponsiveContainer width="100%" height="100%">
                    <ScatterChart margin={{top:2,right:4,bottom:14,left:14}}>
                      <CartesianGrid strokeDasharray="3 3" stroke="#30363d"/>
                      <XAxis dataKey="mass" stroke="#8b949e" tick={{fontSize:8}}/>
                      <YAxis dataKey="growth" stroke="#8b949e" tick={{fontSize:8}}/>
                      <Scatter data={history.slice(-150)} fill="#0c8599" fillOpacity={0.5} r={1.5}/>
                    </ScatterChart>
                  </ResponsiveContainer>
                </div>
              </div>
            </div>
          </>
        ) : cells ? (
          <>
            <div style={{flex:7,position:'relative',minHeight:0}}>
              <CanvasPanel data={cells} w={w} h={h} zoom={zoom}/>
              <div className="overlay-info">gen={frame?.gen||0} mass={( frame?.mass||0).toFixed(1)} {wasd?'[WASD]':''}</div>
            </div>
            <div style={{flex:3,display:'flex',gap:1,background:'var(--border)',minHeight:0}}>
              <CanvasPanel data={fld} w={w} h={h} cfn={hotmap} label="Growth δ(k∗f)"/>
              <CanvasPanel data={pot} w={w} h={h} cfn={coolmap} label="Potential k∗f"/>
              <div style={{flex:1,background:'#000',minWidth:0,display:'flex',flexDirection:'column'}}>
                <div style={{fontSize:9,color:'#8b949e',padding:'2px 4px',textAlign:'center'}}>Phase (mass vs growth)</div>
                <div style={{flex:1,minHeight:0}}>
                  <ResponsiveContainer width="100%" height="100%">
                    <ScatterChart margin={{top:2,right:4,bottom:14,left:14}}>
                      <CartesianGrid strokeDasharray="3 3" stroke="#30363d"/>
                      <XAxis dataKey="mass" stroke="#8b949e" tick={{fontSize:8}}/>
                      <YAxis dataKey="growth" stroke="#8b949e" tick={{fontSize:8}}/>
                      <Scatter data={history.slice(-150)} fill="#0c8599" fillOpacity={0.5} r={1.5}/>
                    </ScatterChart>
                  </ResponsiveContainer>
                </div>
              </div>
            </div>
          </>
        ) : <div style={{margin:'auto',color:'var(--text-dim)'}}>Connecting...</div>}
      </div>

      {/* RIGHT SIDEBAR */}
      <div className="sidebar-right">
        <div className="panel">
          <div className="panel-title">Stats</div>
          {[['Gen',frame?.gen||0],['Mass',(frame?.mass||0).toFixed(1)],['Growth',(frame?.growth||0).toFixed(1)],
            ['Speed',(frame?.speed||0).toFixed(4)],['Gyradius',(frame?.gyradius||0).toFixed(2)],
            ['Lyapunov',(frame?.lyapunov||0).toFixed(4)],['Symmetry',`${frame?.symmetry||0}-fold`],
            ['Entropy',frame?.entropy>=0?frame.entropy.toFixed(3):'-'],
            ['Components',frame?.components>=0?frame.components:'-'],
            ['Status',frame?.is_empty?'EMPTY':frame?.is_full?'FULL':'ALIVE'],
          ].map(([l,v])=><div key={l} className="stat-row"><span className="stat-label">{l}</span><span className="stat-value">{v}</span></div>)}
        </div>
        <div className="panel">
          <div className="panel-title">Mass</div>
          <div className="chart-container"><ResponsiveContainer width="100%" height="100%"><LineChart data={history}>
            <XAxis dataKey="gen" stroke="#8b949e" tick={{fontSize:8}}/><YAxis stroke="#8b949e" tick={{fontSize:8}}/>
            <Line type="monotone" dataKey="mass" stroke="#0c8599" dot={false} strokeWidth={1.5}/>
          </LineChart></ResponsiveContainer></div>
        </div>
        <div className="panel">
          <div className="panel-title">Growth & Speed</div>
          <div className="chart-container"><ResponsiveContainer width="100%" height="100%"><LineChart data={history}>
            <XAxis dataKey="gen" stroke="#8b949e" tick={{fontSize:8}}/><YAxis stroke="#8b949e" tick={{fontSize:8}}/>
            <Line type="monotone" dataKey="growth" stroke="#2b8a3e" dot={false} strokeWidth={1}/>
            <Line type="monotone" dataKey="speed" stroke="#e67700" dot={false} strokeWidth={1}/>
          </LineChart></ResponsiveContainer></div>
        </div>
        <div className="panel">
          <div className="panel-title">Equation</div>
          <div style={{fontSize:9,fontFamily:'monospace',lineHeight:1.5}}>
            <div style={{color:'var(--accent)'}}>A' = clip(A + dt·G(K∗A), 0, 1)</div>
            <div>dt=1/{p.T}  R={p.R}  m={p.m?.toFixed(3)}  s={p.s?.toFixed(4)}</div>
            <div style={{color:'#8b949e'}}>G(u)={p.gn===1?'exp(-(u-m)²/2s²)·2-1':p.gn===2?'poly4':'step'}</div>
            <div style={{color:'#8b949e'}}>K(r)={p.kn<=1?'(4r(1-r))⁴':'bump4'}·b, b=[{(p.b||[1]).map(v=>v.toFixed(1)).join(',')}]</div>
          </div>
        </div>
        <div className="panel">
          <div className="panel-title">Colormap</div>
          <div className="btn-row">{COLORMAPS.map((n,i)=><button key={i} className="btn" onClick={()=>send(`colormap ${i}`)} style={{fontSize:9,padding:'1px 4px'}}>{n}</button>)}</div>
        </div>
      </div>

      <div className="statusbar">
        <span>{connected?'●':'○'} {connected?'Connected':'Offline'}</span>
        <span>|</span><span>{w}²</span>
        <span>|</span><span>R={p.R} T={p.T} m={p.m?.toFixed(3)} s={p.s?.toFixed(4)}</span>
        <span>|</span><span>{view.toUpperCase()}</span>
        {wasd&&<span>| WASD active</span>}
      </div>
    </div>
  );
}
