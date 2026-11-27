// Genere des WAV de test 48 kHz stereo 16 bits (node tools/audio-test/gen.js <dossier>).
const fs = require('fs');
const path = require('path');
const outDir = process.argv[2] || '.';
const R = 48000;
function wav(name, samples) {
  const n = samples.length, buf = Buffer.alloc(44 + n * 4);
  buf.write('RIFF', 0); buf.writeUInt32LE(36 + n * 4, 4); buf.write('WAVE', 8); buf.write('fmt ', 12);
  buf.writeUInt32LE(16, 16); buf.writeUInt16LE(1, 20); buf.writeUInt16LE(2, 22); buf.writeUInt32LE(R, 24);
  buf.writeUInt32LE(R * 4, 28); buf.writeUInt16LE(4, 32); buf.writeUInt16LE(16, 34); buf.write('data', 36); buf.writeUInt32LE(n * 4, 40);
  for (let i = 0; i < n; i++) { const v = Math.max(-1, Math.min(1, samples[i])) * 32767 | 0; buf.writeInt16LE(v, 44 + i * 4); buf.writeInt16LE(v, 46 + i * 4); }
  fs.writeFileSync(path.join(outDir, name), buf);
}
const secs = 5, N = R * secs;
const sine = (f, a) => Float32Array.from({ length: N }, (_, i) => a * Math.sin(2 * Math.PI * f * i / R));
wav('sine100.wav', sine(100, 0.25));
wav('sine1k.wav', sine(1000, 0.25));
wav('sine8k.wav', sine(8000, 0.25));
// Bruit rose (Paul Kellet)
{ let b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0; const out = new Float32Array(N);
  for (let i = 0; i < N; i++) { const w = Math.random()*2-1;
    b0=0.99886*b0+w*0.0555179; b1=0.99332*b1+w*0.0750759; b2=0.96900*b2+w*0.1538520; b3=0.86650*b3+w*0.3104856;
    b4=0.55000*b4+w*0.5329522; b5=-0.7616*b5-w*0.0168980; out[i]=(b0+b1+b2+b3+b4+b5+b6+w*0.5362)*0.05; b6=w*0.115926; }
  wav('pink.wav', out); }
// Kick 120 BPM + charleston sur les contretemps, 8 s
function drums(bpm, secs2, name) {
  const n = R * secs2, out = new Float32Array(n), beat = 60 / bpm;
  for (let t0 = 0; t0 < secs2; t0 += beat) {
    const s = Math.floor(t0 * R);
    let phase = 0;
    for (let k = 0; k < R * 0.25 && s + k < n; k++) { const t = k / R; const f = 50 + 100 * Math.exp(-t * 30); phase += 2 * Math.PI * f / R;
      out[s + k] += 0.6 * Math.sin(phase) * Math.exp(-t * 12); }
    const h = Math.floor((t0 + beat / 2) * R);
    for (let k = 0; k < R * 0.04 && h + k < n; k++) out[h + k] += 0.08 * (Math.random() * 2 - 1) * Math.exp(-k / R * 120);
  }
  wav(name, out);
}
drums(120, 10, 'kick120.wav');
drums(128, 10, 'kick128.wav');
// Source calme : 440 Hz a -36 dBFS, 12 s
{ const n = R * 12; wav('quiet440.wav', Float32Array.from({ length: n }, (_, i) => 0.016 * Math.sin(2 * Math.PI * 440 * i / R))); }
