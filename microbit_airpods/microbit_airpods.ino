/**
 * micro:bit v1 - AirPods Proximity Pairing (Bare-Metal NRF_RADIO BLE Advertiser)
 * 
 * 特徴:
 * - クールダウンなし（Aボタンを押したらいつでも即座に送信、押しっぱなしでも連続送信）
 * - AirPodsのモデルID（AirPods 1/2/3, Pro 1/2, Max の全6種）、MACアドレス、
 *   個体識別子・バッテリー状態をハードウェア乱数生成器(NRF_RNG)で毎回完全ランダム生成
 * - micro:bit v1 の5x5全25個LEDマトリクス（ROW1〜3: 26,27,28 / COL1〜9）を正確に制御し、
 *   送信中は全LEDが明るく点灯・点滅して視覚的にすぐわかる設計
 */

#include <Arduino.h>
#include <nrf.h>

// micro:bit v1 のピン定義 (variants/BBCmicrobit/variant.cpp 準拠)
const int PIN_BTN_A = 5; // Aボタン (P0.17, プルアップ, 押下時LOW)

// 5x5 LEDマトリクスの全ピン定義
const int LED_ROWS[3] = {26, 27, 28};                     // ROW1(P0.13), ROW2(P0.14), ROW3(P0.15)
const int LED_COLS[9] = {3, 4, 10, 23, 24, 25, 9, 7, 6};  // COL1〜COL9

// AirPods モデル識別子一覧 (6種類)
const uint8_t AIRPODS_MODELS[6][2] = {
  {0x02, 0x20}, // AirPods (第1世代)
  {0x0f, 0x20}, // AirPods (第2世代)
  {0x13, 0x20}, // AirPods (第3世代)
  {0x0e, 0x20}, // AirPods Pro (第1世代)
  {0x14, 0x20}, // AirPods Pro (第2世代)
  {0x0a, 0x20}  // AirPods Max
};

// BLE PDU バッファ (4バイト境界アラインメント必須)
// [0]: Header S0 (1B), [1]: Length (1B), [2..7]: AdvA MAC (6B), [8..38]: AdvData (31B)
static uint8_t ble_pdu[40] __attribute__((aligned(4)));

// 1回のボタン押下での最低送信時間 (ミリ秒) ※クールダウンは0秒
const unsigned long BURST_DURATION_MS = 1500;
const unsigned long ADV_INTERVAL_MS   = 20;

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

  // 送信出力: +4dBm (最大出力)
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

// 毎回ランダムなMACアドレス・AirPodsモデル・識別子でBLE PDUを組み立てる
void buildRandomAirPodsPdu() {
  // Byte 0: PDU Type = ADV_NONCONN_IND (0x02) | TxAdd = Random (0x40) -> 0x42
  ble_pdu[0] = 0x42;

  // Byte 1: Payload Length = 6 (MAC) + 31 (AdvData) = 37 bytes
  ble_pdu[1] = 37;

  // Byte 2..7: ランダムな送信元BLE MACアドレス (6バイト)
  for (int i = 0; i < 6; i++) {
    ble_pdu[2 + i] = getRandomByte();
  }
  ble_pdu[7] |= 0xC0; // Static Random Address 規格 (最上位2ビット = 11)

  // AirPods モデルを6種類からランダム選択
  uint8_t modelIdx = getRandomByte() % 6;

  // Byte 8..38: Apple Continuity Proximity Pairing Payload (31バイト)
  uint8_t *adv = &ble_pdu[8];
  adv[0]  = 0x1e;                         // Length: 30 bytes
  adv[1]  = 0xff;                         // AD Type: Manufacturer Specific Data
  adv[2]  = 0x4c;                         // Apple Company ID (Low)
  adv[3]  = 0x00;                         // Apple Company ID (High)
  adv[4]  = 0x07;                         // Continuity Type: Proximity Pairing (0x07)
  adv[5]  = 0x19;                         // Continuity Length: 25 bytes
  adv[6]  = 0x07;                         // Prefix
  adv[7]  = AIRPODS_MODELS[modelIdx][0];  // ランダム選択されたAirPodsモデルID (1)
  adv[8]  = AIRPODS_MODELS[modelIdx][1];  // ランダム選択されたAirPodsモデルID (2)
  adv[9]  = 0x75;                         // Status (Pairing mode)
  adv[10] = 0xaa;                         // Battery / Charging flags
  adv[11] = (getRandomByte() & 0x7F);     // ランダムなバッテリー残量/状態
  adv[12] = 0x01;                         // Lid open counter / flag
  adv[13] = 0x00;                         // Color (0x00 = White)
  adv[14] = 0x00;

  // Byte 15..30 (16バイト): 個体識別・暗号化ハッシュ領域を毎回ランダム化
  for (int i = 15; i < 31; i++) {
    adv[i] = getRandomByte();
  }
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
  }
}

void setup() {
  Serial.begin(115200);

  // Aボタン初期化
  pinMode(PIN_BTN_A, INPUT);

  // 5x5 全LEDピンをOUTPUTに初期化
  for (int r = 0; r < 3; r++) {
    pinMode(LED_ROWS[r], OUTPUT);
  }
  for (int c = 0; c < 9; c++) {
    pinMode(LED_COLS[c], OUTPUT);
  }

  // 起動確認のため全LEDを2回キラッと点滅させる
  for (int i = 0; i < 2; i++) {
    setAllLeds(true);
    delay(150);
    setAllLeds(false);
    delay(150);
  }

  initBleRadio();

  Serial.println(F("========================================"));
  Serial.println(F("micro:bit v1 AirPods Random BLE Notifier"));
  Serial.println(F("No cooldown / Full random ID / Full LED"));
  Serial.println(F("========================================"));
}

void loop() {
  // Aボタンが押されたら即座に送信（クールダウンなし）
  if (digitalRead(PIN_BTN_A) == LOW) {
    Serial.println(F("[BLE] Sending Random AirPods Notification..."));

    // ランダムなAirPodsモデル・MACアドレス・識別子を構築
    buildRandomAirPodsPdu();

    // 送信開始: 全25個のLEDを点灯
    setAllLeds(true);

    unsigned long startTime = millis();
    int packetCount = 0;

    // 最低1.5秒間（またはAボタンを押し続けている間）送信し続ける
    while ((millis() - startTime < BURST_DURATION_MS) || (digitalRead(PIN_BTN_A) == LOW)) {
      transmitBleAdvOnce();
      packetCount++;

      // 送信中であることがわかりやすいよう、約100ms(5回)ごとにLEDを高速点滅
      if ((packetCount % 5) == 0) {
        setAllLeds((packetCount / 5) % 2 == 0);
      }

      // ボタンを長押ししている場合は、500ms(25回)ごとに新しいランダムAirPods個体へ切り替える
      if ((packetCount % 25) == 0) {
        buildRandomAirPodsPdu();
      }

      delay(ADV_INTERVAL_MS);
    }

    // 送信終了: 全LED消灯
    setAllLeds(false);
    Serial.println(F("[BLE] Stopped. Ready immediately."));
  }
}
