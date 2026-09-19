# MPU6050 IMU on Arduino UNO Q -- Linux (MPU) side
#
# MCU が Bridge.notify("imu_data", ...) で push してくる姿勢角を受け取り、
# 10Hz に間引いてブラウザへ Socket.IO で broadcast する。
# ブラウザからの操作は Bridge.call() で MCU へ中継する。

import json
import time

from arduino.app_utils import App, Bridge
from arduino.app_bricks.web_ui import WebUI

ui = WebUI()

# ── 共有状態（すべて数値 or 真偽値。文字列で持たない）─────────
state = {
    "roll": 0.0,
    "pitch": 0.0,
    "yaw": 0.0,
    "temp": 0.0,
    "fresh": False,   # 直近1秒以内に MCU から受信したか
    "hz": 0.0,        # 実測の受信レート
}

BROADCAST_INTERVAL = 0.1     # 10 Hz
_last_rx = 0.0               # 最後に受信した時刻
_last_broadcast = 0.0
_rx_count = 0
_rx_window_start = time.monotonic()


def _broadcast(room=None):
    ui.send_message("imu_update", state, room=room)


def _notice(text, room=None):
    ui.send_message("imu_notice", {"text": text}, room=room)


# ── MCU → Python: 姿勢角の受信 ──────────────────────────────
# ★ メソッド名 "imu_data" は sketch 側の Bridge.notify と完全一致させる
def on_imu_data(roll: float, pitch: float, yaw: float, temp: float):
    global _last_rx, _rx_count

    state["roll"] = round(float(roll), 2)
    state["pitch"] = round(float(pitch), 2)
    state["yaw"] = round(float(yaw), 2)
    state["temp"] = round(float(temp), 2)

    _last_rx = time.monotonic()
    _rx_count += 1


Bridge.provide("imu_data", on_imu_data)

def on_imu_status(ok: bool, err_code: int, err_msg: str):
    state["sensor_ok"] = bool(ok)
    if ok:
        _notice("IMUセンサーが利用可能になりました")
    else:
        _notice(f"IMUセンサーエラー (code={err_code}): {err_msg}")

Bridge.provide("imu_status", on_imu_status)

# ── ブラウザ → Python → MCU ────────────────────────────────
def on_calibrate(sid, data):
    try:
        res = json.loads(Bridge.call("imu_calibrate", 500, timeout=10))
    except Exception as e:
        _notice(f"キャリブレーション失敗: {e}")
        return
    if res.get("ok"):
        _notice("キャリブレーション完了 "
                f"(bias gx={res['gx']} gy={res['gy']} gz={res['gz']} deg/s)")
    else:
        _notice(f"キャリブレーション失敗: {res.get('err')}")


def on_reset_yaw(sid, data):
    try:
        Bridge.call("imu_reset_yaw", 0, timeout=5)
        _notice("yaw を 0 にリセットしました")
    except Exception as e:
        _notice(f"yaw リセット失敗: {e}")


def on_read_raw(sid, data):
    try:
        res = json.loads(Bridge.call("imu_read_raw", 0, timeout=5))
    except Exception as e:
        _notice(f"生値の取得に失敗: {e}")
        return
    _notice(
        f"WHO_AM_I=0x{res.get('who', 0):02X}  "
        f"acc=({res.get('ax')}, {res.get('ay')}, {res.get('az')})  "
        f"gyr=({res.get('gx')}, {res.get('gy')}, {res.get('gz')})"
    )

def on_connect(sid):
    _broadcast(room=sid)
    _notice("接続しました。UNO Q を静止させてからキャリブレーションしてください。",
            room=sid)

ui.on_connect(on_connect)
ui.on_message("calibrate", on_calibrate)
ui.on_message("reset_yaw", on_reset_yaw)
ui.on_message("read_raw", on_read_raw)

# ── 定期ループ: 間引いて broadcast ──────────────────────────
def loop():
    global _last_broadcast, _rx_count, _rx_window_start

    now = time.monotonic()

    # 受信レートを1秒窓で計測
    if now - _rx_window_start >= 1.0:
        state["hz"] = round(_rx_count / (now - _rx_window_start), 1)
        _rx_count = 0
        _rx_window_start = now

    # 1秒以上受信がなければ stale 扱い
    state["fresh"] = bool(_last_rx > 0.0 and (now - _last_rx) < 1.0)

    if now - _last_broadcast >= BROADCAST_INTERVAL:
        _last_broadcast = now
        _broadcast()

    time.sleep(0.02)
    
App.run(user_loop=loop)