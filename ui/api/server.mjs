import { spawn } from 'child_process';
import { createServer } from 'http';
import { WebSocketServer } from 'ws';
import { readFileSync, existsSync, writeFileSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT = join(__dirname, '..', '..');
const BUILD = join(ROOT, 'build');
const DIST = join(__dirname, '..', 'dist');
const LENIA_PY_DIR = join(ROOT, '..', 'external_repos', 'Lenia', 'Python');
const ANIMALS = join(LENIA_PY_DIR, 'animals.json');

const PORT = process.env.PORT || 3100;
const SIM_SIZE = parseInt(process.env.SIM_SIZE || '128');
const SIM_FPS = parseInt(process.env.SIM_FPS || '30');
let SIM_DIM = parseInt(process.env.SIM_DIM || '2');
const FRAME_MAGIC = Buffer.from('LFRM');
const FRAME_HEADER_SIZE = 24;
const MAX_WS_BUFFERED_BYTES = 1024 * 1024;

let simProcess = null;
let latestPacket = null;
let latestParsedPacket = null;
let latestParsedSource = null;
let wsClients = new Set();

let currentSize = SIM_SIZE;

function parseFramePacket(packet) {
  if (packet.length < FRAME_HEADER_SIZE) {
    throw new Error('short frame header');
  }
  if (!packet.subarray(0, 4).equals(FRAME_MAGIC)) {
    throw new Error('bad frame magic');
  }

  const version = packet.readUInt32LE(4);
  if (version !== 1) {
    throw new Error(`unsupported frame version ${version}`);
  }

  const metaLen = packet.readUInt32LE(8);
  const cellsLen = packet.readUInt32LE(12);
  const potLen = packet.readUInt32LE(16);
  const fldLen = packet.readUInt32LE(20);
  const metaStart = FRAME_HEADER_SIZE;
  const metaEnd = metaStart + metaLen;
  const cellsEnd = metaEnd + cellsLen;
  const potEnd = cellsEnd + potLen;
  const fldEnd = potEnd + fldLen;

  if (packet.length < fldEnd) {
    throw new Error('short frame payload');
  }

  const meta = JSON.parse(packet.toString('utf8', metaStart, metaEnd));
  return {
    meta,
    cells: packet.subarray(metaEnd, cellsEnd),
    potential: packet.subarray(cellsEnd, potEnd),
    field: packet.subarray(potEnd, fldEnd),
  };
}

function getLatestParsedPacket() {
  if (!latestPacket) return null;
  if (latestParsedSource !== latestPacket) {
    latestParsedPacket = parseFramePacket(latestPacket);
    latestParsedSource = latestPacket;
  }
  return latestParsedPacket;
}

function startSim(code, dim, size) {
  if (simProcess) simProcess.kill();
  if (dim) SIM_DIM = dim;
  if (size) currentSize = size;

  const animalsFile = SIM_DIM === 3
    ? join(LENIA_PY_DIR, 'animals3D.json')
    : ANIMALS;
  const simSize = SIM_DIM === 3 ? Math.min(currentSize, 64) : currentSize;  // 3D needs smaller size

  const args = ['--size', String(simSize), '--fps', String(SIM_FPS), '--dim', String(SIM_DIM)];
  if (existsSync(animalsFile)) args.push('--animals', animalsFile);
  if (code) args.push('--code', code);

  const bin = join(BUILD, 'lenia_server');
  console.log(`Starting: ${bin} ${args.join(' ')}`);
  simProcess = spawn(bin, args, { stdio: ['pipe', 'pipe', 'inherit'] });

  let buffer = Buffer.alloc(0);
  simProcess.stdout.on('data', (chunk) => {
    buffer = buffer.length === 0 ? chunk : Buffer.concat([buffer, chunk]);

    while (buffer.length >= FRAME_HEADER_SIZE) {
      const magicIndex = buffer.indexOf(FRAME_MAGIC);
      if (magicIndex === -1) {
        buffer = Buffer.alloc(0);
        return;
      }
      if (magicIndex > 0) {
        buffer = buffer.subarray(magicIndex);
        if (buffer.length < FRAME_HEADER_SIZE) return;
      }

      const metaLen = buffer.readUInt32LE(8);
      const cellsLen = buffer.readUInt32LE(12);
      const potLen = buffer.readUInt32LE(16);
      const fldLen = buffer.readUInt32LE(20);
      const totalLen = FRAME_HEADER_SIZE + metaLen + cellsLen + potLen + fldLen;

      if (totalLen > 64 * 1024 * 1024) {
        buffer = buffer.subarray(4);
        continue;
      }
      if (buffer.length < totalLen) return;

      const packet = Buffer.from(buffer.subarray(0, totalLen));
      buffer = buffer.subarray(totalLen);

      try {
        latestPacket = packet;
        latestParsedPacket = null;
        latestParsedSource = null;
        for (const ws of wsClients) {
          if (ws.readyState !== 1) continue;
          if (ws.bufferedAmount > MAX_WS_BUFFERED_BYTES) continue;
          ws.send(packet, { binary: true });
        }
      } catch (e) {
        console.log('Skipping malformed frame:', e.message);
      }
    }
  });

  simProcess.on('exit', (code) => {
    console.log(`Sim exited with code ${code}`);
    simProcess = null;
  });
}

function sendCommand(cmd) {
  if (simProcess && simProcess.stdin.writable) {
    simProcess.stdin.write(cmd + '\n');
  }
}

// Load animals list
let animalsList = [];
try {
  const data = JSON.parse(readFileSync(ANIMALS, 'utf-8'));
  animalsList = data.map(d => ({ code: d.code, name: d.name }))
    .filter(d => !d.code.startsWith('>'));
} catch (e) {
  console.log('Could not load animals.json:', e.message);
}

// HTTP server for REST API + static files
const httpServer = createServer((req, res) => {
  const url = new URL(req.url, `http://localhost:${PORT}`);

  // CORS
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
  if (req.method === 'OPTIONS') { res.writeHead(200); res.end(); return; }

  // API routes
  if (url.pathname === '/api/state') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(getLatestParsedPacket()?.meta || { gen: 0, mass: 0, running: false }));
    return;
  }

  if (url.pathname === '/api/animals') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(animalsList));
    return;
  }

  if (url.pathname === '/api/resize' && req.method === 'POST') {
    let body = '';
    req.on('data', c => body += c);
    req.on('end', () => {
      try {
        const { size, code } = JSON.parse(body);
        const newSize = Math.max(32, Math.min(512, parseInt(size)));
        // Restart sim with new size
        startSim(code || null, SIM_DIM, newSize);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, size: newSize }));
      } catch (e) {
        res.writeHead(400);
        res.end(JSON.stringify({ error: e.message }));
      }
    });
    return;
  }

  if (url.pathname === '/api/switch-dim' && req.method === 'POST') {
    let body = '';
    req.on('data', c => body += c);
    req.on('end', () => {
      try {
        const { dim, code } = JSON.parse(body);
        startSim(code || null, dim);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, dim }));
      } catch (e) {
        res.writeHead(400);
        res.end(JSON.stringify({ error: e.message }));
      }
    });
    return;
  }

  if (url.pathname === '/api/command' && req.method === 'POST') {
    let body = '';
    req.on('data', c => body += c);
    req.on('end', () => {
      try {
        const { cmd } = JSON.parse(body);
        sendCommand(cmd);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true }));
      } catch (e) {
        res.writeHead(400);
        res.end(JSON.stringify({ error: e.message }));
      }
    });
    return;
  }

  if (url.pathname === '/api/params' && req.method === 'POST') {
    let body = '';
    req.on('data', c => body += c);
    req.on('end', () => {
      try {
        const params = JSON.parse(body);
        for (const [key, val] of Object.entries(params)) {
          sendCommand(`set_${key} ${val}`);
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true }));
      } catch (e) {
        res.writeHead(400);
        res.end(JSON.stringify({ error: e.message }));
      }
    });
    return;
  }

  // Save/load user patterns
  if (url.pathname === '/api/save-pattern' && req.method === 'POST') {
    let body = '';
    req.on('data', c => body += c);
    req.on('end', () => {
      try {
        const { name } = JSON.parse(body);
        const parsed = getLatestParsedPacket();
        const frame = parsed?.meta;
        if (!frame) { res.writeHead(400); res.end(JSON.stringify({error:'no frame'})); return; }
        const pattern = { name: name || 'unnamed', code: 'user_' + Date.now(),
          params: frame.params,
          cells_b64: parsed?.cells ? parsed.cells.toString('base64') : null,
          width: frame.width, height: frame.height,
          saved_at: new Date().toISOString(), mass: frame.mass };
        // Append to saved.json
        const savedPath = join(ROOT, 'saved_patterns.json');
        let saved = [];
        try { saved = JSON.parse(readFileSync(savedPath, 'utf-8')); } catch(e) {}
        saved.push(pattern);
        writeFileSync(savedPath, JSON.stringify(saved, null, 2));
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, count: saved.length }));
      } catch(e) {
        res.writeHead(400); res.end(JSON.stringify({ error: e.message }));
      }
    });
    return;
  }

  if (url.pathname === '/api/load-pattern' && req.method === 'POST') {
    let body = '';
    req.on('data', c => body += c);
    req.on('end', () => {
      try {
        const { params } = JSON.parse(body);
        // Set params then random to reset
        if (params) {
          sendCommand(`set_R ${params.R}`);
          sendCommand(`set_T ${params.T}`);
          sendCommand(`set_m ${params.m}`);
          sendCommand(`set_s ${params.s}`);
          sendCommand(`set_kn ${params.kn}`);
          sendCommand(`set_gn ${params.gn}`);
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true }));
      } catch(e) {
        res.writeHead(400); res.end(JSON.stringify({ error: e.message }));
      }
    });
    return;
  }

  if (url.pathname === '/api/saved-patterns') {
    const savedPath = join(ROOT, 'saved_patterns.json');
    try {
      const data = readFileSync(savedPath, 'utf-8');
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(data);
    } catch(e) {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end('[]');
    }
    return;
  }

  // Serve static files from dist/
  let filePath = url.pathname === '/' ? '/index.html' : url.pathname;
  filePath = join(DIST, filePath);
  const extMap = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css',
                   '.png': 'image/png', '.svg': 'image/svg+xml', '.json': 'application/json' };
  const ext = filePath.match(/\.\w+$/)?.[0] || '.html';
  try {
    const content = readFileSync(filePath);
    res.writeHead(200, { 'Content-Type': extMap[ext] || 'application/octet-stream' });
    res.end(content);
  } catch (e) {
    // SPA fallback
    try {
      res.writeHead(200, { 'Content-Type': 'text/html' });
      res.end(readFileSync(join(DIST, 'index.html')));
    } catch (e2) {
      res.writeHead(404);
      res.end('Not found');
    }
  }
});

// WebSocket server for real-time frame streaming
const wss = new WebSocketServer({ server: httpServer });
wss.on('connection', (ws) => {
  wsClients.add(ws);
  console.log(`WS client connected (${wsClients.size} total)`);

  ws.on('message', (msg) => {
    const cmd = msg.toString().trim();
    sendCommand(cmd);
  });

  ws.on('close', () => {
    wsClients.delete(ws);
    console.log(`WS client disconnected (${wsClients.size} total)`);
  });

  // Send current state immediately
  if (latestPacket) ws.send(latestPacket, { binary: true });
});

httpServer.listen(PORT, () => {
  console.log(`Lenia dashboard at http://localhost:${PORT}`);
  startSim('O2u');
});
