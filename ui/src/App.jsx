import React, { useState, useEffect, useRef, useCallback, useMemo } from 'react';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from 'recharts';

const COLORMAPS = [
  'BCYR', 'Rainbow', 'GYPB', 'PPGG', 'BGYR', 'GYPB2', 'YPCG', 'W/B', 'B/W'
];

const KERNEL_NAMES = ['Polynomial', 'Exponential', 'Step', 'Life'];
const GROWTH_NAMES = ['Polynomial', 'Gaussian', 'Step'];

function useLeniaSocket() {
  const ws = useRef(null);
  const [frame, setFrame] = useState(null);
  const [connected, setConnected] = useState(false);
  const [history, setHistory] = useState([]);
  const [analysisHistory, setAnalysisHistory] = useState([]);
  const historyRef = useRef([]);
  const analysisRef = useRef([]);

  useEffect(() => {
    const url = `ws://${window.location.hostname}:3100`;
    const connect = () => {
      ws.current = new WebSocket(url);
      ws.current.onopen = () => { setConnected(true); console.log('WS connected'); };
      ws.current.onerror = () => {};  // suppress error noise
      ws.current.onclose = () => {
        console.log('WS disconnected, reconnecting...');
        setConnected(false);
        setFrame(null);
        historyRef.current = [];
        analysisRef.current = [];
        setHistory([]);
        setAnalysisHistory([]);
        setTimeout(connect, 1500);
      };
      ws.current.onmessage = (e) => {
        try {
          if (!e.data || typeof e.data !== 'string') return;
          const data = JSON.parse(e.data);
          if (!data || typeof data.gen !== 'number') return;
          setFrame(data);
          const entry = { gen: data.gen, mass: data.mass, time: data.time };
          historyRef.current = [...historyRef.current.slice(-200), entry];
          setHistory(historyRef.current);
          const aEntry = { gen: data.gen, growth: data.growth || 0, speed: data.speed || 0,
                           gyradius: data.gyradius || 0, lyapunov: data.lyapunov || 0,
                           symmetry: data.symmetry || 0 };
          analysisRef.current = [...analysisRef.current.slice(-200), aEntry];
          setAnalysisHistory(analysisRef.current);
        } catch (err) {}
      };
    };
    connect();
    return () => { if (ws.current) ws.current.close(); };
  }, []);

  const send = useCallback((cmd) => {
    if (ws.current && ws.current.readyState === 1) ws.current.send(cmd);
  }, []);

  return { frame, connected, history, analysisHistory, send };
}

function decodeCells(frame) {
  if (!frame || !frame.cells_b64) return null;
  const w = frame.width, h = frame.height;
  if (!w || !h) return null;
  let raw;
  try { raw = atob(frame.cells_b64); } catch (e) { return null; }
  if (raw.length < w * h) return null;
  const arr = new Uint8Array(w * h);
  for (let i = 0; i < w * h; i++) arr[i] = raw.charCodeAt(i);
  return { data: arr, w, h };
}

function colorize(v) {
  let r, g, b;
  if (v < 64) { r = 0; g = 0; b = v * 2; }
  else if (v < 128) { const t = (v - 64) / 64; r = 0; g = Math.floor(t * 200); b = 128 + Math.floor(t * 127); }
  else if (v < 192) { const t = (v - 128) / 64; r = Math.floor(t * 255); g = 200 + Math.floor(t * 55); b = 255 - Math.floor(t * 100); }
  else { const t = (v - 192) / 63; r = 255; g = 255 - Math.floor(t * 100); b = 155 - Math.floor(t * 155); }
  return [r, g, b];
}

function SimCanvas2D({ frame, zoom }) {
  const canvasRef = useRef(null);
  useEffect(() => {
    const cells = decodeCells(frame);
    if (!cells || !canvasRef.current) return;
    const { data, w, h } = cells;
    const canvas = canvasRef.current;
    const dispW = Math.floor(w * zoom);
    const dispH = Math.floor(h * zoom);
    canvas.width = w; canvas.height = h;
    canvas.style.width = dispW + 'px';
    canvas.style.height = dispH + 'px';
    const ctx = canvas.getContext('2d');
    const imgData = ctx.createImageData(w, h);
    for (let i = 0; i < w * h; i++) {
      const [r, g, b] = colorize(data[i]);
      imgData.data[i*4] = r; imgData.data[i*4+1] = g; imgData.data[i*4+2] = b; imgData.data[i*4+3] = 255;
    }
    ctx.putImageData(imgData, 0, 0);
  }, [frame, zoom]);
  return <canvas ref={canvasRef} style={{ imageRendering: 'pixelated' }} />;
}

function SimCanvas3D({ frame, zoom }) {
  const canvasRef = useRef(null);
  const stateRef = useRef({ rx: -30, ry: 30, dragging: false, lastX: 0, lastY: 0 });

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const s = stateRef.current;
    const onDown = (e) => { s.dragging = true; s.lastX = e.clientX; s.lastY = e.clientY; };
    const onMove = (e) => { if (!s.dragging) return; s.ry += (e.clientX - s.lastX) * 0.4; s.rx += (e.clientY - s.lastY) * 0.4; s.lastX = e.clientX; s.lastY = e.clientY; };
    const onUp = () => { s.dragging = false; };
    canvas.addEventListener('mousedown', onDown);
    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);
    return () => { canvas.removeEventListener('mousedown', onDown); window.removeEventListener('mousemove', onMove); window.removeEventListener('mouseup', onUp); };
  }, []);

  useEffect(() => {
    const cells = decodeCells(frame);
    if (!cells || !canvasRef.current) return;
    const { data, w, h } = cells;
    const canvas = canvasRef.current;
    const cw = canvas.parentElement?.clientWidth || 600;
    const ch = canvas.parentElement?.clientHeight || 600;
    canvas.width = cw; canvas.height = ch;
    const ctx = canvas.getContext('2d');
    ctx.fillStyle = '#0d1117'; ctx.fillRect(0, 0, cw, ch);

    const s = stateRef.current;
    const rx = s.rx * Math.PI / 180, ry = s.ry * Math.PI / 180;
    const cRx = Math.cos(rx), sRx = Math.sin(rx), cRy = Math.cos(ry), sRy = Math.sin(ry);
    const scale = Math.min(cw, ch) * 0.38 * zoom;
    const ox = cw / 2, oy = ch / 2;

    // Downsample to ~80x80 grid for smooth rendering
    const step = Math.max(1, Math.floor(Math.max(w, h) / 80));
    const gw = Math.ceil(w / step), gh = Math.ceil(h / step);

    // Build projected grid
    const proj = new Float64Array(gh * gw * 3); // sx, sy, depth per vertex
    const vals = new Float64Array(gh * gw);
    for (let gr = 0; gr < gh; gr++) {
      for (let gc = 0; gc < gw; gc++) {
        const sr = Math.min(gr * step, h - 1), sc = Math.min(gc * step, w - 1);
        const v = data[sr * w + sc] / 255;
        const x3 = (sc / w - 0.5) * 2, y3 = (sr / h - 0.5) * 2, z3 = v * 0.5;
        const x1 = x3 * cRy - z3 * sRy;
        const z1 = x3 * sRy + z3 * cRy;
        const y1 = y3 * cRx - z1 * sRx;
        const z2 = y3 * sRx + z1 * cRx;
        const idx = (gr * gw + gc) * 3;
        proj[idx] = ox + x1 * scale;
        proj[idx + 1] = oy + y1 * scale;
        proj[idx + 2] = z2;
        vals[gr * gw + gc] = v;
      }
    }

    // Draw filled quads back-to-front (painter's algorithm on quads)
    const quads = [];
    for (let gr = 0; gr < gh - 1; gr++) {
      for (let gc = 0; gc < gw - 1; gc++) {
        const i00 = gr * gw + gc, i01 = gr * gw + gc + 1;
        const i10 = (gr+1) * gw + gc, i11 = (gr+1) * gw + gc + 1;
        const avgZ = (proj[i00*3+2] + proj[i01*3+2] + proj[i10*3+2] + proj[i11*3+2]) / 4;
        const avgV = (vals[i00] + vals[i01] + vals[i10] + vals[i11]) / 4;
        quads.push({ i00, i01, i10, i11, z: avgZ, v: avgV });
      }
    }
    quads.sort((a, b) => a.z - b.z);

    for (const q of quads) {
      const [cr, cg, cb] = colorize(Math.floor(q.v * 255));
      ctx.fillStyle = `rgb(${cr},${cg},${cb})`;
      ctx.beginPath();
      ctx.moveTo(proj[q.i00*3], proj[q.i00*3+1]);
      ctx.lineTo(proj[q.i01*3], proj[q.i01*3+1]);
      ctx.lineTo(proj[q.i11*3], proj[q.i11*3+1]);
      ctx.lineTo(proj[q.i10*3], proj[q.i10*3+1]);
      ctx.closePath();
      ctx.fill();
    }

    // Axis labels
    ctx.fillStyle = '#8b949e'; ctx.font = '11px monospace';
    ctx.fillText('drag to rotate', 10, ch - 10);
  }, [frame, zoom]);

  return <canvas ref={canvasRef} style={{ width: '100%', height: '100%', cursor: 'grab' }} />;
}

function ParamSlider({ label, value, min, max, step, onChange, title }) {
  return (
    <div className="param-row" title={title}>
      <span className="param-label">{label}</span>
      <input type="range" id={`param-${label}`} name={`param-${label}`} min={min} max={max} step={step} value={value}
             title={title}
             onChange={e => onChange(parseFloat(e.target.value))} />
      <span className="param-value">{typeof value === 'number' ? value.toFixed(step < 0.01 ? 4 : 3) : value}</span>
    </div>
  );
}

function App() {
  const { frame, connected, history, analysisHistory, send } = useLeniaSocket();
  const [animals, setAnimals] = useState([]);
  const [selectedAnimal, setSelectedAnimal] = useState('');
  const [animalFilter, setAnimalFilter] = useState('');
  const [view3D, setView3D] = useState(false);
  const [zoom, setZoom] = useState(3);

  useEffect(() => {
    fetch('/api/animals').then(r => r.json()).then(setAnimals).catch(() => {});
  }, []);

  const params = (frame && frame.params) ? frame.params : { R: 13, T: 10, m: 0.15, s: 0.015, kn: 1, gn: 1, b: [1] };
  const filteredAnimals = animals.filter(a =>
    a.name.toLowerCase().includes(animalFilter.toLowerCase()) ||
    a.code.toLowerCase().includes(animalFilter.toLowerCase())
  );

  return (
    <div className="app">
      <div className="header">
        <h1>Lenia</h1>
        <span className="tag">{connected ? 'LIVE' : 'OFFLINE'}</span>
        <div className="btn-row">
          <button className={`btn ${frame?.running ? 'active' : ''}`}
                  title={frame?.running ? 'Pause the simulation' : 'Resume the simulation'}
                  onClick={() => send(frame?.running ? 'pause' : 'run')}>
            {frame?.running ? '⏸ Pause' : '▶ Run'}
          </button>
          <button className="btn" title="Advance one simulation step (while paused)" onClick={() => send('step')}>Step</button>
          <button className="btn" title="Randomize the world with current parameters — fills center with random cell values" onClick={() => send('random')}>Random</button>
          <button className="btn" title="Randomize both the world AND parameters (R, T, m, s, b) — generates a completely new organism candidate" onClick={() => send('random_params')}>New Params</button>
          <button className="btn" title="Clear the world — set all cells to zero" onClick={() => send('clear')}>Clear</button>
          <button className="btn" title="Generate a world using a CPPN (Compositional Pattern-Producing Network) — creates smooth organic initial patterns via a small neural network" onClick={() => send('cppn')}>CPPN</button>
          <button className={`btn ${view3D ? 'active' : ''}`}
                  title={view3D ? 'Switch back to flat 2D pixel view' : 'View the simulation as a 3D height-map surface (cell value = height). Drag to rotate, scroll to zoom.'}
                  onClick={() => setView3D(!view3D)}>
            {view3D ? '2D View' : '3D Surface'}
          </button>
        </div>
        <span style={{ marginLeft: 'auto', fontSize: 11, color: 'var(--text-dim)' }}>
          gen {frame?.gen || 0} | mass {(frame?.mass || 0).toFixed(1)} | {frame?.name || ''}
        </span>
      </div>

      <div className="sidebar-left">
        <div className="panel">
          <div className="panel-title">Parameters</div>
          <ParamSlider label="R" value={params.R} min={5} max={50} step={1}
                       title="Kernel Radius — how far each cell can sense its neighbors. Larger R = wider interaction range. Optimal is often ~13 for Orbium."
                       onChange={v => send(`set_R ${Math.round(v)}`)} />
          <ParamSlider label="T" value={params.T} min={1} max={50} step={1}
                       title="Time Resolution — number of sub-steps per generation. Higher T = smoother/slower dynamics (dt = 1/T)."
                       onChange={v => send(`set_T ${Math.round(v)}`)} />
          <ParamSlider label="m" value={params.m} min={0.01} max={0.5} step={0.001}
                       title="Growth Mean — the neighborhood density at which cells grow fastest. The 'sweet spot' for life. Too low = everything grows, too high = nothing grows."
                       onChange={v => send(`set_m ${v}`)} />
          <ParamSlider label="s" value={params.s} min={0.001} max={0.1} step={0.0001}
                       title="Growth Sigma — how sharply the growth function peaks around m. Smaller s = narrower viability window = more selective/fragile organisms."
                       onChange={v => send(`set_s ${v}`)} />
          <div className="param-row" title="Kernel Shape — the spatial interaction pattern. Exponential (bump) is the classic Lenia kernel. Polynomial is smoother. Step creates sharp-edged rings.">
            <span className="param-label">kn</span>
            <select id="param-kn" name="param-kn" value={params.kn} onChange={e => send(`set_kn ${e.target.value}`)}
                    style={{ flex: 1, margin: '0 8px', background: 'var(--bg)', color: 'var(--text)', border: '1px solid var(--border)', padding: '2px', borderRadius: 3 }}>
              {KERNEL_NAMES.map((n, i) => <option key={i} value={i + 1}>{n}</option>)}
            </select>
          </div>
          <div className="param-row" title="Growth Function — how neighborhood density maps to cell growth/decay. Gaussian is classic Lenia. Polynomial has flatter tails. Step creates binary alive/dead behavior.">
            <span className="param-label">gn</span>
            <select id="param-gn" name="param-gn" value={params.gn} onChange={e => send(`set_gn ${e.target.value}`)}
                    style={{ flex: 1, margin: '0 8px', background: 'var(--bg)', color: 'var(--text)', border: '1px solid var(--border)', padding: '2px', borderRadius: 3 }}>
              {GROWTH_NAMES.map((n, i) => <option key={i} value={i + 1}>{n}</option>)}
            </select>
          </div>
          <div className="param-row" title="Birth weights: ring amplitudes for the kernel. Controls concentric ring structure.">
            <span className="param-label">b</span>
            <span className="param-value">[{(params.b || [1]).map(v => v.toFixed(2)).join(', ')}]</span>
          </div>
          <div className="param-row" title="Grid size (NxN). Larger = bigger organisms but slower. Changing restarts the simulation.">
            <span className="param-label">Grid</span>
            <select id="param-grid" name="param-grid" value={frame?.width || 128}
                    onChange={e => {
                      fetch('/api/resize', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify({ size: parseInt(e.target.value), code: frame?.code || '' })
                      }).catch(() => {});
                    }}
                    style={{ flex: 1, margin: '0 8px', background: 'var(--bg)', color: 'var(--text)', border: '1px solid var(--border)', padding: '2px', borderRadius: 3 }}>
              <option value="32">32x32</option>
              <option value="64">64x64</option>
              <option value="128">128x128</option>
              <option value="256">256x256</option>
              <option value="512">512x512</option>
            </select>
          </div>
        </div>

        <div className="panel">
          <div className="panel-title">Search</div>
          <div className="btn-row">
            <button className="btn" title="Search UP: mutate parameters toward more complex organisms. If the current organism dies or fills the world, tries new random parameters. If it survives, nudges m and s slightly to explore nearby viable parameter space." onClick={() => send('search_up')}>Search ↑</button>
            <button className="btn" title="Search DOWN: mutate parameters toward simpler organisms. Same stability check as Search ↑ but nudges parameters in the opposite direction." onClick={() => send('search_down')}>Search ↓</button>
            <button className="btn" title="Stop the current parameter search" onClick={() => send('search_stop')}>Stop</button>
          </div>
        </div>

        <div className="panel">
          <div className="panel-title">Animals ({filteredAnimals.length})</div>
          <input type="text" placeholder="Filter..." value={animalFilter}
                 onChange={e => setAnimalFilter(e.target.value)}
                 style={{ width: '100%', background: 'var(--bg)', border: '1px solid var(--border)',
                          color: 'var(--text)', padding: '4px 8px', borderRadius: 4, marginBottom: 4, fontSize: 12 }} />
          <div className="animal-list">
            {filteredAnimals.slice(0, 100).map(a => (
              <div key={a.code} className={`animal-item ${selectedAnimal === a.code ? 'active' : ''}`}
                   onClick={() => { setSelectedAnimal(a.code); send(`load ${a.code}`); }}>
                <strong>{a.code}</strong> {a.name}
              </div>
            ))}
          </div>
        </div>
      </div>

      <div className="viewport" onWheel={(e) => { e.preventDefault(); setZoom(z => Math.max(0.5, Math.min(10, z * (e.deltaY < 0 ? 1.1 : 0.9)))); }}>
        {frame && frame.cells_b64 ? (view3D ? <SimCanvas3D frame={frame} zoom={zoom} /> : <SimCanvas2D frame={frame} zoom={zoom} />) : <div style={{color:'var(--text-dim)',fontSize:14}}>Waiting for simulation...</div>}
        <div className="overlay-info">
          gen={frame?.gen || 0} mass={( frame?.mass || 0).toFixed(1)}
          {frame?.is_empty ? ' [EMPTY]' : ''}{frame?.is_full ? ' [FULL]' : ''}
        </div>
      </div>

      <div className="sidebar-right">
        <div className="panel">
          <div className="panel-title">Statistics</div>
          <div className="stat-row" title="Number of simulation steps completed"><span className="stat-label">Generation</span><span className="stat-value">{frame?.gen || 0}</span></div>
          <div className="stat-row" title="Simulation time = generation x dt (where dt = 1/T)"><span className="stat-label">Time</span><span className="stat-value">{(frame?.time || 0).toFixed(2)}</span></div>
          <div className="stat-row" title="Total cell mass: sum of all cell values. Stable organisms maintain roughly constant mass."><span className="stat-label">Mass</span><span className="stat-value">{(frame?.mass || 0).toFixed(2)}</span></div>
          <div className="stat-row" title="Growth rate: sum of positive growth field values. High growth = organism is actively building."><span className="stat-label">Growth</span><span className="stat-value">{(frame?.growth || 0).toFixed(2)}</span></div>
          <div className="stat-row" title="Movement speed: how fast the organism center of mass moves per step. Gliders have high speed."><span className="stat-label">Speed</span><span className="stat-value">{(frame?.speed || 0).toFixed(4)}</span></div>
          <div className="stat-row" title="Gyration radius: how spread out the organism is. Larger = more extended shape."><span className="stat-label">Gyradius</span><span className="stat-value">{(frame?.gyradius || 0).toFixed(2)}</span></div>
          <div className="stat-row" title="Lyapunov exponent: sensitivity to perturbation. Positive = chaotic, ~0 = stable, negative = damped."><span className="stat-label">Lyapunov</span><span className="stat-value">{(frame?.lyapunov || 0).toFixed(4)}</span></div>
          <div className="stat-row" title="Rotational symmetry order detected via polar FFT (e.g. 6 = hexagonal, 2 = bilateral)."><span className="stat-label">Symmetry</span><span className="stat-value">{frame?.symmetry || 0}-fold</span></div>
          <div className="stat-row"><span className="stat-label">View</span><span className="stat-value">{view3D ? '3D Surface' : '2D'}</span></div>
          <div className="stat-row" title="Shannon entropy of cell values. Low = uniform, high = complex/diverse patterns."><span className="stat-label">Entropy</span><span className="stat-value">{frame?.entropy >= 0 ? frame.entropy.toFixed(3) : '-'}</span></div>
          <div className="stat-row" title="Connected components: number of separate bodies. 1 = single organism, 2+ = multiple or fragmented."><span className="stat-label">Components</span><span className="stat-value">{frame?.components >= 0 ? frame.components : '-'}</span></div>
          <div className="stat-row" title="EMPTY = organism died. FULL = hit boundary. ALIVE = stable and contained."><span className="stat-label">Status</span><span className="stat-value">{frame?.is_empty ? 'EMPTY' : frame?.is_full ? 'FULL' : 'ALIVE'}</span></div>
          <div className="stat-row" title="Animal code from the Lenia creature library"><span className="stat-label">Code</span><span className="stat-value">{frame?.code || '-'}</span></div>
        </div>

        <div className="panel">
          <div className="panel-title">Mass Over Time</div>
          <div className="chart-container">
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={history}>
                <CartesianGrid strokeDasharray="3 3" stroke="#30363d" />
                <XAxis dataKey="gen" stroke="#8b949e" tick={{ fontSize: 10 }} />
                <YAxis stroke="#8b949e" tick={{ fontSize: 10 }} />
                <Tooltip contentStyle={{ background: '#161b22', border: '1px solid #30363d', fontSize: 11 }} />
                <Line type="monotone" dataKey="mass" stroke="#0c8599" dot={false} strokeWidth={1.5} />
              </LineChart>
            </ResponsiveContainer>
          </div>
        </div>

        <div className="panel">
          <div className="panel-title">Growth & Speed</div>
          <div className="chart-container">
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={analysisHistory}>
                <CartesianGrid strokeDasharray="3 3" stroke="#30363d" />
                <XAxis dataKey="gen" stroke="#8b949e" tick={{ fontSize: 10 }} />
                <YAxis stroke="#8b949e" tick={{ fontSize: 10 }} />
                <Tooltip contentStyle={{ background: '#161b22', border: '1px solid #30363d', fontSize: 11 }} />
                <Line type="monotone" dataKey="growth" stroke="#2b8a3e" dot={false} strokeWidth={1.5} name="Growth" />
                <Line type="monotone" dataKey="speed" stroke="#e67700" dot={false} strokeWidth={1.5} name="Speed" />
              </LineChart>
            </ResponsiveContainer>
          </div>
        </div>

        <div className="panel">
          <div className="panel-title">Gyradius & Lyapunov</div>
          <div className="chart-container">
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={analysisHistory}>
                <CartesianGrid strokeDasharray="3 3" stroke="#30363d" />
                <XAxis dataKey="gen" stroke="#8b949e" tick={{ fontSize: 10 }} />
                <YAxis stroke="#8b949e" tick={{ fontSize: 10 }} />
                <Tooltip contentStyle={{ background: '#161b22', border: '1px solid #30363d', fontSize: 11 }} />
                <Line type="monotone" dataKey="gyradius" stroke="#a61e4d" dot={false} strokeWidth={1.5} name="Gyradius" />
                <Line type="monotone" dataKey="lyapunov" stroke="#862e9c" dot={false} strokeWidth={1.5} name="Lyapunov" />
              </LineChart>
            </ResponsiveContainer>
          </div>
        </div>

        <div className="panel">
          <div className="panel-title">Display</div>
          <div className="btn-row">
            {COLORMAPS.map((name, i) => (
              <button key={i} className="btn" onClick={() => send(`colormap ${i}`)}
                      style={{ fontSize: 10, padding: '2px 6px' }}>{name}</button>
            ))}
          </div>
        </div>
      </div>

      <div className="statusbar">
        <span>{connected ? '● Connected' : '○ Disconnected'}</span>
        <span>|</span>
        <span>{frame?.width || 0}×{frame?.height || 0}</span>
        <span>|</span>
        <span>R={params.R} T={params.T} m={params.m?.toFixed(3)} s={params.s?.toFixed(4)}</span>
      </div>
    </div>
  );
}

export default App;
