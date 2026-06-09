# scservo

FEETECH シリアルバスサーボ(SCS / SMS / STS シリーズ)制御用の ESP-IDF コンポーネント。
本プロジェクトでは StackChan の首(SCS0009)を UART 経由で制御するために使用しています。

## 出典 (Attribution)

このコンポーネント内の `src/` のドライバ(`SCS`, `SCSerial`, `SCSCL`, `INST.h` など)は、
FEETECH 公式の **FTServo_Arduino** ライブラリを、M5Stack の
[StackChan-BSP](https://github.com/m5stack/StackChan-BSP)
(`src/drivers/FTServo_Arduino`)を経由して取り込んだものです。

- 原典: FEETECH FTServo (https://github.com/ftservo/FTServo_Arduino)
- ライセンス: MIT (Copyright (c) 2024 ftservo) — 本ディレクトリの [LICENSE](./LICENSE) を参照

ESP-IDF の UART ドライバ上で動作する実装(`SCSerial`)をそのまま利用しているため、
Arduino フレームワークには依存しません。本プロジェクトでは SCS0009 用に `SCSCL` のみを
ビルド対象にしています。
