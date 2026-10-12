# micro:bit v1 - AirPods 近接ペアリング通知送信

BBC micro:bit v1 (Nordic nRF51822) を使用して、Aボタンを押したときに iPhone などの iOS 端末に向けて「新しいAirPods」の近接ペアリング通知（セットアップポップアップ）をBLEで送信するArduinoスケッチです。

---

## 仕組みと特徴

1. **Apple Continuity Proximity Pairing のエミュレーション**
   - Apple Company Identifier (`0x004C`) および Proximity Pairing サービス (`0x07`) を含む31バイトの生BLEアドバタイジングパケットを送信します。
2. **レート制限・スパム防止対策**
   - Aボタンが押されたときのみ **約2.5秒間** アドバタイズを送信し、自動で停止します。
   - 送信終了後は **3秒間のクールダウン時間** を設けており、ボタンを連打してもiOS側のスパム抑制（レート制限）に引っかかりにくい設計にしています。
3. **視覚フィードバック**
   - 送信中はmicro:bit本体のLED（左上）が点灯し、送信停止で消灯します。

---

## 必要な準備（Arduino IDE の設定手順）

### 1. ボードマネージャURLの追加
1. Arduino IDE を開き、**[ファイル] -> [基本設定]**（macOSは [Arduino IDE] -> [設定]）を開きます。
2. **「追加のボードマネージャのURL」** に以下のURLを入力して [OK] をクリックします：
   ```text
   https://sandeepmistry.github.io/arduino-nRF5/package_nRF5_boards_index.json
   ```

### 2. nRF5 ボードパッケージのインストール
1. **[ツール] -> [ボード] -> [ボードマネージャ...]** を開きます。
2. 検索バーに `nRF5` と入力し、**「Nordic Semiconductor nRF5 Boards by Sandeep Mistry」** をインストールします。

### 3. ライブラリのインストール
1. **[ツール] -> [ライブラリを管理...]** を開きます。
2. 検索バーに `BLEPeripheral` と入力し、**「BLEPeripheral by Sandeep Mistry」** をインストールします。

### 4. ツールメニューの設定（※重要）
micro:bit を USB ケーブルで PC に接続し、[ツール] メニューで以下のように設定します：

| 項目 | 設定値 | 備考 |
| :--- | :--- | :--- |
| **ボード** | `BBC micro:bit` | |
| **SoftDevice** | `S110` | Bluetoothスタック |
| **Low Frequency Clock** | `Synthesized` | **※必須**: micro:bitには外付け32kHz水晶がないため |
| **シリアルポート** | micro:bitのCOMポート | |
| **書込み装置 (Programmer)** | `CMSIS-DAP` | |

### 5. 初回のみ: SoftDevice（ブートローダ）の書き込み
micro:bit に SoftDevice (S110) が入っていないとBLEが動作しません。
1. 上記の設定を確認した上で、**[ツール] -> [ブートローダを書き込む]** を実行します。
2. 数十秒で書き込みが完了します。

---

## 書き込みと使い方

1. `microbit_airpods/microbit_airpods.ino` を Arduino IDE で開きます。
2. **[マイコンボードに書き込む]** ボタン（右矢印アイコン）を押して書き込みます。
3. **テスト方法**:
   - iPhone の **画面を点灯・ロック解除** して、Bluetoothをオンにしておきます。
   - micro:bit の **Aボタン** を押します。
   - 左上のLEDが約2.5秒間点灯し、iPhoneの画面下部にAirPodsの接続カード（ポップアップ）が表示されます。

---

## 注意事項

- **検証環境での利用**: 周囲の他人の端末にも通知が届くため、必ず私有地・自宅等のプライベートな環境で、ご自身の端末に対して検証を行ってください。
- **iOSの挙動**: iOS 17.2以降では、端末がロック状態または画面消灯状態の場合、未認証BLE通知のポップアップが自動抑制されることがあります。必ず画面を点灯させた状態で近づけて確認してください。
