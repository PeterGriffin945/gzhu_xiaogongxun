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
.field{display:flex;flex-direction:column;gap:4px;font-size:11px;color:var(--dim)}
.field input{width:100%;background:#0a121a;color:var(--fg);border:1px solid #273a4c;border-radius:8px;padding:8px;font-family:inherit}
.statusline{font-size:12px;line-height:1.8;color:#b7c6d6;word-break:break-word}
.slots{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;margin:8px 0}
.slot{padding:7px;border:1px solid var(--line);border-radius:8px;text-align:center;font-size:11px;color:var(--dim)}
.slot.current{border-color:var(--acc);color:var(--acc)}
.c1{color:#f87171}.c2{color:#4ade80}.c3{color:#60a5fa}
details{margin-top:12px}summary{cursor:pointer;font-size:12px;color:var(--acc);margin-bottom:10px}
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
    <input type="range" id="th" min="4.0" max="5.0" step="0.01" value="4.00">
    <div class="switch"><span>黑线为高电平（极性反转）</span>
      <input type="checkbox" id="inv"></div>
    <div class="switch"><span>探头安装顺序反转（8 → 1）</span>
      <input type="checkbox" id="rev"></div>
    <div class="row"><span class="badge" id="linestate">循迹 --</span>
      <span class="mono" id="errv">err 0.00</span></div>
    <div class="hint">条形高度=电压(0~5V 满量程)，橙线=阈值；探头变红表示判定为黑线。编号 1 在最左、8 在最右。阈值调节范围 4.00~5.00V，步进 0.01V，便于在该区间精细标定。</div>
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
      <div class="top"><span>底盘航速（手动 / 循迹基础）</span><b id="csv">300 步/秒</b></div>
      <input type="range" id="cs" min="100" max="1500" step="10" value="300">
    </div>
    <div class="slider">
      <div class="top"><span>升降速度 M5</span><b id="lsv">300 步/秒</b></div>
      <input type="range" id="ls" min="100" max="1200" step="10" value="300">
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
        <button class="ghost" data-slot="0">slot 0</button>
        <button class="ghost" data-slot="1">slot 1</button>
        <button class="ghost" data-slot="2">slot 2</button>
        <button class="ghost" data-slot="3">slot 3</button>
        <button class="ghost" data-slot="4">slot 4</button>
        <button class="ghost" data-slot="5">slot 5</button>
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
      CH0 底座与 CH4 物料盘是 360° 连续旋转舵机：滑块给的是“逻辑角度”，程序按角度差换算转动时间，到位自动停在中位，
      不再一直转。速度角与 180° 用时可在"比赛自动流程 → 标定"里调整；中位校准到实际停转点（85~95）。
    </div>
  </section>

  <section class="card">
    <h2>比赛自动流程 <span id="compState" class="badge">MANUAL</span></h2>
    <div class="statusline" id="compInfo">等待状态…</div>
    <div class="btnrow" style="margin:10px 0">
      <button id="compStart" class="primary">▶ 启动三取三放</button>
      <button id="compAbort" class="stop">■ 全局中止</button>
    </div>
    <div class="slots" id="compSlots"></div>
    <div class="btnrow">
      <button id="zZero">设当前 Z 为运输零点</button>
      <button id="trayZero">设当前盘位为 slot0</button>
      <button id="trayNext">正转 60°</button>
    </div>
    <div class="statusline" id="visionInfo" style="margin-top:8px">K230 等待状态…</div>
    <details>
      <summary>动作、视觉、分度与横线标定（默认值必须实测）</summary>
      <div class="grid2">
        <label class="field">地面抓取 Z 步数<input type="number" data-cfg="zpick"></label>
        <label class="field">盘内取放 Z 步数<input type="number" data-cfg="ztray"></label>
        <label class="field">靶环放置 Z 步数<input type="number" data-cfg="zplace"></label>
        <label class="field">Z 最大软限位<input type="number" data-cfg="zmax"></label>
        <label class="field">底座运输角<input type="number" data-cfg="bstow"></label>
        <label class="field">底座地面抓取角<input type="number" data-cfg="bpick"></label>
        <label class="field">底座盘位角<input type="number" data-cfg="btray"></label>
        <label class="field">底座靶环角<input type="number" data-cfg="bplace"></label>
        <label class="field">夹爪张开角<input type="number" data-cfg="gopen"></label>
        <label class="field">夹爪闭合角<input type="number" data-cfg="gclose"></label>
        <label class="field">底座稳定 ms<input type="number" data-cfg="bms"></label>
        <label class="field">底座速度角（&lt;90 正转）<input type="number" data-cfg="bspeed"></label>
        <label class="field">底座 180° 用时 ms<input type="number" data-cfg="bsweep"></label>
        <label class="field">夹爪稳定 ms<input type="number" data-cfg="gms"></label>
        <label class="field">Z 定步速度<input type="number" data-cfg="lsps"></label>
        <label class="field">物料基准 X<input type="number" data-cfg="mcx"></label>
        <label class="field">物料基准 Y<input type="number" data-cfg="mcy"></label>
        <label class="field">靶环基准 X<input type="number" data-cfg="rcx"></label>
        <label class="field">靶环基准 Y<input type="number" data-cfg="rcy"></label>
        <label class="field">视觉容差 px<input type="number" data-cfg="tol"></label>
        <label class="field">最低置信度<input type="number" data-cfg="minconf"></label>
        <label class="field">最大修正次数<input type="number" data-cfg="maxiter"></label>
        <label class="field">X 修正方向 ±1<input type="number" data-cfg="xsign"></label>
        <label class="field">Y 修正方向 ±1<input type="number" data-cfg="ysign"></label>
        <label class="field">单次修正 mm<input type="number" step="0.5" data-cfg="nudge"></label>
        <label class="field">修正轮速步/秒<input type="number" data-cfg="asps"></label>
        <label class="field">CH4 正转速度角<input type="number" data-cfg="tspeed"></label>
        <label class="field">CH4 每 60° ms<input type="number" data-cfg="tms"></label>
        <label class="field">横线最少黑探头<input type="number" data-cfg="sblack"></label>
        <label class="field">横线确认帧数<input type="number" data-cfg="sconfirm"></label>
        <label class="field">离站最多黑探头<input type="number" data-cfg="sclear"></label>
        <label class="field">正常线重装帧数<input type="number" data-cfg="srearm"></label>
        <label class="field">工位最小间隔 ms<input type="number" data-cfg="sinterval"></label>
      </div>
      <button id="cfgSave" class="primary" style="width:100%;margin-top:10px">保存并确认本次标定</button>
    </details>
    <div class="hint">初始化顺序：低速点动校机械方向 → 设 Z 运输零点 → 对齐盘位并设 slot0 → <b>把底座人工摆到运输角</b> → 实测并保存标定 → 打开底盘/升降 EN → 启动。任何中止都会使 Z、盘位与底座基准失效，需重新确认。</div>
    <div class="statusline" id="compMsg" style="margin-top:8px;color:var(--warn)"></div>
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
const S = { move: null, lift: null, dragging: null, polling: false, status: null,
  compPolling: false, comp: null, cfgEditing: false };

async function api(url) {
  try {
    const r = await fetch(url, { cache: "no-store" });
    const j = await r.json();
    if (!r.ok || j.ok === 0) {
      $("compMsg").textContent = j.msg || ("HTTP " + r.status);
    }
    return j;
  } catch (e) {
    $("compMsg").textContent = "请求失败：" + e.message;
    return { ok: 0, msg: e.message };
  }
}
function stopMove() {
  if (S.move) {
    clearInterval(S.move.timer); S.move.el.classList.remove("active"); S.move = null;
    api("/api/move?vx=0&vy=0&w=0");
  }
}
function stopLift() {
  if (S.lift) {
    clearInterval(S.lift.timer); S.lift.el.classList.remove("active"); S.lift = null;
    api("/api/lift?v=0");
  }
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
window.addEventListener("pointerup", () => {
  stopMove(); stopLift();
  /* 只在拖动结束 100ms 后清除拖动态，给 change 发布的命令留出返回时间，
     否则下一次 50ms 轮询可能把滑块拉回旧值。 */
  setTimeout(() => { S.dragging = null; }, 100);
});
window.addEventListener("pointercancel", () => { stopMove(); stopLift(); S.dragging = null; });
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
const speedPending = {};
function sendSpeedNow() {
  clearTimeout(speedTimer);
  const c = +$("cs").value, l = +$("ls").value;
  api(`/speed?chassis=${c}&lift=${l}`);
  /* 记录本地期望值：服务端回显前不用旧状态覆盖滑块，避免“调了又跳回去” */
  speedPending.cs = { v: c, until: performance.now() + 1500 };
  speedPending.ls = { v: l, until: performance.now() + 1500 };
}
function sendSpeedSoon() {
  clearTimeout(speedTimer);
  speedTimer = setTimeout(sendSpeedNow, 60);
}
function applySpeedValue(id, lab, value) {
  const p = speedPending[id];
  if (p && performance.now() < p.until && Math.abs(p.v - value) > 1) {
    return;   /* 命令仍在途中 */
  }
  delete speedPending[id];
  $(id).value = value;
  $(lab).textContent = Math.round(value) + " 步/秒";
}
[["cs", "csv"], ["ls", "lsv"]].forEach(([id, lab]) => {
  const el = $(id);
  el.addEventListener("pointerdown", () => S.dragging = id);
  el.addEventListener("input", () => {
    $(lab).textContent = el.value + " 步/秒";
    sendSpeedSoon();          /* 拖动过程中也实时生效 */
  });
  el.addEventListener("change", () => { S.dragging = null; sendSpeedNow(); });
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

/* 连续旋转物料盘只允许逻辑仓位分度，不把角度命令当作绝对位置。 */
document.querySelectorAll("[data-slot]").forEach(b => {
  b.onclick = () => api(`/api/competition/cmd?cmd=tray_to&slot=${b.dataset.slot}`);
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
  const anyBlack = j.b.some(Boolean);
  if (j.mode === 1) {
    $("linestate").textContent = j.ls ? "循迹：在线" : "循迹：未检测到线";
    $("linestate").className = "badge " + (j.ls ? "ok" : "bad");
  } else {
    $("linestate").textContent = anyBlack ? "手动 · 探头检测到黑线" : "手动 · 未检测到黑线";
    $("linestate").className = "badge";
  }

  const modeName = j.mode === 1 ? "PID 循迹" : (j.mode === 2 ? "定步平移" : "手动");
  $("modebadge").textContent = modeName;
  $("modebadge").className = "badge " + (j.mode ? "ok" : "");
  $("mMan").classList.toggle("active", j.mode === 0);
  $("mLine").classList.toggle("active", j.mode === 1);

  $("enChassis").checked = !!j.en[0];
  $("enLift").checked = !!j.en[1];

  if (S.dragging !== "th") {
    $("th").value = j.th; $("thv").textContent = j.th.toFixed(2) + " V";
    $("inv").checked = !!j.inv;
    $("rev").checked = !!j.rev;
  }
  if (S.dragging !== "cs") applySpeedValue("cs", "csv", j.cs);
  if (S.dragging !== "ls") applySpeedValue("ls", "lsv", j.lsp);

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
/* ---- 比赛初始化、标定与自动流程 ---- */
const colorNames = ["空", "红", "绿", "蓝"];
async function compCommand(url, success) {
  const j = await api(url);
  $("compMsg").textContent = j.ok ? success : (j.msg || "命令被拒绝");
  await pollComp();
}
$("compStart").onclick = () => {
  if (window.confirm("已确认机械周围安全、全部初始化与标定完成？")) {
    compCommand("/api/competition/cmd?cmd=start", "比赛流程已启动");
  }
};
$("compAbort").onclick = () => compCommand("/api/stop", "已全局中止，请重新初始化");
$("zZero").onclick = () => {
  if (window.confirm("确认当前 Z 滑块处于运输安全零点？")) {
    compCommand("/api/competition/cmd?cmd=z_zero", "Z 零点已确认");
  }
};
$("trayZero").onclick = () => {
  if (window.confirm("确认当前盘位已对准装卸点且物料盘为空？")) {
    compCommand("/api/competition/cmd?cmd=tray_zero", "slot0 已确认，仓位已清空");
  }
};
$("trayNext").onclick = () => compCommand("/api/competition/cmd?cmd=tray_next", "已请求正转 60°");

const cfgElements = Array.from(document.querySelectorAll("[data-cfg]"));
cfgElements.forEach(el => {
  /* 失焦不再自动丢弃未保存的输入：只有“保存并确认”成功后才恢复轮询覆盖。 */
  el.addEventListener("focus", () => S.cfgEditing = true);
  el.addEventListener("input", () => S.cfgEditing = true);
});
$("cfgSave").onclick = async () => {
  const params = new URLSearchParams({ confirm: "1" });
  cfgElements.forEach(el => params.set(el.dataset.cfg, el.value));
  const j = await api("/api/competition/config?" + params.toString());
  $("compMsg").textContent = j.ok ? "标定已保存并确认" : (j.msg || "保存被拒绝");
  if (j.ok) {
    S.cfgEditing = false;
  }
  await pollComp();
};

function renderComp(j) {
  S.comp = j;
  $("compState").textContent = j.state;
  $("compState").className = "badge " + (j.error !== "NONE" ? "bad" : (j.running ? "ok" : ""));
  const seconds = Math.max(0, j.run_ms / 1000).toFixed(1);
  $("compInfo").textContent = `已装 ${j.picked}/3 · 已放 ${j.placed}/3 · 运行 ${seconds}s · Z=${j.zpos} 步` +
    ` | 初始化：Z ${j.zok ? "有效" : "无效"} / 盘 ${j.tray.zero ? "有效" : "无效"}` +
    ` / 标定 ${j.cfgok ? "已确认" : "未确认"} | 故障：${j.error}`;

  let slots = "";
  for (let i = 0; i < 6; i++) {
    const color = j.tray.colors[i];
    slots += `<div class="slot ${i === j.tray.slot ? "current" : ""}">slot ${i}` +
      `<br><span class="c${color}">${colorNames[color] || "?"}</span></div>`;
  }
  $("compSlots").innerHTML = slots;
  $("visionInfo").textContent = `K230 ${j.link.online ? "在线" : "离线"} · 模式 ${j.link.mode}/${j.link.ack}` +
    ` · ${j.vision.detected ? (j.vision.stable ? "稳定" : "检测中") : "无目标"}` +
    ` · ${colorNames[j.vision.color] || "未知"} (${j.vision.cx},${j.vision.cy})` +
    ` · 置信度 ${j.vision.conf}% · 数据年龄 ${j.vision.age}ms · 盘 slot${j.tray.slot}→${j.tray.target}`;

  $("compStart").disabled = !!j.running;
  $("zZero").disabled = !!j.running;
  $("trayZero").disabled = !!j.running;
  $("trayNext").disabled = !!j.running;
  if (!S.cfgEditing) {
    cfgElements.forEach(el => {
      if (document.activeElement !== el &&
          Object.prototype.hasOwnProperty.call(j.cfg, el.dataset.cfg)) {
        el.value = j.cfg[el.dataset.cfg];
      }
    });
  }
}

async function pollComp() {
  if (S.compPolling) return;
  S.compPolling = true;
  try {
    const r = await fetch("/api/competition", { cache: "no-store" });
    if (!r.ok) throw new Error("http " + r.status);
    renderComp(await r.json());
  } catch (e) {
    $("compState").textContent = "状态离线";
    $("compState").className = "badge bad";
  } finally {
    S.compPolling = false;
  }
}

poll();
setInterval(poll, 50);
pollComp();
setInterval(pollComp, 200);
</script>
</body>
</html>
)rawliteral";
