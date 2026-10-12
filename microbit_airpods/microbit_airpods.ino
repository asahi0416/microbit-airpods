/**
 * micro:bit v1 - AirPods Proximity Pairing (Bare-Metal NRF_RADIO BLE Advertiser)
 * 
 * 特徴:
 * - SoftDevice (S110) の別途書き込みが不要（NRF_RADIOハードウェアレジスタを直接制御）
 * - Aボタンを押すたびにハードウェア乱数生成器(NRF_RNG)で新しいランダムMACアドレスを生成
 * - 1回の押下で約2.5秒間のみ20ms間隔でCh37/38/39にアドバタイズし、その後3秒間クールダウン
 */

#include <Arduino.h>
#include <nrf.h>

// micro:bit v1 のピン定義
const int PIN_BTN_A    = 5;  // Aボタン (プルアップ, 押下時LOW)
const int PIN_LED_ROW1 = 13; // LED マトリクス Row 1
const int PIN_LED_COL1 = 4;  // LED マトリクス Col 1 (左上LED)

// Apple Continuity Proximity Pairing (AirPods Pro) アドバタイズデータ (31バイト)
const uint8_t airpods_adv_data[31] = {
  0x1e,             // Length: 30 bytes
  0xff,             // AD Type: Manufacturer Specific Data (0xFF)
  0x4c, 0x00,       // Company ID: Apple Inc. (0x004C)
  0x07,             // Continuity Type: Proximity Pairing (0x07)
  0x19,             // Continuity Length: 25 bytes
  0x07,             // Prefix / Subtype
  0x02, 0x20,       // Device Model: AirPods Pro (0x0220) ※AirPods 3は 0x0f, 0x20
  0x75,             // Status
  0xaa, 0x30, 0x01, // Battery levels
  0x00, 0x00, 0x45, 0x12, 0x12, 0x12,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// BLE PDU バッファ (4バイト境界アラインメント必須)
// [0]: Header S0 (1B), [1]: Length (1B), [2..7]: AdvA MAC (6B), [8..38]: AdvData (31B)
static uint8_t ble_pdu[40] __attribute__((aligned(4)));

// タイミング設定
const unsigned long ADV_DURATION_MS = 2500; // 送信時間: 2.5秒
const unsigned long COOLDOWN_MS     = 3000; // 連続送信防止クールダウン: 3秒
const unsigned long ADV_INTERVAL_MS = 20;   // パケット送信間隔: 20ms

unsigned long lastAdvEndTime = 0;

void setLed(bool on) {
  if (on) {
    digitalWrite(PIN_LED_ROW1, HIGH);
    digitalWrite(PIN_LED_COL1, LOW);
  } else {
    digitalWrite(PIN_LED_ROW1, LOW);
    digitalWrite(PIN_LED_COL1, HIGH);
  }
}

// ハードウェア乱数生成器から1バイト取得
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
  // 高周波水晶クロック (16MHz HFCLK) を起動（BLE無線通信に必須）
  NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
  NRF_CLOCK->TASKS_HFCLKSTART = 1;
  while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0);

  // 無線を一旦停止・リセット
  NRF_RADIO->TASKS_DISABLE = 1;
  delayMicroseconds(100);

  // 送信出力: +4dBm (最大)
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
  NRF_RADIO->CRCPOLY = 0x0000065BUL; // BLE CRC多項式
  NRF_RADIO->CRCINIT = 0x00555555UL; // BLE Advertising CRC初期値

  // ショートカット設定: READY -> START, END -> DISABLE
  NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk;
}

// BLE PDUパケットの組み立て（毎回ランダムなMACアドレスを設定）
void buildAirPodsPdu() {
  // Byte 0: PDU Type = ADV_NONCONN_IND (0x02) | TxAdd = Random (0x40) -> 0x42
  ble_pdu[0] = 0x42;

  // Byte 1: Payload Length = 6 (MAC) + 31 (AdvData) = 37 bytes
  ble_pdu[1] = 37;

  // Byte 2..7: Random Static MAC Address (6 bytes)
  for (int i = 0; i < 6; i++) {
    ble_pdu[2 + i] = getRandomByte();
  }
  ble_pdu[7] |= 0xC0; // Static Random Address の規格に従い最上位2ビットを11にする

  // Byte 8..38: AirPods Proximity Pairing Data (31 bytes)
  memcpy(&ble_pdu[8], airpods_adv_data, 31);
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
      // 送信完了＆自動DISABLEまで待機（約400us）
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN_A, INPUT);
  pinMode(PIN_LED_ROW1, OUTPUT);
  pinMode(PIN_LED_COL1, OUTPUT);
  setLed(false);

  initBleRadio();

  Serial.println(F("========================================"));
  Serial.println(F("micro:bit v1 AirPods BLE Notifier Ready!"));
  Serial.println(F("Press Button A to send AirPods popup."));
  Serial.println(F("========================================"));
}

void loop() {
  unsigned long now = millis();

  // クールダウン期間が経過しており、かつAボタンが押された場合
  if (now - lastAdvEndTime >= COOLDOWN_MS || lastAdvEndTime == 0) {
    if (digitalRead(PIN_BTN_A) == LOW) {
      Serial.println(F("[BLE] Button A pressed! Sending AirPods notification..."));
      setLed(true);

      // 新しいランダムMACアドレスでPDUを構築
      buildAirPodsPdu();

      // 2.5秒間、20ms間隔でアドバタイズ信号を送信
      unsigned long startTime = millis();
      while (millis() - startTime < ADV_DURATION_MS) {
        transmitBleAdvOnce();
        delay(ADV_INTERVAL_MS);
      }

      setLed(false);
      lastAdvEndTime = millis();
      Serial.println(F("[BLE] Finished. Cooldown started (3s)."));
    }
  }
}
