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
.pausebanner{display:none;margin:8px 0;padding:10px 12px;border-radius:10px;
background:rgba(245,158,11,.16);border:1px solid rgba(245,158,11,.55);
color:#fbbf24;font-size:13px;font-weight:600;line-height:1.5;
animation:pausepulse 1.2s ease-in-out infinite}
@keyframes pausepulse{0%,100%{box-shadow:0 0 0 rgba(245,158,11,0)}50%{box-shadow:0 0 14px rgba(245,158,11,.45)}}
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
.pidbox{border-top:1px solid var(--line);margin-top:10px;padding-top:2px}
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
    <h2>循迹探头 &amp; PID 调参 <span id="seq">seq -</span></h2>
    <div class="sensors" id="sensors"></div>
    <div class="row"><label>判定阈值</label><b class="mono" id="thv">4.00 V</b></div>
    <input type="range" id="th" min="4.0" max="5.0" step="0.01" value="4.00">
    <div class="switch"><span>黑线为高电平（极性反转）</span>
      <input type="checkbox" id="inv"></div>
    <div class="switch"><span>探头安装顺序反转（8 → 1）</span>
      <input type="checkbox" id="rev"></div>
    <div class="row"><span class="badge" id="linestate">循迹 --</span>
      <span class="mono" id="errv">err 0.00</span></div>

    <div class="pidbox">
      <div class="row" style="margin:10px 0 6px">
        <span class="badge" id="pidLine">循迹 --</span>
        <span class="mono">err <b id="pidErr" style="color:var(--acc)">0.00</b></span>
        <span class="mono">ω <b id="pidW" style="color:var(--acc)">0.00</b></span>
      </div>
      <div class="slider"><div class="top"><span>Kp 比例</span><b id="kpv">0.60</b></div>
        <input type="range" id="kp" min="0" max="5" step="0.05" value="0.6"></div>
      <div class="slider"><div class="top"><span>Ki 积分</span><b id="kiv">0.00</b></div>
        <input type="range" id="ki" min="0" max="2" step="0.01" value="0"></div>
      <div class="slider"><div class="top"><span>Kd 微分</span><b id="kdv">0.15</b></div>
        <input type="range" id="kd" min="0" max="1" step="0.01" value="0.15"></div>
      <div class="slider"><div class="top"><span>修正角速度上限 ω</span><b id="omv">1.5 rad/s</b></div>
        <input type="range" id="om" min="0" max="5" step="0.1" value="1.5"></div>
      <div class="slider"><div class="top"><span>加速度（5 轴共用，微步/秒²）</span><b id="accv">19200 微步/秒²</b></div>
        <input type="range" id="acc" min="1600" max="40000" step="400" value="19200"></div>
      <div class="btnrow" style="margin-top:6px">
        <button id="saveTune" class="primary">💾 立即保存到 Flash</button>
      </div>
      <div class="statusline" id="saveMsg" style="margin-top:6px;color:var(--ok)"></div>
      <div class="hint">条形高度=电压(0~5V 满量程)，橙线=阈值；探头变红表示判定为黑线。编号 1 在最左、8 在最右。
        阈值 4.00~5.00V（步进 0.01V）。Kp 过大会左右摆，先加 Kd 抑制；Ki 一般保持 0。
        转向角速度有 6 rad/s² 斜率限制，避免猛打方向过冲。参数修改后约 2 秒自动保存，下次上电自动加载。</div>
    </div>
  </section>

  <section class="card">
    <h2>运动控制 <span id="modebadge" class="badge">手动</span></h2>
    <div class="btnrow" style="margin-bottom:10px">
      <button id="estopBtn" class="stop" style="flex:1 1 100%;padding:13px;font-size:15px;font-weight:600">■ 急停（全部停止）</button>
      <button id="estopClear" class="primary" style="flex:1 1 100%;padding:12px;font-size:14px;display:none">✔ 解除急停，恢复控制</button>
    </div>
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
      <div class="top"><span>底盘航速（手动 / 循迹基础）</span><b id="csv">2400 微步/秒 · 150 整步</b></div>
      <input type="range" id="cs" min="800" max="4800" step="25" value="2400">
    </div>
    <div class="slider">
      <div class="top"><span>升降速度 M5</span><b id="lsv">6400 微步/秒 · 400 整步</b></div>
      <input type="range" id="ls" min="1600" max="10000" step="50" value="6400">
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
      <div class="top"><span>底座旋转 CH0 · 20kg（0~360°）</span><b id="a0">90°</b></div>
      <div class="srow">
        <button data-rng="s0" data-d="-1">−1°</button>
        <input type="range" id="s0" min="0" max="360" step="1" value="90">
        <button data-rng="s0" data-d="1">+1°</button>
        <button class="stop" data-rng="s0" data-mid="1">中位</button>
      </div>
    </div>

    <div class="srv">
      <div class="top"><span>物料盘 CH4 · SG90（连续舵机，用盘位按钮）</span><b id="a4">90°</b></div>
      <div class="srow">
        <button data-rng="s4" data-d="-1" disabled>−1°</button>
        <input type="range" id="s4" min="0" max="180" step="1" value="90" disabled>
        <button data-rng="s4" data-d="1" disabled>+1°</button>
        <button class="stop" data-rng="s4" data-mid="1" disabled>中位</button>
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

    <div class="srv">
      <div class="top"><span>位置舵机步进（CH0 / CH8）</span><b id="stepInfo">15°</b></div>
      <div class="row"><label>步进角（°）</label>
        <input id="stepDeg" type="number" min="1" max="90" step="1" value="15"
               style="width:90px;padding:6px;border-radius:8px;border:1px solid #273a4c;background:#17222e;color:#e6edf3">
      </div>
      <div class="btnrow">
        <button id="baseStepMinus">CH0 −步进</button>
        <button id="baseStepPlus">CH0 +步进</button>
        <button id="gripStepMinus">CH8 −步进</button>
        <button id="gripStepPlus">CH8 +步进</button>
      </div>
    </div>

    <div class="pulse">
      <div class="ttl"><span>连续舵机定时点动（仅 CH4 物料盘）</span><b id="pulseInfo">—</b></div>
      <div class="row" style="margin-top:6px"><label>通道</label>
        <select id="pCh">
          <option value="4">CH4 物料盘（连续）</option>
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
      CH0 底座是<b>连续旋转舵机</b>，用“角度差 × 标定用时”做定时点动：
      写目标角度后按标定的速度角与 180° 用时转动，到点自动回中位停止，
      单次最长 10 秒；逻辑行程 0~360°，超出一圈会被夹紧。
      CH8 夹爪是位置舵机（软限位取标定的开/合角度）。
      用“位置舵机步进”按指定角度（默认 15°）一点点移动。<br>
      CH0 的速度角 / 180° 用时在比赛卡片标定区调整（bspeed / bsweep）。
    </div>
  </section>

  <section class="card">
    <h2>比赛自动流程 <span id="compState" class="badge">MANUAL</span></h2>
    <div class="statusline" id="compInfo">等待状态…</div>
    <div class="pausebanner" id="compPauseBanner">站点触发：等待人工抓取...（抓取中）</div>
    <div class="btnrow" style="margin:10px 0">
      <button id="compStart" class="primary">▶ 启动三取三放</button>
      <button id="compAbort" class="stop">■ 全局中止</button>
      <button id="compExecute" class="primary" style="flex:1 1 100%;padding:14px;font-size:15px" disabled>▶ 执行抓取</button>
      <button id="compResume" class="primary" style="flex:1 1 100%;padding:12px;font-size:14px" disabled>跳过本站 / 恢复循迹</button>
    </div>
    <div class="row" style="margin:8px 0">
      <label>示教步骤</label>
      <select id="teachStep">
        <option value="bstow">底座运输角</option>
        <option value="bpick">底座地面抓取角</option>
        <option value="btray">底座盘位角</option>
        <option value="bplace">底座靶环角</option>
        <option value="gopen">夹爪张开角</option>
        <option value="gclose">夹爪闭合角</option>
        <option value="zpick">地面抓取 Z</option>
        <option value="ztray">盘内取放 Z</option>
        <option value="zplace">靶环放置 Z</option>
      </select>
    </div>
    <div class="btnrow">
      <button id="teachSave" class="primary" style="flex:1 1 100%;padding:12px">📌 保存当前姿态到该步骤（含 Flash）</button>
    </div>
    <div class="statusline" id="teachMsg" style="margin-top:6px;color:var(--ok)"></div>
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
/* 与 app_config.h 的 WHEEL_MICROSTEP / LIFT_MICROSTEP 保持一致 */
const MICROSTEP = 16;
function fmtSpeed(micro) {
  const full = micro / MICROSTEP;
  const fullText = Math.abs(full - Math.round(full)) < 0.05
    ? Math.round(full) : full.toFixed(1);
  return Math.round(micro) + " 微步/秒 · " + fullText + " 整步";
}
function applySpeedValue(id, lab, value) {
  const p = speedPending[id];
  if (p && performance.now() < p.until && Math.abs(p.v - value) > 1) {
    return;   /* 命令仍在途中 */
  }
  delete speedPending[id];
  $(id).value = value;
  $(lab).textContent = fmtSpeed(value);
}
[["cs", "csv"], ["ls", "lsv"]].forEach(([id, lab]) => {
  const el = $(id);
  el.addEventListener("pointerdown", () => S.dragging = id);
  el.addEventListener("input", () => {
    $(lab).textContent = fmtSpeed(+el.value);
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
  const max = ch === 0 ? 360 : 180;
  a = Math.max(0, Math.min(max, Math.round(a)));
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
    const max = s.ch === 0 ? 360 : 180;
    const v = Math.max(0, Math.min(max, +el.value + (e.deltaY < 0 ? 1 : -1)));
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
    const max = ch === 0 ? 360 : 180;
    const v = b.dataset.mid ? CENTER[ch]
                            : Math.max(0, Math.min(max, +el.value + (+b.dataset.d)));
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
  api(`/servo_stop?ch=${ch}`);
  $("pulseInfo").textContent = `CH${ch} 停止`;
};

/* ---- 位置舵机步进（CH0 / CH8），一次只走指定角度 ---- */
function stepServo(ch, sign) {
  const step = Math.max(1, Math.min(90, Math.round(+$("stepDeg").value || 15)));
  $("stepInfo").textContent = step + "°";
  const cur = S.status ? (ch === 0 ? S.status.srv[0] : S.status.srv[2]) : 90;
  const max = ch === 0 ? 360 : 180;
  const target = Math.max(0, Math.min(max, Math.round(cur) + sign * step));
  servoSet(ch, target, true);
  syncServoUI(ch, target);
}
$("stepDeg").oninput = () => {
  const v = Math.max(1, Math.min(90, Math.round(+$("stepDeg").value || 15)));
  $("stepInfo").textContent = v + "°";
};
$("baseStepMinus").onclick = () => stepServo(0, -1);
$("baseStepPlus").onclick = () => stepServo(0, 1);
$("gripStepMinus").onclick = () => stepServo(8, -1);
$("gripStepPlus").onclick = () => stepServo(8, 1);

/* ---- 急停 / 解除 ---- */
$("estopBtn").onclick = () => {
  stopMove(); stopLift();
  api("/api/estop");
};
$("estopClear").onclick = () => api("/api/estop/clear");

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
const pidMap = [
  ["kp", "kpv", 2, ""],
  ["ki", "kiv", 2, ""],
  ["kd", "kdv", 2, ""],
  ["om", "omv", 1, " rad/s"],
  ["acc", "accv", 0, " 微步/秒²"]
];
const pidPending = {};
let pidTimer = null;

function sendPidNow() {
  clearTimeout(pidTimer);
  const v = {
    kp: +$("kp").value, ki: +$("ki").value, kd: +$("kd").value,
    om: +$("om").value, acc: +$("acc").value
  };
  api(`/api/line?kp=${v.kp}&ki=${v.ki}&kd=${v.kd}&om=${v.om}&accel=${v.acc}`);
  /* 记录本次下发的值，防止 50ms 轮询用旧值把滑块弹回去后再把旧值发出去 */
  const until = performance.now() + 1500;
  for (const k of Object.keys(v)) pidPending[k] = { v: v[k], until };
}
function sendPidSoon() {
  clearTimeout(pidTimer);
  pidTimer = setTimeout(sendPidNow, 80);
}
function applyPidValue(id, lab, dec, unit, value) {
  if (S.dragging === id) return;
  const p = pidPending[id];
  if (p && performance.now() < p.until && Math.abs(p.v - value) > 1e-4) {
    return;   /* 服务端还没回显，保留本地值 */
  }
  delete pidPending[id];
  $(id).value = value;
  $(lab).textContent = Number(value).toFixed(dec) + unit;
}

pidMap.forEach(([id, lab, dec, unit]) => {
  const el = $(id);
  el.addEventListener("pointerdown", () => { S.dragging = id; });
  el.addEventListener("input", () => {
    $(lab).textContent = Number(el.value).toFixed(dec) + unit;
    sendPidSoon();       /* 拖动过程中就生效 */
  });
  el.addEventListener("change", () => { S.dragging = null; sendPidNow(); });
});

$("saveTune").onclick = async () => {
  const j = await api("/settings?save=1");
  $("saveMsg").textContent = j.ok ? "已保存到 Flash，下次上电自动加载"
                                  : (j.msg || "保存失败");
};

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

  $("estopClear").style.display = j.estop ? "" : "none";
  $("estopBtn").disabled = !!j.estop;
  $("estopBtn").textContent = j.estop ? "■ 已急停（点下方解除）" : "■ 急停（全部停止）";

  if (S.dragging !== "th") {
    $("th").value = j.th; $("thv").textContent = j.th.toFixed(2) + " V";
    $("inv").checked = !!j.inv;
    $("rev").checked = !!j.rev;
  }
  if (S.dragging !== "cs") applySpeedValue("cs", "csv", j.cs);
  if (S.dragging !== "ls") applySpeedValue("ls", "lsv", j.lsp);

  applyPidValue("kp", "kpv", 2, "", j.kp);
  applyPidValue("ki", "kiv", 2, "", j.ki);
  applyPidValue("kd", "kdv", 2, "", j.kd);
  applyPidValue("om", "omv", 1, " rad/s", j.om);
  applyPidValue("acc", "accv", 0, " 步/秒²", j.acc);

  $("pidErr").textContent = Number(j.err).toFixed(2);
  $("pidW").textContent = Number(j.w).toFixed(2);
  $("pidLine").textContent = j.mode === 1 ? (j.ls ? "在线" : "未检测到线") : "未启用";
  $("pidLine").className = "badge " + (j.mode === 1 ? (j.ls ? "ok" : "bad") : "");

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
$("compResume").onclick = () => {
  compCommand("/api/competition/cmd?cmd=resume", "已跳过本站，正在跨线恢复循迹…");
};
$("compExecute").onclick = () => {
  compCommand("/api/competition/cmd?cmd=execute", "已开始执行本站动作，请勿手动干预");
};
$("teachSave").onclick = async () => {
  const step = $("teachStep").value;
  const j = await api(`/api/competition/teach?step=${step}`);
  $("teachMsg").textContent = j.ok
    ? `已记录 ${step} = ${j.value}（已写入 Flash）`
    : (j.msg || "示教失败：请先停止运动");
  await pollComp();
};
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
  const paused = j.state === "DEBUG_PAUSE";
  $("compState").textContent = j.state;
  $("compState").className = "badge " + (paused ? "warn" : (j.error !== "NONE" ? "bad" : (j.running ? "ok" : "")));
  const isTarget = j.pk === 2;
  const pauseBanner = $("compPauseBanner");
  pauseBanner.style.display = paused ? "block" : "none";
  pauseBanner.textContent = j.cross
    ? "正在跨线，请稍候…"
    : (isTarget
        ? "站点触发（靶环站）：先人工对位/转盘选槽，再点“执行放置”｜可用方向键、升降和舵机"
        : "站点触发（物料站）：人工对位后点“执行抓取”｜可用方向键、升降和舵机");
  $("compExecute").textContent = isTarget ? "▶ 执行放置" : "▶ 执行抓取";
  $("compExecute").disabled = !paused || !!j.cross;
  $("compResume").disabled = !paused || !!j.cross;
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
  $("trayNext").disabled = !!j.running && !paused;
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
