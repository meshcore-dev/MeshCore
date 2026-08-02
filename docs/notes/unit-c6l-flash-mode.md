# M5Stack Unit C6L 起動不能の真因調査（2026-08）

> **TL;DR (EN)** — The boot loop on Unit C6L is not a firmware-code problem.
> The board manifest `esp32-c6-devkitm-1.json` sets `flash_mode: qio`, and this
> chip does not boot reliably from a QIO image. `pio run -t upload` hardcodes
> `--flash_mode dio` on a separate code path, so the bug stays invisible during
> development while every distributed `merged.bin` ships broken.
> Fix: one line, `board_build.flash_mode = dio` (PR #3098).

**結論: 真因はボードマニフェスト `esp32-c6-devkitm-1.json` の `flash_mode: qio`。修正は `variants/m5stack_unit_c6l/platformio.ini` に `board_build.flash_mode = dio` を1行加えるだけ（PR #3098）。**

**以下は、PI4IO / TCXO電圧 / I2Cピン / TX_LED の4項目を真因と誤認して PR #3088 を出し、再bisectで撤回するまでの経緯。途中まで読むと誤結論を拾うので注意。**

---

## 症状

USBシリアルポートが接続/切断を繰り返し、デバイスが起動しない。upstream `meshcore-dev/MeshCore` の dev ブランチ由来ファームで発生。Issue #2229 に複数の報告者（comsorg / vikkut / alestandby / svenrobbie）。

一方、M5Burner 経由で配布されている TheRealHaoLiu フォーク版（MeshCore v1.11.0）は正常起動する。

## 真因

`esp32-c6-devkitm-1.json`（ボードマニフェスト）が `flash_mode: qio` を指定している。ESP32-C6 はQIOイメージから安定して起動しない。

**なぜ開発中は見えなかったか** — PlatformIO の書き込み経路は2つあり、挙動が違う。

| 経路 | flash_mode | 結果 |
|---|---|---|
| `pio run -t upload` | `--flash_mode dio` をハードコード（マニフェスト無視） | 起動する |
| `mergebin`（= GitHub Releases 配布の merged.bin = Webフラッシャー経路） | マニフェストの `qio` を採用 | **起動しない** |

つまり開発者は常に dio で書き込んでおり、ユーザーは常に qio を掴まされていた。

## 確認方法

1. 同一ファームウェアに対し `--flash_mode` だけを変えて書き込み。qio で2回クラッシュループを再現、dio で2回解消
2. 書き込み後に `esptool image_info` でオンチップのヘッダを読み戻し、実際にどのモードが載ったかを確認
3. 配布済み merged.bin（jp-v1.8.1 および upstream companion-v1.16.0）のヘッダを確認 → **両方ともQIO**。`--flash_mode` 指定なしで書き込むと同じクラッシュループを再現

## 誤認の経緯（PR #3088）

M5Burner フォークとの差分6項目を洗い出した：

1. TCXO電圧 1.8V → 3.0V
2. I2Cピン 16/17 → 10/8
3. PI4IO `OUT_H_IM` レジスタ書き込み
4. TX_LED GPIO15
5. `SX126X_RXEN` と `GPS_TX` の GPIO5 競合
6. SSD1306 ディスプレイ対応の欠落

このうち1〜4を適用したところクラッシュが解消したように見えたため、PR #3088 を提出し jp-v1.8.1 をリリースした。

翌日クリーンな手順で再検証したところ、**upstream素のコード（変更ゼロ）でも dio なら起動する**ことが判明。4項目はいずれも起動可否に無関係だった。PR #3088 はクローズし、PR #3098 を提出。

**なぜ誤認したか（2つの汚染源）**

- **ビルドキャッシュ**: PlatformIO のインクリメンタルビルドがヘッダファイルの変更を拾わず、bisect中に偽の失敗が混入していた
- **書き込み手順**: `esptool.py write_flash 0x0 firmware.bin` を merged / non-merged の区別なく使っていた

## 教訓

- **ESP32系では「コードのbisect」より先に「書き込み条件（オフセット・flash_mode）の固定」を疑う。** 変数が固定されていない状態でのbisectは、結論ではなくノイズを生む
- 検証ビルドの前には必ず `pio run -t clean` ＋ `rm -rf .pio/build/<env>`
- 書き込みは `pio run -t upload` に統一。esptool 直書き・オフセット手動指定はしない
- 「開発環境では動くが配布物では動かない」類のバグは、ビルド成果物そのもの（ヘッダ等）を読み戻して確認する

## 積み残し（この件とは別テーマ）

- **PI4IO の LNA(P5) 有効化** — upstream では未初期化のまま。受信感度に影響する可能性があるが、起動問題とは無関係。別途検証の余地あり
- **GPIO5 競合**（`SX126X_RXEN=5` / `GPS_TX=5`）— 別PRとして分離（PR #3089、ブランチ `fix/unit-c6l-gpio5-conflict`）

## 関連

- Issue #2229 — 症状の報告先。訂正コメント投稿済み
- PR #3088 — クローズ（誤った修正）
- PR #3098 — 正しい修正（1行）
- jp-v1.8.1 — Unit C6L 向け merged.bin がQIOヘッダのため実質破損版
- jp-v1.8.2 — 4つのC6L merged.bin 資産すべてがDIOヘッダであることを確認したうえでリリース
