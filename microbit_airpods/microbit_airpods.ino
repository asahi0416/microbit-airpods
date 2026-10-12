/**
 * micro:bit v1 - AirPods Proximity Pairing (BLE Advertisement)
 * 
 * 説明:
 * micro:bit v1 (Nordic nRF51822) 上で動作し、
 * Aボタンを押すと Apple Continuity Proximity Pairing (AirPodsセットアップ通知) の
 * BLEアドバタイズ信号を約2.5秒間送信します。
 * 
 * Arduino IDE 設定手順:
 * 1. ボードマネージャURLに追加:
 *    https://sandeepmistry.github.io/arduino-nRF5/package_nRF5_boards_index.json
 * 2. ボードマネージャから「Nordic Semiconductor nRF5 Boards」をインストール
 * 3. ツールメニュー設定:
 *    - ボード: "BBC micro:bit"
 *    - SoftDevice: "S110"
 *    - Low Frequency Clock: "Synthesized" (重要: micro:bitには外付け水晶がないため)
 *    - Programmer: "CMSIS-DAP"
 * 4. 初回のみ: [ツール] -> [ブートローダを書き込む] を実行 (S110 SoftDeviceを書き込み)
 * 5. ライブラリマネージャから "BLEPeripheral" (by Sandeep Mistry) をインストール
 * 6. スケッチをコンパイル＆マイコンに書き込み
 */

#include <BLEPeripheral.h>

// micro:bit v1 のピン定義
const int PIN_BTN_A = 5;    // Aボタン (プルアップ, 押下時LOW)
const int PIN_LED_ROW1 = 13; // LED Row 1
const int PIN_LED_COL1 = 4;  // LED Col 1 (左上LED)

// BLEPeripheral インスタンス (SoftDeviceの初期化に使用)
BLEPeripheral blePeripheral = BLEPeripheral();

// Apple Continuity Proximity Pairing (AirPods Pro) パケット (31バイト)
// 構造:
//  - 0x1e: 長さ (30バイト)
//  - 0xff: AD Type (Manufacturer Specific Data)
//  - 0x4c, 0x00: Apple Company ID (0x004C, リトルエンディアン)
//  - 0x07: Type (Proximity Pairing)
//  - 0x19: 長さ (25バイト)
//  - 0x07: プレフィックス
//  - 0x02, 0x20: デバイスモデルID (AirPods Pro)
//    ※ AirPods 3: 0x0f, 0x20 / AirPods Max: 0x0a, 0x20
//  - 残り: 状態・バッテリー・暗号化ダミーデータ
const uint8_t airpods_packet[31] = {
  0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x02, 0x20,
  0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// 状態管理変数
bool isAdvertising = false;
unsigned long advStartTime = 0;
unsigned long lastAdvEndTime = 0;

// タイミング定数 (ミリ秒)
const unsigned long ADV_DURATION_MS = 2500;  // 1回の送信時間: 2.5秒
const unsigned long COOLDOWN_MS = 3000;      // 連続送信防止クールダウン: 3秒

void setLed(bool on) {
  // micro:bit v1のLEDマトリクス: Row=HIGH, Col=LOWで点灯
  if (on) {
    digitalWrite(PIN_LED_ROW1, HIGH);
    digitalWrite(PIN_LED_COL1, LOW);
  } else {
    digitalWrite(PIN_LED_ROW1, LOW);
    digitalWrite(PIN_LED_COL1, HIGH);
  }
}

void startAirPodsAdvertising() {
  // 生アドバタイズパケットを設定 (31バイト)
  sd_ble_gap_adv_data_set(airpods_packet, sizeof(airpods_packet), NULL, 0);

  // アドバタイズパラメータ設定
  ble_gap_adv_params_t adv_params;
  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.type = BLE_GAP_ADV_TYPE_ADV_NONCONN_IND; // 非接続型アドバタイズ
  adv_params.p_peer_addr = NULL;
  adv_params.fp = BLE_GAP_ADV_FP_ANY;
  adv_params.p_whitelist = NULL;
  adv_params.interval = 32; // 20ms (32 * 0.625ms: 高速間隔で送信して検出率を上げる)
  adv_params.timeout = 0;   // タイムアウトなし (手動で停止)

  // アドバタイズ開始
  sd_ble_gap_adv_start(&adv_params);

  isAdvertising = true;
  advStartTime = millis();
  setLed(true); // 送信中LED点灯
  Serial.println(F("[BLE] AirPods通知 送信開始"));
}

void stopAirPodsAdvertising() {
  // アドバタイズ停止
  sd_ble_gap_adv_stop();

  isAdvertising = false;
  lastAdvEndTime = millis();
  setLed(false); // 送信終了LED消灯
  Serial.println(F("[BLE] AirPods通知 送信停止 (クールダウン開始)"));
}

void setup() {
  Serial.begin(115200);

  // ピン初期化
  pinMode(PIN_BTN_A, INPUT); // Aボタン (内蔵プルアップ)
  pinMode(PIN_LED_ROW1, OUTPUT);
  pinMode(PIN_LED_COL1, OUTPUT);
  setLed(false);

  // BLEPeripheralを使ってSoftDeviceを初期化
  blePeripheral.begin();

  // 初期状態ではアドバタイズを停止しておく
  sd_ble_gap_adv_stop();

  Serial.println(F("========================================"));
  Serial.println(F("micro:bit v1 AirPods BLE Notifier Ready!"));
  Serial.println(F("Aボタンを押すとAirPodsの通知パケットを送信します。"));
  Serial.println(F("========================================"));
}

void loop() {
  // BLEイベント処理 (内部ポーリング)
  blePeripheral.poll();

  unsigned long now = millis();

  if (isAdvertising) {
    // 送信中: 指定時間(2.5秒)経過したら停止
    if (now - advStartTime >= ADV_DURATION_MS) {
      stopAirPodsAdvertising();
    }
  } else {
    // 待機中: クールダウン期間が過ぎており、かつAボタンが押された場合
    if (now - lastAdvEndTime >= COOLDOWN_MS) {
      if (digitalRead(PIN_BTN_A) == LOW) {
        startAirPodsAdvertising();
      }
    }
  }
}
