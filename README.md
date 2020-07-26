# BF-018
JJY Simulator for M5StickC

M5StickC で動作する標準電波（JJY）シミュレータ

### 1. 概要
　M5StickC で電波時計のための JJY もどきを生成します。JJY が届かないところにある電波時計の時刻合わせができます。Wifi 経由 NTP で時刻を取得し、GPIO から JJY 信号を出力します。
 
解説記事:
https://qiita.com/BotanicFields/items/a78c80f947388caf0d36

### 2. アンテナの準備
　送信にはアンテナが必要です。GPIO26 と GND 間に 1kΩ 程度の抵抗を途中に挟んで 1m 程度の電線を接続して実験できます。電線を電波時計の至近距離に這わせると、電波時計が電線からの磁界を受信してくれます。

動作の様子:
https://www.youtube.com/watch?v=S_t3g5wqyh8

### 3. 使い方

- 電源投入またはリセット後、Wifi に接続し、NTP で日時を合わせ、即座に標準信号を送出を開始します。 
- ボタン A で LCD に状況を表示できます。
- JJY 信号オンを内蔵 LED でモニターできます。

### 4. Wifi 接続
　WiFiManager を使用しています。使い方は、WiFiManager の説明を参照ください。

https://github.com/tzapu/WiFiManager

### 4. LCD
　ボタンA で状況（SSID, IPアドレス, 日付、時刻, LED モニタ－オン・オフ）を表示します。約 5 秒で自動的に表示が消えます。

### 5. LED
　ボタンB で LED によるモニターをオン・オフできます。

