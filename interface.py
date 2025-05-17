#!/usr/bin/env python
"""
All-in-one: virtual-env bootstrap ➜ Flask server ➜ dark-mode WebSerial
heat-map UI for four load-cell pressures.

Key features
────────────
• Auto-creates “venv”, installs Flask, then relaunches itself inside it.
• JS WebSerial lets the user pick a port + baudrate (default 115 200 baud).
• Incoming lines look like   7.66,2.80,-357.10,2.52
  (four comma-separated pressures, zero-indexed).
• Diamond-layout heat-map pads:
        [0]
     [1]   [2]
        [3]
  – colour-gradient blue(≈0 kPa) → red(≤-500 kPa).
• Click a pad to begin assignment mode ➜ press any sensor harder
  than -200 kPa for ≥3 s → that sensor-index is bound to the pad
  (persisted in localStorage).  “Clear Cache” wipes all bindings.
• Clean rounded UI, monospace font, full dark-mode.

Upload your own MCU firmware that streams the four comma-separated
values at the chosen baudrate (115 200 recommended).
"""
import sys, os, subprocess, platform, textwrap

# ── 1.  venv bootstrap ─────────────────────────────────────────────────────────
def ensure_venv():
    if os.environ.get("RUNNING_IN_VENV"):
        return
    if not os.path.exists("venv"):
        print("Creating virtual environment …")
        subprocess.check_call([sys.executable, "-m", "venv", "venv"])
    pip = "pip.exe" if platform.system() == "Windows" else "pip"
    python = "python.exe" if platform.system() == "Windows" else "python"
    pip_path = os.path.join("venv", "Scripts" if platform.system()=="Windows" else "bin", pip)
    py_path  = os.path.join("venv", "Scripts" if platform.system()=="Windows" else "bin", python)
    print("Installing Flask …")
    subprocess.check_call([pip_path, "install", "flask"])
    os.environ["RUNNING_IN_VENV"] = "1"
    subprocess.check_call([py_path] + sys.argv)
    sys.exit()

ensure_venv()

# ── 2.  Flask server ───────────────────────────────────────────────────────────
from flask import Flask, render_template_string, Response
app = Flask(__name__)

# (Optional) reference sketch kept for completeness ↓
esp32_code = textwrap.dedent(r"""
// Stream "7.66,2.80,-357.10,2.52"-style lines at 115200 baud.
// Replace with your own firmware as needed.
""").strip()

# ── 3.  HTML template ─────────────────────────────────────────────────────────-
html_template = r"""
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Load-Cell Heat-Map</title>
<style>
:root{color-scheme:dark light;}
body{
    margin:0;padding:2rem;
    font:16px/1.4 "Fira Mono","Courier New",monospace;
    background:#121212;color:#eee;
}
h1{margin-top:0;margin-bottom:1rem;font-size:1.5rem;text-align:center}
.controls{display:flex;gap:1rem;justify-content:center;flex-wrap:wrap;margin-bottom:1.5rem}
button,select{
    background:#1e1e1e;border:1px solid #444;border-radius:8px;
    padding:.6rem 1.2rem;color:#eee;font:inherit;cursor:pointer
}
button:hover,select:hover{background:#272727}
#rate{font-weight:bold}
.heatmap{
   --size:160px;
   display:grid;grid-template-areas:
      ".  top  ."
      "left .  right"
      ". bottom .";
   grid-gap:1.5rem;justify-content:center;align-items:center;
}
.pad{
   width:var(--size);height:var(--size);
   background:#2b2b2b;border-radius:20px;display:flex;
   align-items:center;justify-content:center;position:relative;
   transition:background .15s ease;
   user-select:none
}
.pad.selected{outline:4px solid #fff}
.pad span{
   position:absolute;bottom:.4rem;left:0;right:0;
   text-align:center;font-size:.9rem;pointer-events:none
}
#pad0{grid-area:top}#pad1{grid-area:left}#pad2{grid-area:right}#pad3{grid-area:bottom}
a{color:#8ab4ff}
</style>
</head>
<body>
<h1>Load-Cell Heat-Map</h1>

<div class="controls">
    <button id="connectBtn">🔌 Connect Port</button>
    <label>Baud:
        <select id="baudSel">
            <option>9600</option><option>57600</option>
            <option selected>115200</option>
            <option>230400</option><option>460800</option>
        </select>
    </label>
    <div id="rateBox">Rate: <span id="rate">0</span> Hz</div>
    <button id="clearBtn">🗑 Clear Cache</button>
</div>

<div class="heatmap">
    <div class="pad" id="pad0"><span id="val0">—</span></div>
    <div class="pad" id="pad1"><span id="val1">—</span></div>
    <div class="pad" id="pad2"><span id="val2">—</span></div>
    <div class="pad" id="pad3"><span id="val3">—</span></div>
</div>

<script>
/* ─── Utility ─────────────────────────────────────────────── */
const clamp = (v,min,max)=>Math.max(min,Math.min(max,v));
const map   = (v,inMin,inMax,outMin,outMax)=>outMin+(outMax-outMin)*(v-inMin)/(inMax-inMin);

/* ─── LocalStorage helpers ───────────────────────────────── */
const LS_KEY = "padMap";
function loadMap(){
    try{return JSON.parse(localStorage.getItem(LS_KEY))||[0,1,2,3]}catch{ return [0,1,2,3];}
}
function saveMap(arr){localStorage.setItem(LS_KEY,JSON.stringify(arr));}

/* ─── Globals ────────────────────────────────────────────── */
let padMap = loadMap();              // panel-index ➜ sensor-index
let assigningPad = null;             // which pad the user clicked
let pressTimers = Array(4).fill(null);
let port, reader;
let fpsCount=0,lastT=performance.now();

/* ─── Colour mapping: 0 kPa (blue) → -500 kPa (red) ──────── */
function pressureToColor(p){
    const hue = map(clamp(p,-500,0),0,-500,240,0); // 240° blue → 0° red
    return `hsl(${hue},100%,50%)`;
}

/* ─── UI setup ───────────────────────────────────────────── */
const pads=[...document.querySelectorAll(".pad")];
pads.forEach((pad,i)=>{
    pad.addEventListener("click",()=>{
        if(assigningPad===i){assigningPad=null;pad.classList.remove("selected");return;}
        pads.forEach(p=>p.classList.remove("selected"));
        assigningPad=i;pad.classList.add("selected");
    });
});

document.getElementById("clearBtn").onclick=()=>{
    localStorage.removeItem(LS_KEY);
    padMap=[0,1,2,3];
    alert("Cache cleared – mapping reset.");
};

async function connectSerial(){
    try{
        port = await navigator.serial.requestPort();
        await port.open({ baudRate: +document.getElementById("baudSel").value });
        const dec = new TextDecoderStream();
        port.readable.pipeTo(dec.writable);
        reader = dec.readable.getReader();
        readLoop();
    }catch(e){console.error(e);}
}

document.getElementById("connectBtn").onclick=connectSerial;

/* ─── Read loop ──────────────────────────────────────────── */
async function readLoop(){
    let buffer="";
    for(;;){
        const {value,done}=await reader.read();
        if(done)break;
        buffer+=value;
        let lines=buffer.split("\n");
        buffer=lines.pop();
        lines.forEach(handleLine);
    }
}

function handleLine(line){
    const parts=line.trim().split(",");
    if(parts.length<4)return;
    const vals=parts.map(v=>parseFloat(v));
    updateRate();
    handleAssignment(vals);
    updatePads(vals);
}

function updateRate(){
    fpsCount++;
    const now=performance.now();
    if(now-lastT>1000){
        document.getElementById("rate").textContent=fpsCount;
        fpsCount=0;lastT=now;
    }
}

function handleAssignment(vals){
    if(assigningPad===null)return;
    const now=performance.now();
    vals.forEach((v,idx)=>{
        if(v<=-100){
            if(!pressTimers[idx])pressTimers[idx]=now;
            else if(now-pressTimers[idx]>=2000){
                padMap[assigningPad]=idx;
                saveMap(padMap);
                pads[assigningPad].classList.remove("selected");
                assigningPad=null;
                pressTimers=Array(4).fill(null);
                alert(`Pad ${assigningPad+1} bound to sensor ${idx}`);
            }
        }else pressTimers[idx]=null;
    });
}

function updatePads(vals){
    for(let p=0;p<4;p++){
        const sensor=padMap[p];
        const val=vals[sensor];
        document.getElementById(`val${p}`).textContent=val.toFixed(2);
        document.getElementById(`pad${p}`).style.background=pressureToColor(val);
    }
}

/* ─── Feature detection ─────────────────────────────────── */
if(!("serial" in navigator)){
    alert("WebSerial unsupported in this browser.");
}
</script>
</body>
</html>
"""

# ── 4.  Flask routes ──────────────────────────────────────────────────────────
@app.route("/")
def index(): return render_template_string(html_template)

@app.route("/esp32-code")
def code(): return Response(esp32_code, mimetype="text/plain")

# ── 5.  Run server ────────────────────────────────────────────────────────────
if __name__=="__main__":
    print("Serving on http://0.0.0.0:5000 …")
    app.run(host="0.0.0.0",port=5000,debug=True)
