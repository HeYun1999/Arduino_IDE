#include <bluefruit.h>
#include <OneWire.h>

// -------------------------- 1. 核心配置宏（低功耗开关/参数） --------------------------
#define DEBUG_MODE          1       // 0=关闭串口（低功耗），1=开启调试
#define ONE_WIRE_BUS        0       // M601 DQ引脚（根据实际接线修改）
#define BLE_ADV_INTERVAL    8192    // 广播间隔：8192*0.625ms=5120ms
#define BLE_TX_POWER        -8      // 发射功率：-8dBm（降低耗电）
#define M601_SEARCH_RETRY   2       // M601搜索重试次数
#define TEMP_UPDATE_THRESH  0.03    // 温度变化阈值（超过才更新广播）
// 手动定义串口引脚（根据开发板修改，此处以常见的D6/D7为例）
#define TX_PIN              6
#define RX_PIN              7

// -------------------------- 2. 全局变量 --------------------------
OneWire oneWire(ONE_WIRE_BUS);
BLEBeacon beacon;
bool m601_init_flag = false;
byte m601_rom_addr[8] = {0};
unsigned long last_wake_ms = 0;
float last_temp = -999.0;

// BLE参数
#define MANUFACTURER_ID   0x0059
uint8_t beaconUuid[16] = {0x01,0x12,0x23,0x34,0x45,0x56,0x67,0x78,
                          0x89,0x9a,0xab,0xbc,0xcd,0xde,0xef,0xf0};
uint8_t custom_mac_addr[6] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};

// -------------------------- 3. M601温感操作 --------------------------
void configM601(byte rom_addr[]) {
  oneWire.reset();                
  oneWire.select(rom_addr);       
  oneWire.write(0x4E);            // 写暂存器指令
  oneWire.write(0x00);            // Tha_Set_lsb
  oneWire.write(0x00);            // Tla_Set_lsb
  oneWire.write(0x0A);            // 1次/秒测量 + 高重复性
  delayMicroseconds(100);

  oneWire.reset();
  oneWire.select(rom_addr);
  oneWire.write(0x48);            // 保存配置到E2PROM
  delay(30);
#if DEBUG_MODE
  Serial.println("M601配置完成");
#endif
}

float readM601Temp(void) {
  if (!m601_init_flag) {
    bool found = false;
    for (int retry = 0; retry < M601_SEARCH_RETRY; retry++) {
      oneWire.reset();                
      delayMicroseconds(1000);
      if (oneWire.search(m601_rom_addr)) { 
        found = true;
        break;
      }
      oneWire.reset_search();         
      delay(200);                   
    }

    if (!found) {
#if DEBUG_MODE
      Serial.println("M601未找到（检查上拉/接线）");
#endif
      return -999.0;
    }

    if (m601_rom_addr[0] != 0x28) {
#if DEBUG_MODE
      Serial.println("M601设备类型错误");
#endif
      return -999.0;
    }
    if (!(m601_rom_addr[6] == 0x00 && m601_rom_addr[7] == 0x00) && 
        OneWire::crc8(m601_rom_addr, 7) != m601_rom_addr[7]) {
#if DEBUG_MODE
      Serial.println("M601 ROM CRC失败");
#endif
      return -999.0;
    }

    configM601(m601_rom_addr);
    m601_init_flag = true;
  }

  byte scratchpad[9];
  int16_t temp_raw;

  oneWire.reset();
  oneWire.select(m601_rom_addr);
  oneWire.write(0x44, 1);  // 启动测温

  unsigned long start_time = millis();
  while (true) {
    if (oneWire.read_bit() == 1) break;
    if (millis() - start_time > 20) {
#if DEBUG_MODE
      Serial.println("M601测温超时");
#endif
      return -999.0;
    }
    delayMicroseconds(50);
  }

  oneWire.reset();
  oneWire.select(m601_rom_addr);
  oneWire.write(0xBE);        
  delayMicroseconds(100);
  for (int i = 0; i < 9; i++) {
    scratchpad[i] = oneWire.read();
  }

  if (OneWire::crc8(scratchpad, 8) != scratchpad[8]) {
#if DEBUG_MODE
    Serial.println("M601数据CRC失败");
#endif
    return -999.0;
  }

  temp_raw = (scratchpad[1] << 8) | scratchpad[0];
  float temperature;
  if (temp_raw & 0x8000) {
    temp_raw = ~temp_raw + 1;
    temperature = 40 - (temp_raw * 0.00390625);
  } else {
    temperature = 40 + temp_raw * 0.00390625;
  }

  pinMode(ONE_WIRE_BUS, INPUT);
  return temperature;
}

// -------------------------- 4. BLE配置 --------------------------
bool setCustomMacAddress(uint8_t* custom_addr) {
  ble_gap_addr_t gap_addr;
  gap_addr.addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC;
  memcpy(gap_addr.addr, custom_addr, 6);

  if (Bluefruit.setAddr(&gap_addr)) {
#if DEBUG_MODE
    Serial.print("自定义MAC：");
    for (int i = 5; i >= 0; i--) {
      Serial.printf("%02X%s", gap_addr.addr[i], i>0?":":"");
    }
    Serial.println();
#endif
    return true;
  } else {
#if DEBUG_MODE
    Serial.println("MAC设置失败");
#endif
    return false;
  }
}

void startBeaconAdv() {
  // 配置信标参数（移除setRssi，通过构造函数初始化）
  beacon = BLEBeacon(beaconUuid, 0x0000, 0x0000, -54);  // 此处传入RSSI
  beacon.setManufacturer(MANUFACTURER_ID);

  // 广播配置（用addTxPower替代setTxPower）
  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_SCANNABLE_UNDIRECTED);
  Bluefruit.Advertising.setBeacon(beacon);
  Bluefruit.Advertising.setInterval(BLE_ADV_INTERVAL, BLE_ADV_INTERVAL);
  Bluefruit.Advertising.addTxPower();  // 添加发射功率信息
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.start(0);

#if DEBUG_MODE
  Serial.printf("BLE启动（间隔：%dms）\n", BLE_ADV_INTERVAL * 625 / 1000);
#endif
}

// -------------------------- 5. 低功耗优化 --------------------------
void optimizeGpio() {
  // 仅优化未使用的GPIO，跳过必要引脚
  for (uint8_t pin = 0; pin < 32; pin++) {
    if (pin != ONE_WIRE_BUS && pin != TX_PIN && pin != RX_PIN) {
      pinMode(pin, INPUT);
    }
  }
#if DEBUG_MODE == 0
  // 关闭串口引脚（低功耗模式）
  pinMode(TX_PIN, INPUT);
  pinMode(RX_PIN, INPUT);
#endif
}

// -------------------------- 6. 主程序 --------------------------
void setup() {
#if DEBUG_MODE
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("=== M601+BLE信标 ===");
#endif

  // 检查M601上拉
  pinMode(ONE_WIRE_BUS, INPUT);
  int dq_idle = digitalRead(ONE_WIRE_BUS);
#if DEBUG_MODE
  Serial.printf("M601 DQ电平：%d（1=正常）\n", dq_idle);
#endif
  if (dq_idle != 1) while (1);  // 上拉错误

  // 优化GPIO
  optimizeGpio();

  // 初始化BLE
  if (!Bluefruit.begin()) {
#if DEBUG_MODE
    Serial.println("BLE初始化失败");
#endif
    while (1);
  }
  Bluefruit.setName("M601-Beacon");
  setCustomMacAddress(custom_mac_addr);

  // 启动广播
  startBeaconAdv();

#if DEBUG_MODE
  Serial.println("初始化完成");
#endif
  last_wake_ms = millis();
}

void loop() {
  // 1秒唤醒一次
  if (millis() - last_wake_ms < 1000) {
    delay(10);
    return;
  }
  last_wake_ms = millis();

  // 读取温度
  float temperature = readM601Temp();
  if (temperature == -999.0) return;

  // 温度变化时更新广播
  if (fabs(temperature - last_temp) > TEMP_UPDATE_THRESH) {
#if DEBUG_MODE
    Serial.printf("温度：%.2f ℃\n", temperature);
#endif
    last_temp = temperature;

    uint16_t temp_int = (uint16_t)temperature;
    uint16_t temp_dec = (uint16_t)fabs((temperature - temp_int) * 100);
    beacon.setMajorMinor(temp_int, temp_dec);

    // 更新广播
    Bluefruit.Advertising.stop();
    Bluefruit.Advertising.setBeacon(beacon);
    Bluefruit.Advertising.start(0);
  }
}
