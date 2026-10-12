/**
 * micro:bit v1 - AirPods Proximity Pairing (Bare-Metal NRF_RADIO BLE Advertiser)
 * 
 * 改善点 (PDCA対応):
 * 1. ペイロードのステータス・マジックバイト (0x75, 0xaa, 0x30, 0x01 ... 0x45, 0x12, 0x12, 0x12) を
 *    AppleJuiceの実績ある正規フォーマットに固定し、不正値でiOSに弾かれる問題を解消
 * 2. Aボタンを押すごとに「送信元MACアドレス(6B)」と「AirPodsモデルID(6種)」をランダム生成し、
 *    送信中の2.5秒間は途中でIDを変えずに15ms間隔で高密度送信することでiOSの認識率を最大化
 * 3. PDUタイプを ADV_IND (0x40: 接続可能アドバタイズ) に設定し、iOSのペアリング検知感度を向上
 * 4. クールダウンなし、送信中は5x5全25個のLEDが高速点滅
 */

#include <Arduino.h>
#include <nrf.h>

// micro:bit v1 のピン定義 (variants/BBCmicrobit/variant.cpp 準拠)
const int PIN_BTN_A = 5; // Aボタン (P0.17, プルアップ, 押下時LOW)

// 5x5 LEDマトリクスの全ピン定義
const int LED_ROWS[3] = {26, 27, 28};                     // ROW1(P0.13), ROW2(P0.14), ROW3(P0.15)
const int LED_COLS[9] = {3, 4, 10, 23, 24, 25, 9, 7, 6};  // COL1〜COL9

// AirPods モデル識別子一覧 (全6種類)
const uint8_t AIRPODS_MODELS[6][2] = {
  {0x02, 0x20}, // AirPods (第1世代)
  {0x0f, 0x20}, // AirPods (第2世代)
  {0x13, 0x20}, // AirPods (第3世代)
  {0x0e, 0x20}, // AirPods Pro (第1世代)
  {0x14, 0x20}, // AirPods Pro (第2世代)
  {0x0a, 0x20}  // AirPods Max
};

// Apple Continuity Proximity Pairing 正規テンプレート (31バイト)
// ※ Byte 7, 8 にモデルIDが入る
const uint8_t AIRPODS_TEMPLATE[31] = {
  0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07,
  0x0e, 0x20, // [7],[8]: Model ID (動的にランダム書き換え)
  0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
  0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// BLE PDU バッファ (4バイト境界アラインメント必須)
static uint8_t ble_pdu[40] __attribute__((aligned(4)));

// 1回のボタン押下での送信時間 (iOSが確実に検知できるよう2.5秒間同一IDを維持して連射)
const unsigned long BURST_DURATION_MS = 2500;
const unsigned long ADV_INTERVAL_MS   = 15; // 15ms間隔の高密度送信

// 5x5 全25個のLEDを一括ON/OFF
void setAllLeds(bool on) {
  for (int r = 0; r < 3; r++) {
    digitalWrite(LED_ROWS[r], on ? HIGH : LOW);
  }
  for (int c = 0; c < 9; c++) {
    digitalWrite(LED_COLS[c], on ? LOW : HIGH);
  }
}

// ハードウェア乱数生成器(NRF_RNG)から1バイト取得
uint8_t getRandomByte() {
  NRF_RNG->EVENTS_VALRDY = 0;
  NRF_RNG->TASKS_START = 1;
  while (NRF_RNG->EVENTS_VALRDY == 0);
  uint8_t val = (uint8_t)NRF_RNG->VALUE;
  NRF_RNG->TASKS_STOP = 1;
  return val;
}

// NRF_RADIO を BLE 1Mbps アドバタイズ用に初期化
void initBleRadio() {
  // 高周波水晶クロック (16MHz HFCLK) を起動
  NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
  NRF_CLOCK->TASKS_HFCLKSTART = 1;
  while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0);

  NRF_RADIO->TASKS_DISABLE = 1;
  delayMicroseconds(100);

  // 送信出力: +4dBm (nRF51822 ハードウェア最大出力)
  NRF_RADIO->TXPOWER = (RADIO_TXPOWER_TXPOWER_Pos4dBm << RADIO_TXPOWER_TXPOWER_Pos);

  // モード: BLE 1Mbit
  NRF_RADIO->MODE = (RADIO_MODE_MODE_Ble_1Mbit << RADIO_MODE_MODE_Pos);

  // BLE Advertising Access Address: 0x8E89BED6
  NRF_RADIO->BASE0     = 0x89BED600UL;
  NRF_RADIO->PREFIX0   = 0x0000008EUL;
  NRF_RADIO->TXADDRESS = 0x00UL;

  // パケット構成0: S0=1バイト, LENGTH=8ビット, S1=0ビット
  NRF_RADIO->PCNF0 = (1 << RADIO_PCNF0_S0LEN_Pos) |
                     (8 << RADIO_PCNF0_LFLEN_Pos) |
                     (0 << RADIO_PCNF0_S1LEN_Pos);

  // パケット構成1: Whitening有効, Little Endian, BaseAddr=3バイト, MaxLen=255
  NRF_RADIO->PCNF1 = (RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos) |
                     (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) |
                     (3 << RADIO_PCNF1_BALEN_Pos) |
                     (0 << RADIO_PCNF1_STATLEN_Pos) |
                     (255 << RADIO_PCNF1_MAXLEN_Pos);

  // CRC設定: 24-bit (3バイト), アドレス部はCRC計算から除外
  NRF_RADIO->CRCCNF  = (RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos) |
                       (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos);
  NRF_RADIO->CRCPOLY = 0x0000065BUL;
  NRF_RADIO->CRCINIT = 0x00555555UL;

  // ショートカット設定: READY -> START, END -> DISABLE
  NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk;
}

// ボタンを押すたびに新しいランダムMACアドレス＆ランダムAirPodsモデルでPDUを構築
void buildRandomAirPodsPdu() {
  // Byte 0: PDU Type = ADV_IND (0x00: 接続可能アドバタイズ) | TxAdd = Random (0x40) -> 0x40
  ble_pdu[0] = 0x40;

  // Byte 1: Payload Length = 6 (MAC) + 31 (AdvData) = 37 bytes
  ble_pdu[1] = 37;

  // Byte 2..7: ランダムな送信元BLE MACアドレス (6バイト: ハードウェアアドレス)
  for (int i = 0; i < 6; i++) {
    ble_pdu[2 + i] = getRandomByte();
  }
  ble_pdu[7] |= 0xC0; // Static Random Address 規格 (最上位2ビット = 11)

  // Byte 8..38: 正規テンプレートをコピーした上でモデルIDをランダム選択
  uint8_t *adv = &ble_pdu[8];
  memcpy(adv, AIRPODS_TEMPLATE, 31);

  uint8_t modelIdx = getRandomByte() % 6;
  adv[7] = AIRPODS_MODELS[modelIdx][0];
  adv[8] = AIRPODS_MODELS[modelIdx][1];
}

// BLE Advertising Channels (37, 38, 39) で1回ずつパケットを送信
void transmitBleAdvOnce() {
  const uint8_t channels[3] = {37, 38, 39};
  const uint8_t freqs[3]    = { 2, 26, 80}; // 2402MHz, 2426MHz, 2480MHz

  NRF_RADIO->PACKETPTR = (uint32_t)ble_pdu;

  for (int i = 0; i < 3; i++) {
    NRF_RADIO->FREQUENCY   = freqs[i];
    NRF_RADIO->DATAWHITEIV = channels[i];

    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_TXEN      = 1;
    while (NRF_RADIO->EVENTS_DISABLED == 0) {
      // 送信完了まで待機
    }
    delayMicroseconds(150); // チャンネル間の微小ガードタイム
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN_A, INPUT);

  for (int r = 0; r < 3; r++) {
    pinMode(LED_ROWS[r], OUTPUT);
  }
  for (int c = 0; c < 9; c++) {
    pinMode(LED_COLS[c], OUTPUT);
  }

  // 起動確認のため全LEDを2回点滅
  for (int i = 0; i < 2; i++) {
    setAllLeds(true);
    delay(150);
    setAllLeds(false);
    delay(150);
  }

  initBleRadio();

  Serial.println(F("========================================"));
  Serial.println(F("micro:bit v1 AirPods BLE Notifier v3"));
  Serial.println(F("========================================"));
}

void loop() {
  // Aボタンが押されたら即座に送信開始（クールダウンなし）
  if (digitalRead(PIN_BTN_A) == LOW) {
    // 今回の送信セッション用のランダムMACアドレス＆ランダムAirPodsモデルを生成
    buildRandomAirPodsPdu();

    setAllLeds(true);

    unsigned long startTime = millis();
    int packetCount = 0;

    // 2.5秒間、同じ個体の電波を15ms間隔で高密度に維持してiOSに確実に認識させる
    while (millis() - startTime < BURST_DURATION_MS) {
      transmitBleAdvOnce();
      packetCount++;

      // 約90ms(6回)ごとに全LEDを高速点滅
      if ((packetCount % 6) == 0) {
        setAllLeds((packetCount / 6) % 2 == 0);
      }

      delay(ADV_INTERVAL_MS);
    }

    setAllLeds(false);
  }
}
