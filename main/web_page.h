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
select{background:#17222e;color:var(--fg);border:1px solid #273a4c;border-radius:9px;
padding:6px 8px;font-family:inherit;font-size:12px;outline:none;max-width:62%}
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
.srv{margin:12px 0 0;padding-top:10px;border-top:1px dashed #1b2836}
.srv:first-of-type{border-top:none;padding-top:0}
.srv .top{display:flex;justify-content:space-between;font-size:12px;color:#b7c6d6;margin-bottom:5px}
.srv .top b{color:var(--acc);font-weight:600;font-variant-numeric:tabular-nums}
.srow{display:grid;grid-template-columns:40px 1fr 40px 56px;gap:6px;align-items:center}
.srow button{padding:5px 0;font-size:12px;border-radius:9px}
.pulse{margin-top:14px;padding-top:12px;border-top:1px solid var(--line)}
.pulse .ttl{display:flex;justify-content:space-between;font-size:12px;color:#b7c6d6}
.pulse .ttl b{color:var(--warn);font-weight:600}
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
    <div class="row"><label>判定阈值</label><b class="mono" id="thv">4.00 V</b></div>
    <input type="range" id="th" min="0.0" max="5.0" step="0.05" value="4.00">
    <div class="switch"><span>黑线为高电平（极性反转）</span>
      <input type="checkbox" id="inv"></div>
    <div class="switch"><span>探头安装顺序反转（8 → 1）</span>
      <input type="checkbox" id="rev"></div>
    <div class="row"><span class="badge" id="linestate">循迹 --</span>
      <span class="mono" id="errv">err 0.00</span></div>
    <div class="hint">条形高度=电压(0~5V 满量程)，橙线=阈值；探头变红表示判定为黑线。编号 1 在最左、8 在最右。</div>
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
    <div class="slider" style="margin-top:12px">
      <div class="top"><span>底盘航速（手动 / 循迹基础）</span><b id="csv">450 步/秒</b></div>
      <input type="range" id="cs" min="100" max="1500" step="10" value="450">
    </div>
    <div class="slider">
      <div class="top"><span>升降速度 M5</span><b id="lsv">400 步/秒</b></div>
      <input type="range" id="ls" min="100" max="1200" step="10" value="400">
    </div>
    <div class="switch" style="margin-top:2px"><span>底盘使能 (EN / GPIO42)</span>
      <input type="checkbox" id="enChassis"></div>
    <div class="switch"><span>升降使能 (EN / GPIO41)</span>
      <input type="checkbox" id="enLift"></div>
    <div class="grid2" style="margin-top:6px">
      <button id="liftUp">▲ 升降点动</button>
      <button id="liftDown">▼ 下降点动</button>
    </div>
    <div class="hint">按住方向键持续运动，松开立即停止；指令 400ms 未刷新自动停车。方向键只决定方向，实际轮速由上面的"底盘航速"决定。</div>
    <div class="wheels" id="wheels"></div>
  </section>

  <section class="card">
    <h2>机械臂舵机 <span id="srvbadge" class="badge">LU9685</span></h2>
    <div class="row" style="margin-top:-4px">
      <label>协议模式</label>
      <select id="proto">
        <option value="0">单字节角度 0~180（默认）</option>
        <option value="1">双字节脉宽 · 低字节在前</option>
        <option value="2">双字节脉宽 · 高字节在前</option>
      </select>
    </div>

    <div class="srv">
      <div class="top"><span>底座旋转 CH0 · 20kg</span><b id="a0">90°</b></div>
      <div class="srow">
        <button data-rng="s0" data-d="-1">−1°</button>
        <input type="range" id="s0" min="0" max="180" step="1" value="90">
        <button data-rng="s0" data-d="1">+1°</button>
        <button class="stop" data-rng="s0" data-mid="1">中位</button>
      </div>
    </div>

    <div class="srv">
      <div class="top"><span>物料盘 CH4 · SG90</span><b id="a4">90°</b></div>
      <div class="srow">
        <button data-rng="s4" data-d="-1">−1°</button>
        <input type="range" id="s4" min="0" max="180" step="1" value="90">
        <button data-rng="s4" data-d="1">+1°</button>
        <button class="stop" data-rng="s4" data-mid="1">中位</button>
      </div>
      <div class="btnrow" style="margin-top:8px">
        <button class="ghost" data-tray="0">工位1 · 0°</button>
        <button class="ghost" data-tray="45">工位2 · 45°</button>
        <button class="ghost" data-tray="90">工位3 · 90°</button>
        <button class="ghost" data-tray="135">工位4 · 135°</button>
        <button class="ghost" data-tray="180">工位5 · 180°</button>
      </div>
    </div>

    <div class="srv">
      <div class="top"><span>夹爪开合 CH8 · 20kg</span><b id="a8">90°</b></div>
      <div class="srow">
        <button data-rng="s8" data-d="-1">−1°</button>
        <input type="range" id="s8" min="0" max="180" step="1" value="90">
        <button data-rng="s8" data-d="1">+1°</button>
        <button class="stop" data-rng="s8" data-mid="1">中位</button>
      </div>
    </div>

    <div class="pulse">
      <div class="ttl"><span>360° 连续舵机 · 定时点动</span><b id="pulseInfo">—</b></div>
      <div class="row" style="margin-top:6px"><label>通道</label>
        <select id="pCh">
          <option value="0">CH0 底座</option>
          <option value="4">CH4 物料盘</option>
          <option value="8">CH8 夹爪</option>
        </select>
      </div>
      <div class="slider"><div class="top"><span>速度角（&lt;90 正转 / &gt;90 反转）</span><b id="pAv">60°</b></div>
        <input type="range" id="pA" min="0" max="180" step="1" value="60"></div>
      <div class="slider"><div class="top"><span>持续时间</span><b id="pDv">500 ms</b></div>
        <input type="range" id="pD" min="50" max="3000" step="50" value="500"></div>
      <div class="slider"><div class="top"><span>中位停止校准</span><b id="pCv">90</b></div>
        <input type="range" id="pC" min="85" max="95" step="1" value="90"></div>
      <div class="btnrow">
        <button id="pulseGo" class="primary">▶ 点动</button>
        <button id="pulseStop" class="stop">■ 停止</button>
      </div>
    </div>

    <div class="hint">
      滑块拖动时 30ms 节流实时下发；鼠标悬停滑块滚动滚轮可按 1° 微调。<br>
      若舵机表现为"持续转动、角度无效"，多半是 360° 连续旋转舵机：把速度角调到 60/120 用点动控制转角，中位校准到实际停转点（85~95）。
    </div>
  </section>

  <section class="card">
    <h2>循迹 PID 调参 <span>实时生效</span></h2>
    <div class="slider"><div class="top"><span>Kp 比例</span><b id="kpv">0.90</b></div>
      <input type="range" id="kp" min="0" max="5" step="0.05" value="0.9"></div>
    <div class="slider"><div class="top"><span>Ki 积分</span><b id="kiv">0.00</b></div>
      <input type="range" id="ki" min="0" max="2" step="0.01" value="0"></div>
    <div class="slider"><div class="top"><span>Kd 微分</span><b id="kdv">0.00</b></div>
      <input type="range" id="kd" min="0" max="1" step="0.01" value="0"></div>
    <div class="slider"><div class="top"><span>修正常数上限 ω</span><b id="omv">3.0 rad/s</b></div>
      <input type="range" id="om" min="0" max="8" step="0.1" value="3"></div>
    <div class="slider"><div class="top"><span>加速度（5 轴共用）</span><b id="accv">3000 步/秒²</b></div>
      <input type="range" id="acc" min="500" max="20000" step="100" value="3000"></div>
    <div class="hint">误差范围 ±3.5（探头间距）。Kp 过大会左右摆动，可先加 Kd 抑制；Ki 一般保持 0。循迹基础速度在"运动控制"里的底盘航速。</div>
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
$("liftUp").addEventListener("pointerdown", e => { e.preventDefault(); holdLift(e.currentTarget, 1); });
$("liftDown").addEventListener("pointerdown", e => { e.preventDefault(); holdLift(e.currentTarget, -1); });

/* ---- 模式 ---- */
$("mMan").onclick = () => { stopMove(); api("/api/mode?m=0"); };
$("mLine").onclick = () => { stopMove(); api("/api/mode?m=1"); };

/* ---- EN 开关 ---- */
$("enChassis").onchange = e => api(`/api/en?chassis=${e.target.checked ? 1 : 0}`);
$("enLift").onchange = e => api(`/api/en?lift=${e.target.checked ? 1 : 0}`);

/* ---- 速度滑块（底盘 / 升降） ---- */
let speedTimer = null;
function sendSpeed() {
  clearTimeout(speedTimer);
  speedTimer = setTimeout(() => {
    api(`/speed?chassis=${$("cs").value}&lift=${$("ls").value}`);
  }, 100);
}
[["cs", "csv"], ["ls", "lsv"]].forEach(([id, lab]) => {
  const el = $(id);
  el.addEventListener("pointerdown", () => S.dragging = id);
  el.addEventListener("input", () => { $(lab).textContent = el.value + " 步/秒"; });
  el.addEventListener("change", () => { S.dragging = null; sendSpeed(); });
});

/* ---- 舵机：实时节流 + 滚轮微调 + 微调/中位按钮 ---- */
const SRV = [
  { ch: 0, rng: "s0", lab: "a0" },
  { ch: 4, rng: "s4", lab: "a4" },
  { ch: 8, rng: "s8", lab: "a8" }
];
const CENTER = { 0: 90, 4: 90, 8: 90 };
const srvState = {};

function servoSet(ch, a, force) {
  a = Math.max(0, Math.min(180, Math.round(a)));
  const st = srvState[ch];
  const send = () => { st.last = performance.now(); api(`/api/servo?ch=${ch}&a=${a}`); };
  clearTimeout(st.timer);
  if (force || performance.now() - st.last >= 30) {
    send();
  } else {
    st.timer = setTimeout(send, 30);   /* 30ms 节流：尾帧必定发出 */
  }
  return a;
}

function syncServoUI(ch, a) {
  const s = SRV.find(x => x.ch === ch);
  if (!s) return;
  $(s.rng).value = a;
  $(s.lab).textContent = a + "°";
}

SRV.forEach(s => {
  const el = $(s.rng);
  srvState[s.ch] = { last: 0, timer: null };

  el.addEventListener("pointerdown", () => { S.dragging = s.rng; });
  el.addEventListener("input", () => {
    $(s.lab).textContent = el.value + "°";
    servoSet(s.ch, +el.value);
  });
  el.addEventListener("change", () => {
    S.dragging = null;
    servoSet(s.ch, +el.value, true);
  });
  /* 电脑滚轮 1° 精细调节 */
  el.addEventListener("wheel", e => {
    e.preventDefault();
    const v = Math.max(0, Math.min(180, +el.value + (e.deltaY < 0 ? 1 : -1)));
    el.value = v;
    $(s.lab).textContent = v + "°";
    servoSet(s.ch, v, true);
  }, { passive: false });
});

/* 微调 / 中位按钮 */
document.querySelectorAll(".srow button[data-rng]").forEach(b => {
  b.onclick = () => {
    const ch = SRV.find(x => x.rng === b.dataset.rng).ch;
    const el = $(b.dataset.rng);
    const v = b.dataset.mid ? CENTER[ch]
                            : Math.max(0, Math.min(180, +el.value + (+b.dataset.d)));
    el.value = v;
    $(SRV.find(x => x.ch === ch).lab).textContent = v + "°";
    servoSet(ch, v, true);
  };
});

/* 物料盘工位 */
document.querySelectorAll("[data-tray]").forEach(b => {
  b.onclick = () => {
    const a = +b.dataset.tray;
    syncServoUI(4, a);
    servoSet(4, a, true);
  };
});

/* ---- 协议切换 ---- */
$("proto").onchange = e => api(`/servo_proto?mode=${e.target.value}`);

/* ---- 360° 点动 ---- */
const pCh = $("pCh"), pA = $("pA"), pD = $("pD"), pC = $("pC");
function syncCenterUI() {
  const ch = +pCh.value;
  pC.value = CENTER[ch];
  $("pCv").textContent = CENTER[ch];
}
pCh.onchange = syncCenterUI;
pA.oninput = () => { $("pAv").textContent = pA.value + "°"; };
pD.oninput = () => { $("pDv").textContent = pD.value + " ms"; };
pC.addEventListener("pointerdown", () => { S.dragging = "pC"; });
pC.oninput = () => { $("pCv").textContent = pC.value; };
pC.onchange = () => {
  S.dragging = null;
  const ch = +pCh.value, v = +pC.value;
  CENTER[ch] = v;
  api(`/servo_center?ch=${ch}&a=${v}`);
};
$("pulseGo").onclick = () => {
  const ch = +pCh.value;
  api(`/servo_pulse?ch=${ch}&speed_angle=${pA.value}&duration_ms=${pD.value}&stop=${CENTER[ch]}`);
  $("pulseInfo").textContent = `CH${ch} ${pA.value}° × ${pD.value}ms`;
};
$("pulseStop").onclick = () => {
  const ch = +pCh.value;
  servoSet(ch, CENTER[ch], true);
  syncServoUI(ch, CENTER[ch]);
  $("pulseInfo").textContent = `CH${ch} 停止`;
};

/* ---- 阈值 / 极性 / 顺序 ---- */
const th = $("th"), inv = $("inv"), rev = $("rev");
th.addEventListener("pointerdown", () => { S.dragging = "th"; });
th.addEventListener("input", () => { $("thv").textContent = Number(th.value).toFixed(2) + " V"; });
th.addEventListener("change", () => {
  S.dragging = null;
  api(`/thresh?th=${Number(th.value).toFixed(2)}`);
});
inv.onchange = () => api(`/api/line?inv=${inv.checked ? 1 : 0}`);
rev.onchange = () => api(`/api/line?rev=${rev.checked ? 1 : 0}`);

/* ---- PID ---- */
const pidMap = [["kp","kpv",2],["ki","kiv",2],["kd","kdv",2],["om","omv",1],["acc","accv",0]];
pidMap.forEach(([id, lab, dec]) => {
  const el = $(id);
  el.addEventListener("pointerdown", () => { S.dragging = id; });
  el.addEventListener("input", () => {
    const unit = id === "om" ? " rad/s" : id === "acc" ? " 步/秒²" : "";
    $(lab).textContent = Number(el.value).toFixed(dec) + unit;
  });
  el.addEventListener("change", () => { S.dragging = null; sendPid(); });
});
let pidTimer = null;
function sendPid() {
  clearTimeout(pidTimer);
  pidTimer = setTimeout(() => {
    api(`/api/line?kp=${$("kp").value}&ki=${$("ki").value}&kd=${$("kd").value}` +
        `&om=${$("om").value}&accel=${$("acc").value}`);
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
    bars[i].fill.style.height = Math.max(0, Math.min(100, v / 5.0 * 100)) + "%";
    bars[i].val.textContent = v.toFixed(2);
    bars[i].el.classList.toggle("black", !!j.b[i]);
    const m = (j.th / 5.0 * 100);
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
  if (S.dragging !== "cs") { $("cs").value = j.cs; $("csv").textContent = Math.round(j.cs) + " 步/秒"; }
  if (S.dragging !== "ls") { $("ls").value = j.lsp; $("lsv").textContent = Math.round(j.lsp) + " 步/秒"; }

  if (S.dragging !== "kp") { $("kp").value = j.kp; $("kpv").textContent = j.kp.toFixed(2); }
  if (S.dragging !== "ki") { $("ki").value = j.ki; $("kiv").textContent = j.ki.toFixed(2); }
  if (S.dragging !== "kd") { $("kd").value = j.kd; $("kdv").textContent = j.kd.toFixed(2); }
  if (S.dragging !== "om") { $("om").value = j.om; $("omv").textContent = j.om.toFixed(1) + " rad/s"; }
  if (S.dragging !== "acc") { $("acc").value = j.acc; $("accv").textContent = j.acc.toFixed(0) + " 步/秒²"; }

  if (S.dragging !== "s0") { $("s0").value = j.srv[0]; $("a0").textContent = j.srv[0] + "°"; }
  if (S.dragging !== "s4") { $("s4").value = j.srv[1]; $("a4").textContent = j.srv[1] + "°"; }
  if (S.dragging !== "s8") { $("s8").value = j.srv[2]; $("a8").textContent = j.srv[2] + "°"; }

  CENTER[0] = j.ctr[0]; CENTER[4] = j.ctr[1]; CENTER[8] = j.ctr[2];
  if (S.dragging !== "pC") syncCenterUI();
  if ($("proto").value !== String(j.proto)) $("proto").value = j.proto;

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
