# CHANGELOG

## [Unreleased]
### 2026-10-12
- **日付**: 2026-10-12
- **変更内容**:
  - `microbit_airpods/microbit_airpods.ino` をベアメタル `NRF_RADIO` 直接制御方式に改修（動的ランダムMACアドレス生成機能付き）
  - `arduino-cli` によるビルド環境構築および micro:bit 実機（COM5）への書き込み完了
  - ワンクリック書き込み用バッチファイル `flash.bat` と `.gitignore` の作成
  - `README.md` の更新
- **理由**: SoftDevice (S110) の別途書き込みを不要にし、単体で即座に書き込み・動作可能にするため、またiOSのMACアドレスキャッシュによるスパム抑制を回避するため
- **影響範囲**: スケッチ本体、ビルド・書き込みスクリプト、ドキュメント

- **日付**: 2026-10-12
- **変更内容**:
  - `microbit_airpods/microbit_airpods.ino` の作成
  - `README.md` の作成（Arduino IDEの設定手順、書き込み手順、トラブルシューティング）
- **理由**: micro:bit v1のAボタン押下時にiOS端末へAirPods近接ペアリング通知を送信するスケッチと解説手順を提供するため
- **影響範囲**: プロジェクトルート（スケッチファイルおよびドキュメントの新規作成）


