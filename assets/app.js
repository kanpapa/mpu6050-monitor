const socket = io();
const statusEl = document.getElementById("status");
const logEl    = document.getElementById("log");

// ±90deg を 0..100% にマップ（yaw だけ ±180deg）
function setBar(id, value, range) {
  const v = Math.max(-range, Math.min(range, value));
  const center = 50;
  const pct = (v / range) * 50;          // -50 .. +50
  const el = document.getElementById(id);
  el.style.left  = (pct >= 0 ? center : center + pct) + "%";
  el.style.width = Math.abs(pct) + "%";
}

function fmt(v) {
  return (v >= 0 ? "+" : "") + v.toFixed(1) + "°";
}

function log(text) {
  const t = new Date().toLocaleTimeString();
  logEl.textContent = `[${t}] ${text}\n` + logEl.textContent;
}

// ★ Python 側の送信イベント名 "imu_update" と完全一致させる
socket.on("imu_update", (d) => {
  if (!d) return;

  document.getElementById("v-roll").textContent  = fmt(d.roll);
  document.getElementById("v-pitch").textContent = fmt(d.pitch);
  document.getElementById("v-yaw").textContent   = fmt(d.yaw);
  document.getElementById("v-temp").textContent  = d.temp.toFixed(1);
  document.getElementById("v-hz").textContent    = d.hz.toFixed(1);

  setBar("b-roll",  d.roll,  90);
  setBar("b-pitch", d.pitch, 90);
  setBar("b-yaw",   d.yaw,  180);

    // ── 3D姿勢表示の更新 ─────────────────────────────────────
  const board = document.getElementById("board3d");
  if (board) {
    // roll: 左右の傾き, pitch: 前後の傾き, yaw: 上から見た回転
    board.style.transform =
      `rotateZ(${d.roll}deg) rotateX(${-d.pitch}deg) rotateY(${d.yaw}deg)`;
  }

  // fresh は真偽値。文字列比較しない
  if (d.fresh === false) {
    statusEl.className = "status stale";
    statusEl.textContent = "● MCU 無応答";
  } else {
    statusEl.className = "status connected";
    statusEl.textContent = "● Streaming";
  }
});

socket.on("imu_notice", (d) => { if (d && d.text) log(d.text); });

socket.on("connect",       () => { statusEl.className = "status connected";
                                   statusEl.textContent = "● Connected"; });
socket.on("disconnect",    () => { statusEl.className = "status disconnected";
                                   statusEl.textContent = "● Disconnected"; });
socket.on("connect_error", () => { statusEl.className = "status connecting";
                                   statusEl.textContent = "Connecting…"; });

document.getElementById("btn-cal").addEventListener("click", () => {
  log("キャリブレーション開始… 基板を静止させてください");
  socket.emit("calibrate", {});
});
document.getElementById("btn-yaw").addEventListener("click",
  () => socket.emit("reset_yaw", {}));
document.getElementById("btn-raw").addEventListener("click",
  () => socket.emit("read_raw", {}));