# 次回リリース向けメモ（直接push分のみ）

PRを経由した変更は自動生成（generate_release_notes）で拾われるため、ここに書く必要はない。
jirogit/dev への直接push（fork限定ドキュメント等）だけをここに1行で書き足す。

リリース時：この内容をドラフトリリース本文にコピーしてから、このファイルを空にする。

---

- fix(jp-lbt): reduce backoff parameters for SF10 airtime (base_ms 2000→500, max_backoff 16000→4000) — c49d26d4
- feat(jp-release): restrict Off-Grid Repeat to 921.2MHz only (ARIB STD-T108 JP LBT range)
