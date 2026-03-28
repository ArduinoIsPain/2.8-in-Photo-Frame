#include "upload_mode.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include "Pins.h"

// ---------- Config ----------
static const char* AP_PASS = "12345678";  // 8+ chars required
static const int   AP_CH   = 6;

// ---------- Module state ----------
static WebServer s_server(80);
static bool      s_active = false;
static uint32_t  s_uploaded = 0;

static File      s_uploadFile;
static String    s_uploadPath;

static String    s_apSsid;
static IPAddress s_ip;

// ---------- Small HTML uploader ----------
// Browser converts JPG/PNG/HEIC(if supported by browser) to 320x240 JPEG and uploads to /upload.
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>PhotoFrame Upload</title>
  <style>
    :root{
      --bg:#0f1216;
      --panel:#141922;
      --muted:#9aa4b2;
      --text:#eef2f7;
      --border:#2a3240;
      --purple:#f5d682;
      --purple2:#ffe9a6;
      --ok:#4ade80;
      --bad:#fb7185;
    }
    *{box-sizing:border-box}
    body{
      margin:0;
      font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;
      background: radial-gradient(1200px 600px at 20% -10%, rgba(245,214,130,.22), transparent 55%),
                  radial-gradient(900px 500px at 90% 0%, rgba(255,233,166,.14), transparent 60%),
                  var(--bg);
      color:var(--text);
    }
    .wrap{max-width:980px;margin:0 auto;padding:24px 16px 36px;}
    .top{
      display:flex;align-items:flex-end;justify-content:space-between;gap:16px;
      padding:12px 6px 18px;border-bottom:1px solid rgba(255,255,255,.06);margin-bottom:18px;
    }
    .title{font-size:26px;font-weight:800;letter-spacing:.2px;}
    .sub{color:var(--muted);font-size:14px;line-height:1.35;max-width:720px;}
    .badge{
      display:inline-flex;align-items:center;gap:8px;font-size:12px;color:var(--muted);
      padding:8px 10px;border:1px solid rgba(122,60,255,.35);background:rgba(122,60,255,.08);
      border-radius:999px;white-space:nowrap;
    }
    .dot{width:10px;height:10px;border-radius:50%;background:var(--purple);box-shadow:0 0 0 4px rgba(122,60,255,.15);}
    .grid{display:grid;grid-template-columns:1.25fr .75fr;gap:16px;margin-top:18px;}
    @media (max-width:860px){.grid{grid-template-columns:1fr}}
    .card{
      background: rgba(20,25,34,.72);
      border: 1px solid rgba(255,255,255,.07);
      border-radius: 18px;
      padding: 16px;
      box-shadow: 0 10px 30px rgba(0,0,0,.35);
      backdrop-filter: blur(6px);
    }
    .drop{
      border: 2px dashed rgba(245,214,130,.55);
      background: linear-gradient(180deg, rgba(122,60,255,.10), rgba(122,60,255,.03));
      border-radius: 16px;
      padding: 16px;
      display:flex;
      flex-direction:column;
      gap:12px;
      transition: .15s ease;
    }
    .drop.dragover{
      border-color: rgba(255,233,166,.90);
      background: linear-gradient(180deg, rgba(255,233,166,.18), rgba(245,214,130,.08));
      transform: translateY(-1px);
    }
    .row{display:flex;gap:12px;flex-wrap:wrap;align-items:center;}
    input[type=file]{
      color: var(--muted);
      background: rgba(0,0,0,.25);
      border: 1px solid rgba(255,255,255,.08);
      border-radius: 12px;
      padding: 10px;
      width: min(560px, 100%);
    }
    button{
      appearance:none;
      border: 1px solid rgba(245,214,130,.70);
      background: linear-gradient(180deg, rgba(245,214,130,.95), rgba(245,214,130,.65));
      color: white;
      font-weight: 700;
      font-size: 15px;
      padding: 11px 14px;
      border-radius: 12px;
      cursor:pointer;
      transition: .12s ease;
      box-shadow: 0 10px 20px rgba(245,214,130,.16);
    }
    button.secondary{
      border: 1px solid rgba(255,255,255,.12);
      background: rgba(0,0,0,.20);
      box-shadow:none;
      color: var(--text);
    }
    button:disabled{opacity:.55;cursor:not-allowed;box-shadow:none;}
    button:hover{transform: translateY(-1px);}
    button:active{transform: translateY(0px);}
    .pill{
      display:inline-flex;align-items:center;gap:8px;
      padding: 10px 12px;border-radius: 12px;
      border: 1px solid rgba(255,255,255,.08);
      background: rgba(0,0,0,.25);
      color: var(--muted);
      font-size: 13px;
    }
    code{
      color: var(--text);
      background: rgba(255,255,255,.06);
      padding: 2px 8px;
      border-radius: 999px;
      border: 1px solid rgba(255,255,255,.08);
    }
    .statusLine{
      display:flex;gap:12px;flex-wrap:wrap;align-items:center;justify-content:space-between;margin-top:10px;
    }
    progress{
      width: 100%;
      height: 16px;
      border-radius: 999px;
      overflow:hidden;
      border: 1px solid rgba(255,255,255,.10);
      background: rgba(0,0,0,.35);
    }
    progress::-webkit-progress-bar{background: rgba(0,0,0,.35);}
    progress::-webkit-progress-value{
      background: linear-gradient(90deg, rgba(122,60,255,.9), rgba(180,140,255,.9));
    }
    .log{
      margin-top: 10px;
      font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
      font-size: 12.5px;
      color: rgba(238,242,247,.92);
      background: rgba(0,0,0,.35);
      border: 1px solid rgba(255,255,255,.08);
      border-radius: 14px;
      padding: 12px;
      white-space: pre-wrap;
      line-height: 1.35;
      max-height: 240px;
      overflow:auto;
    }
    .ok{color: var(--ok);}
    .bad{color: var(--bad);}
    .muted{color: var(--muted);}
    .sideTitle{font-size:14px;font-weight:800;letter-spacing:.2px;margin-bottom:10px;color:var(--text);}
    .help{color: var(--muted);font-size:13px;line-height:1.45;}
    .help b{color: var(--text);}
    .sep{height:1px;background:rgba(255,255,255,.07);margin:12px 0}

    /* Modal */
    .modal{
      display:none; position:fixed; inset:0; background:rgba(0,0,0,.75);
      padding:18px; z-index:9999;
    }
    .modalCard{
      max-width:980px;margin:0 auto;
      background: rgba(20,25,34,.95);
      border: 1px solid rgba(255,255,255,.08);
      border-radius: 18px;
      padding: 14px;
      box-shadow: 0 14px 36px rgba(0,0,0,.55);
    }
    .modalTop{
      display:flex; justify-content:space-between; align-items:center; gap:12px; margin-bottom:10px; flex-wrap:wrap;
    }
    .modalTitle{font-weight:900; letter-spacing:.2px;}
    .modalHint{color:var(--muted); font-size:13px}
    .modalGrid{display:flex; gap:14px; flex-wrap:wrap; align-items:flex-start;}
    .cropWrap{flex:1 1 640px; min-width:320px;}
    #cropCanvas{
      width:100%;
      aspect-ratio: 320 / 240;
      border-radius:14px;
      border:1px solid rgba(122,60,255,.45);
      background:#000;
      cursor: grab;
      user-select:none;
      touch-action:none;
    }
    #cropCanvas.dragging{cursor: grabbing;}
    .sideWrap{flex:0 1 280px; min-width:240px;}
    select{
      width:100%;
      border-radius:12px;
      padding:10px;
      background:rgba(0,0,0,.25);
      color:var(--text);
      border:1px solid rgba(255,255,255,.08);
    }
    .kbar{
      display:flex; gap:10px; align-items:center; flex-wrap:wrap;
      padding: 10px 12px;
      border-radius: 12px;
      border: 1px solid rgba(255,255,255,.08);
      background: rgba(0,0,0,.25);
    }
    input[type=range]{width:220px;}
    .miniKey{
      display:inline-flex; align-items:center; justify-content:center;
      padding: 2px 8px;
      border:1px solid rgba(255,255,255,.10);
      border-radius: 8px;
      font-size: 12px;
      color: var(--muted);
      background: rgba(255,255,255,.04);
    }
  </style>
</head>

<body>
  <div class="wrap">
    <div class="top">
      <div>
        <div class="title">Upload Photos</div>
        <div class="sub">
          Pick images from your phone or computer. This page converts them to <b>JPEG</b> and uploads them to the frame.
          Default is <b>Fill / Crop</b> so every photo fills the screen. Use <b>Crop Selected</b> if a face is cut off.
        </div>
      </div>
      <div class="badge"><span class="dot"></span> PhotoFrame Upload</div>
    </div>

    <div class="grid">
      <div class="card">
        <div id="drop" class="drop">
          <div class="row">
            <input id="files" type="file" multiple accept="image/*"/>
            <button id="cropBtn" class="secondary" type="button">Crop Selected</button>
            <button id="go" type="button">Convert & Upload</button>
          </div>

          <div class="row">
            <span class="pill">Output: <code>320×240</code></span>
            <span class="pill">Format: <code>JPEG</code></span>
            <span class="pill">Mode: <code>Fill / Crop</code></span>
          </div>

          <div class="statusLine">
            <div class="muted" id="status">Idle</div>
          </div>

          <progress id="prog" value="0" max="100"></progress>
          <div id="log" class="log"></div>
        </div>
      </div>

      <div class="card">
        <div class="sideTitle">Notes</div>
        <div class="help">
          • Works best with <b>JPG</b> / <b>PNG</b>.<br/>
          • Some browsers can’t decode <b>HEIC</b> — if it fails, export/share as JPG first.<br/>
          <div class="sep"></div>
          • Use <b>Crop Selected</b> for tricky portraits, then hit <b>Next</b> to fix a batch quickly.<br/>
          • Filenames are cleaned automatically.<br/>
        </div>
      </div>
    </div>
  </div>

  <!-- Crop Modal -->
  <div id="cropModal" class="modal">
    <div class="modalCard">
      <div class="modalTop">
        <div>
          <div class="modalTitle">Adjust Crop</div>
          <div class="modalHint">Drag to reposition • Zoom to scale • Defaults apply if you don’t touch it</div>
        </div>

        <div class="kbar">
          <span class="modalHint">Zoom</span>
          <input id="cropZoom" type="range" min="1" max="2.6" value="1.15" step="0.01"/>
          <button id="prevBtn" class="secondary" type="button">Prev</button>
          <button id="nextBtn" class="secondary" type="button">Next</button>
          <button id="cropReset" class="secondary" type="button">Reset</button>
          <button id="cropDone" type="button">Done</button>
          <span class="miniKey">←/→</span><span class="miniKey">N</span><span class="miniKey">P</span><span class="miniKey">R</span><span class="miniKey">Enter</span>
        </div>
      </div>

      <div class="modalGrid">
        <div class="cropWrap">
          <canvas id="cropCanvas" width="320" height="240"></canvas>
          <div class="modalHint" style="margin-top:8px;">
            Tip: for portraits, zoom ~1.25 and drag up a bit to keep faces centered.
          </div>
        </div>

        <div class="sideWrap">
          <div style="color:var(--text); font-weight:700; margin-bottom:8px;">Files</div>
          <select id="cropSelect" size="10"></select>
          <div class="modalHint" style="margin-top:8px;">
            Select a file, adjust crop, then hit <b>Next</b>.
          </div>
        </div>
      </div>
    </div>
  </div>

<script>
const W = 320, H = 240;

const drop = document.getElementById('drop');
const fileInput = document.getElementById('files');
const goBtn = document.getElementById('go');
const cropBtn = document.getElementById('cropBtn');

const cropModal = document.getElementById('cropModal');
const cropCanvas = document.getElementById('cropCanvas');
const cropCtx = cropCanvas.getContext('2d');
const cropZoom = document.getElementById('cropZoom');
const cropSelect = document.getElementById('cropSelect');
const cropDone = document.getElementById('cropDone');
const cropReset = document.getElementById('cropReset');
const nextBtn = document.getElementById('nextBtn');
const prevBtn = document.getElementById('prevBtn');

function setStatus(s){ document.getElementById('status').textContent = s; }
function setProg(p){ document.getElementById('prog').value = p; }

function logLine(html){
  const el = document.getElementById('log');
  el.innerHTML += html + "<br/>";
  el.scrollTop = el.scrollHeight;
}

// Per-file crop settings: { zoom, panX, panY } where panX/panY are in output pixels.
const cropSettings = new Map();

// Modal state
let cropImg = null;
let cropKey = null;        // file.name
let isDragging = false;
let lastX = 0, lastY = 0;

function getFilesArray(){
  const files = fileInput.files;
  return files ? Array.from(files) : [];
}

function ensureDefaultCrop(file){
  const key = file.name;
  if (!cropSettings.has(key)) cropSettings.set(key, { zoom: 1.15, panX: 0, panY: 0 });
}

async function loadImageForFile(file){
  const url = URL.createObjectURL(file);
  const img = new Image();
  img.decoding = "async";
  const loaded = new Promise((res, rej)=>{ img.onload=res; img.onerror=rej; });
  img.src = url;
  await loaded;
  URL.revokeObjectURL(url);
  return img;
}

function drawCropPreview(){
  if (!cropImg || !cropKey) return;

  const s = cropSettings.get(cropKey) || { zoom: 1.0, panX: 0, panY: 0 };
  cropCtx.clearRect(0,0,W,H);

  const base = Math.max(W / cropImg.width, H / cropImg.height);
  const scale = base * s.zoom;

  const dw = Math.round(cropImg.width * scale);
  const dh = Math.round(cropImg.height * scale);
  const dx = Math.floor((W - dw)/2 + s.panX);
  const dy = Math.floor((H - dh)/2 + s.panY);

  cropCtx.drawImage(cropImg, dx, dy, dw, dh);
}

function selectCropIndex(idx){
  const files = getFilesArray();
  if (!files.length) return;
  idx = Math.max(0, Math.min(idx, files.length - 1));
  cropSelect.selectedIndex = idx;
  cropSelect.dispatchEvent(new Event("change"));
}

async function openCropModal(){
  const files = getFilesArray();
  if (!files.length){
    alert("Pick files first.");
    return;
  }

  cropSelect.innerHTML = "";
  for (const f of files){
    ensureDefaultCrop(f);
    const opt = document.createElement("option");
    opt.value = f.name;
    opt.textContent = f.name;
    cropSelect.appendChild(opt);
  }

  cropModal.style.display = "block";
  selectCropIndex(0);
}

function closeCropModal(){ cropModal.style.display = "none"; }

cropBtn.onclick = openCropModal;
cropDone.onclick = closeCropModal;

cropReset.onclick = () => {
  if (!cropKey) return;
  cropSettings.set(cropKey, { zoom: 1.15, panX: 0, panY: 0 });
  cropZoom.value = 1.15;
  drawCropPreview();
};

nextBtn.onclick = () => selectCropIndex(cropSelect.selectedIndex + 1);
prevBtn.onclick = () => selectCropIndex(cropSelect.selectedIndex - 1);

cropSelect.onchange = async () => {
  const files = getFilesArray();
  const name = cropSelect.value;
  const f = files.find(x => x.name === name);
  if (!f) return;

  cropKey = f.name;
  cropImg = await loadImageForFile(f);

  cropZoom.value = (cropSettings.get(cropKey)?.zoom || 1.15);
  drawCropPreview();
};

cropZoom.oninput = () => {
  if (!cropKey) return;
  const s = cropSettings.get(cropKey) || { zoom: 1.0, panX: 0, panY: 0 };
  s.zoom = parseFloat(cropZoom.value);
  cropSettings.set(cropKey, s);
  drawCropPreview();
};

// Drag-to-pan (mouse)
cropCanvas.addEventListener("mousedown", (e)=>{
  if (!cropKey) return;
  isDragging = true;
  cropCanvas.classList.add("dragging");
  lastX = e.clientX;
  lastY = e.clientY;
});
window.addEventListener("mouseup", ()=>{
  isDragging = false;
  cropCanvas.classList.remove("dragging");
});
window.addEventListener("mousemove", (e)=>{
  if (!isDragging || !cropKey) return;
  const dx = e.clientX - lastX;
  const dy = e.clientY - lastY;
  lastX = e.clientX;
  lastY = e.clientY;

  const s = cropSettings.get(cropKey) || { zoom: 1.0, panX: 0, panY: 0 };
  const rect = cropCanvas.getBoundingClientRect();
  const sx = W / rect.width;
  const sy = H / rect.height;
  s.panX += dx * sx;
  s.panY += dy * sy;
  cropSettings.set(cropKey, s);
  drawCropPreview();
});

// Touch drag (mobile)
cropCanvas.addEventListener("touchstart", (e)=>{
  if (!cropKey) return;
  if (!e.touches || e.touches.length !== 1) return;
  e.preventDefault();
  isDragging = true;
  cropCanvas.classList.add("dragging");
  lastX = e.touches[0].clientX;
  lastY = e.touches[0].clientY;
}, {passive:false});

cropCanvas.addEventListener("touchend", ()=>{
  isDragging = false;
  cropCanvas.classList.remove("dragging");
});

cropCanvas.addEventListener("touchmove", (e)=>{
  if (!isDragging || !cropKey) return;
  if (!e.touches || e.touches.length !== 1) return;
  e.preventDefault();

  const cx = e.touches[0].clientX;
  const cy = e.touches[0].clientY;

  const dx = cx - lastX;
  const dy = cy - lastY;
  lastX = cx;
  lastY = cy;

  const s = cropSettings.get(cropKey) || { zoom: 1.0, panX: 0, panY: 0 };
  const rect = cropCanvas.getBoundingClientRect();
  const sx = W / rect.width;
  const sy = H / rect.height;

  s.panX += dx * sx;
  s.panY += dy * sy;
  cropSettings.set(cropKey, s);
  drawCropPreview();
}, {passive:false});

// Keyboard shortcuts while modal open
window.addEventListener("keydown", (e)=>{
  if (cropModal.style.display !== "block") return;
  if (e.key === "ArrowRight" || e.key.toLowerCase() === "n") { e.preventDefault(); nextBtn.click(); }
  if (e.key === "ArrowLeft"  || e.key.toLowerCase() === "p") { e.preventDefault(); prevBtn.click(); }
  if (e.key === "Enter") { e.preventDefault(); closeCropModal(); }
  if (e.key.toLowerCase() === "r") { e.preventDefault(); cropReset.click(); }
});

// Render one file -> 320x240 JPEG blob
async function imageFileToJpegBlob(file){
  const img = await loadImageForFile(file);

  ensureDefaultCrop(file);
  const s = cropSettings.get(file.name) || { zoom: 1.15, panX: 0, panY: 0 };

  const canvas = document.createElement('canvas');
  canvas.width = W; canvas.height = H;
  const ctx = canvas.getContext('2d', {willReadFrequently:false});

  // Fill/crop: scale so both dimensions cover output
  const base = Math.max(W / img.width, H / img.height);
  const scale = base * s.zoom;

  const dw = Math.round(img.width * scale);
  const dh = Math.round(img.height * scale);
  const dx = Math.floor((W - dw)/2 + s.panX);
  const dy = Math.floor((H - dh)/2 + s.panY);

  ctx.fillStyle = "#000";
  ctx.fillRect(0,0,W,H);
  ctx.drawImage(img, dx, dy, dw, dh);

  const blob = await new Promise((resolve, reject)=>{
    canvas.toBlob((b)=> b ? resolve(b) : reject(new Error("toBlob failed")), "image/jpeg", 0.90);
  });

  return blob;
}

function cleanBaseName(name, i){
  const base = (name || ("photo_"+i)).replace(/\.[^.]+$/, "");
  return base.replace(/[^a-zA-Z0-9_-]/g,"_");
}

async function uploadJpeg(blob, outName){
  const fd = new FormData();
  fd.append("file", blob, outName);

  const resp = await fetch("/upload", { method:"POST", body: fd });
  if(!resp.ok) throw new Error("HTTP " + resp.status);
  return (await resp.text()).trim();
}

async function runUpload(files){
  if(!files.length){ alert("Pick files first."); return; }

  goBtn.disabled = true;
  document.getElementById('log').innerHTML = "";
  setProg(0);

  let ok = 0;
  for(let i=0; i<files.length; i++){
    const f = files[i];
    const outName = cleanBaseName(f.name, i) + ".jpg";

    setStatus(`Converting ${i+1}/${files.length}: ${f.name}`);
    try{
      const jpg = await imageFileToJpegBlob(f);
      setStatus(`Uploading ${i+1}/${files.length}: ${outName}`);
      const txt = await uploadJpeg(jpg, outName);

      if (txt && txt.toUpperCase() !== "OK") {
        logLine(`<span class="ok">OK</span> ${outName} <span class="muted">(${txt})</span>`);
      } else {
        logLine(`<span class="ok">OK</span> ${outName}`);
      }
      ok++;
    }catch(e){
      logLine(`<span class="bad">FAIL</span> ${f.name} <span class="muted">(${e})</span>`);
    }
    setProg(Math.round(((i+1)/files.length)*100));
  }

  setStatus(`Done. Uploaded ${ok}/${files.length}.`);
  goBtn.disabled = false;
}

goBtn.onclick = async () => { await runUpload(getFilesArray()); };

// Drag & drop
drop.addEventListener('dragover', (e)=>{ e.preventDefault(); drop.classList.add('dragover'); });
drop.addEventListener('dragleave', ()=> drop.classList.remove('dragover'));
drop.addEventListener('drop', async (e)=>{
  e.preventDefault();
  drop.classList.remove('dragover');
  if(e.dataTransfer && e.dataTransfer.files){
    fileInput.files = e.dataTransfer.files;
    await runUpload(getFilesArray());
  }
});
</script>
</body>
</html>
)HTML";

// ---------- Helpers ----------
static String make_ap_ssid() {
  uint64_t mac = ESP.getEfuseMac();
  uint16_t last = (uint16_t)(mac & 0xFFFF);
  char buf[32];
  snprintf(buf, sizeof(buf), "PhotoFrame-%04X", last);
  return String(buf);
}

static String sanitize_filename(const String& in) {
  String out;
  out.reserve(in.length() + 4);
  for (size_t i=0; i<in.length(); i++) {
    char c = in[i];
    if ((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') ||
        c=='_' || c=='-' || c=='.') out += c;
    else out += '_';
  }
  // force .jpg
  String low = out; low.toLowerCase();
  if (low.endsWith(".jpeg")) return out;
  if (!low.endsWith(".jpg")) out += ".jpg";
  return out;
}


static void handle_root() {
  s_server.send_P(200, "text/html", INDEX_HTML);
}

static void handle_upload_done_response() {
  s_server.send(200, "text/plain", "OK");
}

static void handle_upload_stream() {
  HTTPUpload& up = s_server.upload();

  if (up.status == UPLOAD_FILE_START) {
    if (SD.cardType() == CARD_NONE) {
      Serial.println("Upload: SD not mounted");
      return;
    }

    SD.mkdir("/photos");

    String fn = sanitize_filename(String(up.filename));
    s_uploadPath = String("/photos/") + fn;

    // overwrite existing
    if (SD.exists(s_uploadPath)) SD.remove(s_uploadPath);

    s_uploadFile = SD.open(s_uploadPath, FILE_WRITE);
    if (!s_uploadFile) {
      Serial.printf("Upload open failed: %s\n", s_uploadPath.c_str());
      return;
    }

    Serial.printf("Upload START: %s -> %s\n", up.filename.c_str(), s_uploadPath.c_str());

  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (s_uploadFile) {
      s_uploadFile.write(up.buf, up.currentSize);
    }

  } else if (up.status == UPLOAD_FILE_END) {
    if (s_uploadFile) {
      s_uploadFile.close();
      s_uploadFile = File();
      s_uploaded++;
      Serial.printf("Upload END: %s (%u bytes) total=%lu\n",
                    s_uploadPath.c_str(), (unsigned)up.totalSize, (unsigned long)s_uploaded);
    }

  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (s_uploadFile) {
      s_uploadFile.close();
      s_uploadFile = File();
    }
    Serial.println("Upload ABORTED");
  }
}

// ---------- Public API ----------
namespace UploadMode {

void init() {
  // no-op
}

bool isActive() { return s_active; }
uint32_t uploadedCount() { return s_uploaded; }

String apSsid() { return s_apSsid; }
String apPass() { return String(AP_PASS); }
String ipString() { return s_ip.toString(); }

bool enter() {
  if (s_active) return true;
  s_uploaded = 0;

  // Ensure SD is mounted (you already mount in setup, this is just safety)
  if (SD.cardType() == CARD_NONE) {
    Serial.println("UploadMode: SD not mounted (CARD_NONE)");
    // still allow AP/server to start so user sees something, but uploads will fail
  }

  WiFi.mode(WIFI_AP);
  delay(20);

  s_apSsid = make_ap_ssid();
  bool ok = WiFi.softAP(s_apSsid.c_str(), AP_PASS, AP_CH);
  delay(50);

  if (!ok) {
    Serial.println("UploadMode: softAP failed");
    return false;
  }

  s_ip = WiFi.softAPIP();
  Serial.printf("UploadMode AP: SSID=%s PASS=%s IP=%s\n",
                s_apSsid.c_str(), AP_PASS, s_ip.toString().c_str());

  // Server routes
  s_server.on("/", HTTP_GET, handle_root);
  s_server.on("/upload", HTTP_POST, handle_upload_done_response, handle_upload_stream);
  s_server.on("/status", HTTP_GET, []() {
    String json = "{\"uploaded\":" + String(s_uploaded) + "}";
    s_server.send(200, "application/json", json);
  });

  s_server.begin();
  s_active = true;
  return true;
}

void loop() {
  if (!s_active) return;
  s_server.handleClient();
}

void exit() {
  if (!s_active) return;

  s_server.stop();

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  s_active = false;
}

} // namespace UploadMode
