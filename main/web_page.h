/**
 * @file web_page.h
 * @brief 内嵌单页调试控制台 (HTML/CSS/JS)
 */
#pragma once

static const char WEB_INDEX_HTML[] =
R"rawliteral(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>麦克纳姆轮调试台</title>
<style>
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
:root{--bg:#070a0f;--card:#101821;--line:#1e2c3a;--fg:#e6edf3;--dim:#8b9bb0;
--acc:#38bdf8;--ok:#22c55e;--bad:#ef4444;--warn:#f59e0b}
html,body{margin:0;background:radial-gradient(1200px 600px at 50% -10%,#12202e 0%,var(--bg) 60%);
color:var(--fg);font-family:system-ui,-apple-system,"Segoe UI",Roboto,"PingFang SC","Microsoft YaHei",sans-serif}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;
padding:14px 18px;border-bottom:1px solid var(--line);position:sticky;top:0;
background:rgba(7,10,15,.86);backdrop-filter:blur(10px);z-index:5}
h1{font-size:16px;margin:0;font-weight:600;letter-spacing:.02em}
h1 small{color:var(--dim);font-weight:400;font-size:11px;margin-left:8px}
.conn{display:flex;align-items:center;gap:8px;font-size:12px;color:var(--dim)}
.dot{width:9px;height:9px;border-radius:50%;background:var(--bad);box-shadow:0 0 8px var(--bad)}
.dot.on{background:var(--ok);box-shadow:0 0 8px var(--ok)}
main{display:grid;grid-template-columns:repeat(auto-fit,minmax(310px,1fr));gap:12px;
padding:12px;max-width:1440px;margin:0 auto;align-items:start}
.card{background:linear-gradient(180deg,#121b25,#0d141c);border:1px solid var(--line);
border-radius:14px;padding:14px}
h2{font-size:11px;letter-spacing:.14em;text-transform:uppercase;color:var(--dim);
margin:0 0 12px;font-weight:600;display:flex;justify-content:space-between;align-items:center}
h2 span{color:#4b6076;letter-spacing:0;text-transform:none;font-weight:400}
.sensors{display:flex;gap:6px}
.sensor{flex:1;text-align:center;min-width:0}
.track{position:relative;height:140px;background:#0a121a;border-radius:8px;
border:1px solid var(--line);overflow:hidden}
.fill{position:absolute;left:0;right:0;bottom:0;height:0%;transition:height .08s linear;
background:linear-gradient(180deg,#7dd3fc,#0284c7)}
.sensor.black .fill{background:linear-gradient(180deg,#fca5a5,#dc2626)}
.mark{position:absolute;left:0;right:0;bottom:50%;height:1px;background:rgba(245,158,11,.85)}
.lbl{font-size:10px;color:var(--dim);margin-top:5px}
.val{font-size:10px;font-variant-numeric:tabular-nums;color:#a9bbcf;margin-top:1px}
.row{display:flex;align-items:center;justify-content:space-between;gap:10px;margin:9px 0}
.row label{font-size:12px;color:#b7c6d6}
.mono{font-variant-numeric:tabular-nums;font-size:11px;color:var(--dim)}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.pad button{aspect-ratio:1.15;font-size:20px;display:flex;align-items:center;justify-content:center}
.rot{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:8px}
button{background:#17222e;color:var(--fg);border:1px solid #273a4c;border-radius:11px;
padding:10px 12px;font-size:13px;font-family:inherit;cursor:pointer;user-select:none;
touch-action:none;transition:background .12s,border-color .12s,transform .05s}
button:hover{border-color:#3a5570}
button:active,button.active{background:var(--acc);color:#04121b;border-color:var(--acc);transform:scale(.97)}
button.stop{background:#2a1416;border-color:#5a2229;color:#fca5a5}
button.stop:active{background:var(--bad);color:#fff}
button.ghost{background:transparent}
button.primary{background:#0c2a3a;border-color:#1d5675;color:#9fdcff}
button.primary.active{background:var(--acc);color:#04121b}
.btnrow{display:flex;gap:8px;flex-wrap:wrap}
.btnrow button{flex:1;min-width:70px}
.switch{display:flex;align-items:center;justify-content:space-between;gap:10px;
padding:7px 0;font-size:12px;color:#b7c6d6}
input[type=checkbox]{appearance:none;-webkit-appearance:none;width:46px;height:26px;
border-radius:20px;background:#22303f;position:relative;cursor:pointer;transition:.2s;margin:0;flex:none}
input[type=checkbox]:checked{background:var(--ok)}
input[type=checkbox]::after{content:"";position:absolute;top:3px;left:3px;width:20px;height:20px;
border-radius:50%;background:#fff;transition:.2s}
input[type=checkbox]:checked::after{left:23px}
input[type=range]{width:100%;accent-color:var(--acc);height:24px;margin:0}
.slider{margin:10px 0}
.slider .top{display:flex;justify-content:space-between;font-size:12px;color:#b7c6d6;margin-bottom:2px}
.slider .top b{color:var(--acc);font-weight:600;font-variant-numeric:tabular-nums}
.hint{font-size:11px;color:#5c6f84;line-height:1.6;margin-top:8px}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.badge{font-size:10px;padding:2px 7px;border-radius:20px;background:#1a2836;color:#8b9bb0}
.badge.ok{background:rgba(34,197,94,.15);color:#5ee08a}
.badge.bad{background:rgba(239,68,68,.15);color:#fca5a5}
.badge.warn{background:rgba(245,158,11,.15);color:#fbbf24}
.wheels{display:grid;grid-template-columns:repeat(2,1fr);gap:6px;margin-top:6px}
.wheel{background:#0b141d;border:1px solid var(--line);border-radius:8px;padding:7px 9px;font-size:11px;color:var(--dim)}
.wheel b{float:right;color:#cfe3f5;font-weight:500;font-variant-numeric:tabular-nums}
</style>
</head>
<body>
<header>
  <h1>麦克纳姆轮调试台 <small>ESP32-S3 · ESP-IDF</small></h1>
  <div class="conn"><span id="dot" class="dot"></span><span id="ip">未连接</span></div>
</header>
<main>

  <section class="card">
    <h2>8 路循迹探头 <span id="seq">seq -</span></h2>
    <div class="sensors" id="sensors"></div>
    <div class="row"><label>判定阈值</label><b class="mono" id="thv">1.65 V</b></div>
    <input type="range" id="th" min="0" max="3.3" step="0.01" value="1.65">
    <div class="switch"><span>黑线为高电平（极性反转）</span>
      <input type="checkbox" id="inv"></div>
    <div class="switch"><span>探头安装顺序反转（8 → 1）</span>
      <input type="checkbox" id="rev"></div>
    <div class="row"><span class="badge" id="linestate">循迹 --</span>
      <span class="mono" id="errv">err 0.00</span></div>
    <div class="hint">条形高度=电压，红线=阈值；探头变红表示判定为黑线。编号 1 在最左、8 在最右。</div>
  </section>

  <section class="card">
    <h2>运动控制 <span id="modebadge" class="badge">手动</span></h2>
    <div class="btnrow" style="margin-bottom:10px">
      <button id="mMan" class="primary active">手动遥控</button>
      <button id="mLine" class="primary">PID 循迹</button>
    </div>
    <div class="pad">
      <button data-vx="-0.35" data-vy="0.35" data-w="0">↖</button>
      <button data-vx="0" data-vy="0.45" data-w="0">↑</button>
      <button data-vx="0.35" data-vy="0.35" data-w="0">↗</button>
      <button data-vx="-0.45" data-vy="0" data-w="0">←</button>
      <button class="stop" id="btnStop">■</button>
      <button data-vx="0.45" data-vy="0" data-w="0">→</button>
      <button data-vx="-0.35" data-vy="-0.35" data-w="0">↙</button>
      <button data-vx="0" data-vy="-0.45" data-w="0">↓</button>
      <button data-vx="0.35" data-vy="-0.35" data-w="0">↘</button>
    </div>
    <div class="rot">
      <button data-vx="0" data-vy="0" data-w="1.6">↺ 左旋</button>
      <button data-vx="0" data-vy="0" data-w="-1.6">右旋 ↻</button>
    </div>
    <div class="switch" style="margin-top:10px"><span>底盘使能 (EN / GPIO42)</span>
      <input type="checkbox" id="enChassis"></div>
    <div class="switch"><span>升降使能 (EN / GPIO41)</span>
      <input type="checkbox" id="enLift"></div>
    <div class="grid2" style="margin-top:6px">
      <button id="liftUp">▲ 升降点动</button>
      <button id="liftDown">▼ 下降点动</button>
    </div>
    <div class="hint">按住方向键持续运动，松开立即停止；指令 400ms 未刷新自动停车。方向按 O 型麦轮解算。</div>
    <div class="wheels" id="wheels"></div>
  </section>

  <section class="card">
    <h2>机械臂舵机 <span id="srvbadge" class="badge">LU9685</span></h2>
    <div class="slider">
      <div class="top"><span>底座旋转 CH0 · 20kg</span><b id="a0">90°</b></div>
      <input type="range" id="s0" min="0" max="180" step="1" value="90">
    </div>
    <div class="slider">
      <div class="top"><span>物料盘 CH4 · SG90</span><b id="a4">90°</b></div>
      <input type="range" id="s4" min="0" max="180" step="1" value="90">
    </div>
    <div class="btnrow" style="margin-top:4px">
      <button class="ghost" data-tray="0">工位1 · 0°</button>
      <button class="ghost" data-tray="45">工位2 · 45°</button>
      <button class="ghost" data-tray="90">工位3 · 90°</button>
      <button class="ghost" data-tray="135">工位4 · 135°</button>
      <button class="ghost" data-tray="180">工位5 · 180°</button>
    </div>
    <div class="slider" style="margin-top:12px">
      <div class="top"><span>夹爪开合 CH8 · 20kg</span><b id="a8">90°</b></div>
      <input type="range" id="s8" min="0" max="180" step="1" value="90">
    </div>
    <div class="hint">滑块松开后下发角度；上电默认三路 90° 中位。LU9685 地址 0x40/0x00 自动探测。</div>
  </section>

  <section class="card">
    <h2>循迹 PID 调参 <span>实时生效</span></h2>
    <div class="slider"><div class="top"><span>Kp 比例</span><b id="kpv">0.90</b></div>
      <input type="range" id="kp" min="0" max="5" step="0.05" value="0.9"></div>
    <div class="slider"><div class="top"><span>Ki 积分</span><b id="kiv">0.00</b></div>
      <input type="range" id="ki" min="0" max="2" step="0.01" value="0"></div>
    <div class="slider"><div class="top"><span>Kd 微分</span><b id="kdv">0.00</b></div>
      <input type="range" id="kd" min="0" max="1" step="0.01" value="0"></div>
    <div class="slider"><div class="top"><span>基础速度</span><b id="basev">0.30 m/s</b></div>
      <input type="range" id="base" min="0" max="1.5" step="0.01" value="0.3"></div>
    <div class="slider"><div class="top"><span>修正常数上限 ω</span><b id="omv">3.0 rad/s</b></div>
      <input type="range" id="om" min="0" max="8" step="0.1" value="3"></div>
    <div class="slider"><div class="top"><span>加速度（5 轴共用）</span><b id="accv">3000 步/秒²</b></div>
      <input type="range" id="acc" min="500" max="20000" step="100" value="3000"></div>
    <div class="hint">误差范围 ±3.5（探头间距）。Kp 过大会左右摆动，可先加 Kd 抑制；Ki 一般保持 0。</div>
  </section>

</main>
<script>
"use strict";
const $ = id => document.getElementById(id);
const S = { move: null, lift: null, dragging: null, polling: false, status: null };

function api(url) { fetch(url, { cache: "no-store" }).catch(() => {}); }
function stopMove() {
  if (S.move) { clearInterval(S.move.timer); S.move.el.classList.remove("active"); S.move = null; }
  api("/api/move?vx=0&vy=0&w=0");
}
function stopLift() {
  if (S.lift) { clearInterval(S.lift.timer); S.lift.el.classList.remove("active"); S.lift = null; }
  api("/api/lift?v=0");
}
function holdMove(el, vx, vy, w) {
  stopMove();
  const send = () => api(`/api/move?vx=${vx}&vy=${vy}&w=${w}`);
  send(); el.classList.add("active");
  S.move = { el, timer: setInterval(send, 100) };
}
function holdLift(el, v) {
  stopLift();
  const send = () => api(`/api/lift?v=${v}`);
  send(); el.classList.add("active");
  S.lift = { el, timer: setInterval(send, 100) };
}

/* ---- 方向键 ---- */
document.querySelectorAll(".pad button[data-vx], .rot button").forEach(el => {
  el.addEventListener("pointerdown", e => {
    e.preventDefault();
    holdMove(el, el.dataset.vx, el.dataset.vy, el.dataset.w);
  });
});
window.addEventListener("pointerup", () => { stopMove(); stopLift(); });
window.addEventListener("pointercancel", () => { stopMove(); stopLift(); });
$("btnStop").addEventListener("click", () => {
  stopMove(); stopLift(); api("/api/stop");
});
$("liftUp").addEventListener("pointerdown", e => { e.preventDefault(); holdLift(e.currentTarget, 700); });
$("liftDown").addEventListener("pointerdown", e => { e.preventDefault(); holdLift(e.currentTarget, -700); });

/* ---- 模式 ---- */
$("mMan").onclick = () => { stopMove(); api("/api/mode?m=0"); };
$("mLine").onclick = () => { stopMove(); api("/api/mode?m=1"); };

/* ---- EN 开关 ---- */
$("enChassis").onchange = e => api(`/api/en?chassis=${e.target.checked ? 1 : 0}`);
$("enLift").onchange = e => api(`/api/en?lift=${e.target.checked ? 1 : 0}`);

/* ---- 舵机 ---- */
function bindServo(id, ch, label) {
  const el = $(id);
  el.addEventListener("pointerdown", () => S.dragging = id);
  el.addEventListener("input", () => { $(label).textContent = el.value + "°"; });
  el.addEventListener("change", () => {
    S.dragging = null;
    api(`/api/servo?ch=${ch}&a=${el.value}`);
  });
}
bindServo("s0", 0, "a0");
bindServo("s4", 4, "a4");
bindServo("s8", 8, "a8");
document.querySelectorAll("[data-tray]").forEach(b => {
  b.onclick = () => {
    const a = b.dataset.tray;
    $("s4").value = a; $("a4").textContent = a + "°";
    api(`/api/servo?ch=4&a=${a}`);
  };
});

/* ---- 阈值 ---- */
const th = $("th"), inv = $("inv"), rev = $("rev");
th.addEventListener("pointerdown", () => S.dragging = "th");
th.addEventListener("input", () => { $("thv").textContent = Number(th.value).toFixed(2) + " V"; });
th.addEventListener("change", () => {
  S.dragging = null;
  api(`/api/line?th=${th.value}&inv=${inv.checked ? 1 : 0}&rev=${rev.checked ? 1 : 0}`);
});
inv.onchange = () => api(`/api/line?inv=${inv.checked ? 1 : 0}`);
rev.onchange = () => api(`/api/line?rev=${rev.checked ? 1 : 0}`);

/* ---- PID ---- */
const pidMap = [["kp","kpv",2],["ki","kiv",2],["kd","kdv",2],["base","basev",2],["om","omv",1],["acc","accv",0]];
pidMap.forEach(([id, lab, dec]) => {
  const el = $(id);
  el.addEventListener("pointerdown", () => S.dragging = id);
  el.addEventListener("input", () => {
    const unit = id === "base" ? " m/s" : id === "om" ? " rad/s" : id === "acc" ? " 步/秒²" : "";
    $(lab).textContent = Number(el.value).toFixed(dec) + unit;
  });
  el.addEventListener("change", () => { S.dragging = null; sendPid(); });
});
let pidTimer = null;
function sendPid() {
  clearTimeout(pidTimer);
  pidTimer = setTimeout(() => {
    api(`/api/line?kp=${$("kp").value}&ki=${$("ki").value}&kd=${$("kd").value}` +
        `&base=${$("base").value}&om=${$("om").value}&accel=${$("acc").value}`);
  }, 80);
}

/* ---- 渲染 ---- */
const names = ["RR","RF","LF","LR"];
let bars = [];
for (let i = 0; i < 8; i++) {
  const d = document.createElement("div");
  d.className = "sensor"; d.id = "bar" + i;
  d.innerHTML = `<div class="track"><div class="fill"></div><div class="mark"></div></div>
    <div class="lbl">${i + 1}</div><div class="val">0.00</div>`;
  $("sensors").appendChild(d);
  bars.push({ el: d, fill: d.querySelector(".fill"), mark: d.querySelector(".mark"), val: d.querySelector(".val") });
}

function setConn(ok, ip) {
  $("dot").className = "dot" + (ok ? " on" : "");
  $("ip").textContent = ok ? ip : "未连接";
}

function render(j) {
  S.status = j;
  for (let i = 0; i < 8; i++) {
    const v = j.v[i];
    bars[i].fill.style.height = Math.max(0, Math.min(100, v / 3.3 * 100)) + "%";
    bars[i].val.textContent = v.toFixed(2);
    bars[i].el.classList.toggle("black", !!j.b[i]);
    const m = (j.th / 3.3 * 100);
    bars[i].mark.style.bottom = Math.max(0, Math.min(100, m)) + "%";
  }
  $("seq").textContent = "seq " + j.seq;
  $("errv").textContent = "err " + j.err.toFixed(2);
  $("linestate").textContent = j.mode ? (j.ls ? "循迹：在线" : "循迹：丢线停车") : "循迹：未启用";
  $("linestate").className = "badge " + (j.mode ? (j.ls ? "ok" : "bad") : "");

  $("modebadge").textContent = j.mode ? "PID 循迹" : "手动";
  $("modebadge").className = "badge " + (j.mode ? "ok" : "");
  $("mMan").classList.toggle("active", !j.mode);
  $("mLine").classList.toggle("active", !!j.mode);

  $("enChassis").checked = !!j.en[0];
  $("enLift").checked = !!j.en[1];

  if (S.dragging !== "th") {
    $("th").value = j.th; $("thv").textContent = j.th.toFixed(2) + " V";
    $("inv").checked = !!j.inv;
    $("rev").checked = !!j.rev;
  }
  if (S.dragging !== "kp") { $("kp").value = j.kp; $("kpv").textContent = j.kp.toFixed(2); }
  if (S.dragging !== "ki") { $("ki").value = j.ki; $("kiv").textContent = j.ki.toFixed(2); }
  if (S.dragging !== "kd") { $("kd").value = j.kd; $("kdv").textContent = j.kd.toFixed(2); }
  if (S.dragging !== "base") { $("base").value = j.base; $("basev").textContent = j.base.toFixed(2) + " m/s"; }
  if (S.dragging !== "om") { $("om").value = j.om; $("omv").textContent = j.om.toFixed(1) + " rad/s"; }
  if (S.dragging !== "acc") { $("acc").value = j.acc; $("accv").textContent = j.acc.toFixed(0) + " 步/秒²"; }

  if (S.dragging !== "s0") { $("s0").value = j.srv[0]; $("a0").textContent = j.srv[0] + "°"; }
  if (S.dragging !== "s4") { $("s4").value = j.srv[1]; $("a4").textContent = j.srv[1] + "°"; }
  if (S.dragging !== "s8") { $("s8").value = j.srv[2]; $("a8").textContent = j.srv[2] + "°"; }

  $("srvbadge").textContent = j.srv_ok ? "LU9685 在线" : "LU9685 离线";
  $("srvbadge").className = "badge " + (j.srv_ok ? "ok" : "bad");

  let wh = "";
  for (let i = 0; i < 4; i++) {
    wh += `<div class="wheel">M${i + 1} ${names[i]}<b>${j.ws[i]}</b></div>`;
  }
  $("wheels").innerHTML = wh;
}

async function poll() {
  if (S.polling) return;
  S.polling = true;
  try {
    const r = await fetch("/api/status", { cache: "no-store" });
    if (!r.ok) throw new Error("http " + r.status);
    const j = await r.json();
    render(j);
    setConn(true, j.ip);
  } catch (e) {
    setConn(false);
  } finally {
    S.polling = false;
  }
}
poll();
setInterval(poll, 50);
</script>
</body>
</html>
)rawliteral";
