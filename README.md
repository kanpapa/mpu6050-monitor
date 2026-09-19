# mpu6050-monitor

## 概要

Arduino UNO Q に MPU6050(GY-521)を接続し、MCU側で姿勢角(roll/pitch/yaw)を計算し、Python(Linux)側でブラウザにリアルタイム表示するArduino App Labアプリです。

- MCU側: I2Cでセンサーを読み取り、相補フィルタで姿勢角を計算して Bridge 経由でpush
- Python側: 受け取った値を間引いてWebUIへSocket.IOでbroadcast
- ブラウザ: roll/pitch/yawのバー表示 + CSSのみによる簡易3D姿勢表示

計算をMCU側に置いているのは、100Hz程度の等間隔サンプリングが必要な積分処理を
Linux側のスケジューラ揺れから切り離すためです。

## ディレクトリ構成

開発環境はArduino App Lab 0.10.0を使用しています。

```
mpu6050_monitor/
├── sketch
|   ├── sketch.ino   # MCU側: センサー読み取り・姿勢計算・Bridge RPC
|   └── sketch.yaml  # Arduino App Lab用設定
├── python/main.py      # Python側: Bridge受信・WebUI配信
├── app.yaml
├── README.md
└── assets/
    ├── libs
    |   ├── arduino.js
    |   └── socket.io.min.js
    ├── app.js
    ├── index.html
    └── style.css

```

## 必要なもの

- Arduino UNO Q
- MPU6050搭載モジュール(GY-521)
- ジャンパー線
- ブレッドボード
- Arduino App Lab 0.10.0
  - Add Sketch Libraryでインストール:
    - `Adafruit MPU6050`
    - 依存関係で他のライブラリも自動インストールされます

## 配線

| MPU6050(GY-521) | Arduino UNO Q | 備考 |
|---|---|---|
| VCC | 3V3 | 5Vトレラントではないため必ず3.3V系へ |
| GND | GND | |
| SDA | **D21**(コネクタ表記 "SDA") | ⚠️ 下記の注意参照 |
| SCL | **D20**(コネクタ表記 "SCL") | ⚠️ 下記の注意参照 |
| AD0 | GND または未接続 | 未接続でも内部プルダウンで0x68 |
| XDA / XCL | 未使用 | 補助I2C(磁気センサ用)。今回は未使用 |

### ⚠️ 重要: A4/A5ではなくD20/D21を使うこと

UNO QにはI2Cに使えるピンが複数ありますが、実際に検証した結果は以下の通りです。

- **A4/A5(SDA2/SCL2)**: I2C3のI2Cバス。今回の構成(デフォルトの`Wire`)では**応答しません**
- **D20/D21(コネクタ表記 "SCL"/"SDA"、数字なし)**: I2C2のI2Cバス。デフォルトの`Wire`はこちらに対応

## セットアップ

1. 上記の配線を行う
2. Arduino App Labで新規アプリを作成し、`sketch/`・`python/`・`assets/`の内容を配置
3. 必要なライブラリ(Adafruit MPU6050)をインストール
4. App Labから実行(Run)
5. 起動直後、基板を静止させた状態でジャイロの自動キャリブレーションが走る(起動時に約500サンプル取得)
6. WebUIで自動的にブラウザで表示される

## WebUIの操作方法

- **ジャイロ キャリブレーション**: 基板を静止させた状態で押す。ジャイロのバイアス値を再計測する
- **yawリセット**: yawの基準を0に戻す(磁気センサがないため、yawは相対値でドリフトする)
- **生値を読む**: WHO_AM_Iや加速度・ジャイロの生値を確認する。配線トラブルの切り分けに使う

## Bridge API仕様

**MCU → Python(push, `Bridge.notify`)**

| メソッド名 | 引数 | 型 | 説明 |
|---|---|---|---|
| `imu_data` | roll, pitch, yaw, temp | float×4 | 20Hzで姿勢角をpush |
| `imu_status` | ok, err_code, err_msg | bool, int, string | センサー状態に変化があったときのみpush |

**Python → MCU(call, `Bridge.call`)**

| メソッド名 | 引数 | 戻り値 |
|---|---|---|
| `imu_calibrate` | samples: int | JSON文字列 `{"ok":true,"gx":...,"gy":...,"gz":...}` |
| `imu_reset_yaw` | dummy: int(0固定) | JSON文字列 `{"ok":true}` |
| `imu_read_raw` | dummy: int(0固定) | JSON文字列(WHO_AM_I・生値) |
| `imu_get_status` | dummy: int(0固定) | JSON文字列(現在のセンサー状態) |

## エラー処理

- 起動時、`mpu.begin()`が失敗した場合は最大5回・200ms間隔でリトライ
- 稼働中に読み取りが10回連続で失敗した場合、センサーを「未接続」状態にし、以後3秒おきに自動で再初期化を試みる(配線を直せば再起動不要で復帰)
- 状態が変化した際は`imu_status`でPython側に通知され、WebUIのログに表示される
