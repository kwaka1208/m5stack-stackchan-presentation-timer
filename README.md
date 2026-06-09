# m5stack-stackchan-presentation-timer

M5Stack **StackChan**(CoreS3 ベース)で動くプレゼン用カウントダウンタイマーです。
残り時間に応じて顔の表情が変化し、カウントダウン中は首を上下にうなずかせます。

![platform](https://img.shields.io/badge/platform-M5Stack%20StackChan%20(CoreS3)-orange)
![framework](https://img.shields.io/badge/framework-Arduino-blue)
![license](https://img.shields.io/badge/license-MIT-green)

## 機能

- デフォルト 90 分からのカウントダウン(画面タッチの 4 ボタンで操作)
  - `-5min` / `RESET` / `START`・`PAUSE` / `+5min`
- 残り時間で表情が変化(通常 → 15分以下:真剣 → 5分以下:焦り → 時間切れ:びっくり)
- 残り 30/15/5/1 分・10 秒以下でビープ、時間切れでトーン
- **カウントダウン中は首(垂直サーボ)を上下にゆっくりうなずかせる**

## ハードウェア

- **M5Stack StackChan**(ホスト: M5Stack CoreS3 / ESP32-S3)
- 首振り: **FEETECH SCS0009 シリアルバスサーボ ×2**(水平=ID1 / 垂直=ID2)
  - PWM ではなく **UART1 (1 Mbps, TX=GPIO6 / RX=GPIO7)** で制御
  - サーボ電源(VM)は **PY32 I/O エクスパンダ(I2C 0x6F)の pin0** を High にして投入

## ビルドと書き込み

[PlatformIO CLI](https://platformio.org/install/cli) が必要です。

```bash
# ビルド
pio run

# 書き込みとシリアルモニターの起動
# ポートは自動検出されます。必要に応じて --upload-port で指定してください。
pio run -t upload -t monitor
```

## 調整

首振りやサーボの中心位置は `src/main.cpp` の定数で調整できます。

| 定数 | 既定値 | 説明 |
|------|--------|------|
| `SERVO_SWAY_AMPLITUDE_DEG` | `15.0` | うなずきの上下振れ幅 (度) |
| `SERVO_SWAY_PERIOD_S` | `2.5` | 首振り周期 (秒) |
| `PITCH_NEUTRAL_DEG` | `35.0` | うなずきの中心角 (推奨 5〜85 度内) |
| `YAW_ZERO_POS` / `PITCH_ZERO_POS` | `460` / `620` | サーボ中心位置(個体ごとに校正が必要な場合あり) |

> サーボの中心位置は個体差があります。首が傾く場合は `YAW_ZERO_POS` / `PITCH_ZERO_POS` を実機に合わせてください。

## ライセンス / クレジット

本リポジトリは **MIT License**(`LICENSE`)で公開しています。以下の MIT ライセンスのソフトウェアを利用しています。

- [M5Unified](https://github.com/m5stack/M5Unified) — MIT (© M5Stack)
- [M5GFX](https://github.com/m5stack/M5GFX) — MIT (© M5Stack)
- FEETECH FTServo ドライバ(`lib/scservo/`) — MIT (© ftservo)。
  [M5Stack/StackChan-BSP](https://github.com/m5stack/StackChan-BSP) 経由で取り込み。詳細は
  [`lib/scservo/README.md`](lib/scservo/README.md) を参照。
