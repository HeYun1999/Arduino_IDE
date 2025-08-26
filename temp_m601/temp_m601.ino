#include <bluefruit.h>
#include <OneWire.h>

// 提前声明中断服务函数
void tempReadyISR();

// -------------------------- 1. 硬件引脚与核心实例定义 --------------------------
#define ONE_WIRE_BUS 0  // M601 DQ引脚（依赖外部10kΩ上拉）
OneWire oneWire(ONE_WIRE_BUS);

BLEBeacon beacon;                  // BLE信标实例
bool m601_init_flag = false;       // M601初始化标记
byte m601_rom_addr[8] = {0};       // M601 ROM地址

// 低功耗关键变量
volatile bool temp_ready_flag = false; // 测温完成中断标志
float last_temp = -999.0;              // 上一次温度（用于阈值判断）
const float TEMP_THRESHOLD = 0.02;      // 温度变化阈值（超过才更新广播）
const uint16_t BLE_ADV_INTERVAL_MIN = 1600; // BLE最小广播间隔（1600*0.625ms=1000ms）
const uint16_t BLE_ADV_INTERVAL_MAX = 3200; // BLE最大广播间隔（3200*0.625ms=2000ms）

// -------------------------- 2. BLE与M601参数 --------------------------
#define MANUFACTURER_ID   0x0059  // 制造商ID
uint8_t beaconUuid[16] = {         // 自定义UUID
  0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78,
  0x89, 0x9a, 0xab, 0xbc, 0xcd, 0xde, 0xef, 0xf0
};
uint8_t custom_mac_addr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}; // 自定义MAC

// -------------------------- 3. 中断服务函数（M601测温完成唤醒） --------------------------
// DQ引脚上升沿触发（M601测温完成后DQ从低变高）
void tempReadyISR() {
  temp_ready_flag = true;
}

// -------------------------- 4. M601配置与温度读取 --------------------------
void configM601(byte rom_addr[]) {
  oneWire.reset();                
  oneWire.select(rom_addr);       
  oneWire.write(0x4E);            // 写暂存器指令
  oneWire.write(0x00);            // Tha_Set_lsb（默认）
  oneWire.write(0x00);            // Tla_Set_lsb（默认）
  oneWire.write(0x0A);            // 1次/秒 + 高重复性
  delayMicroseconds(180);         // 10kΩ上拉适配

  oneWire.reset();
  oneWire.select(rom_addr);
  oneWire.write(0x48);            // 配置保存到E2PROM
  delay(40);                      // E2PROM写周期
  Serial.println("M601配置完成：1次/秒测量");
}

float readM601Temp(void) {
  // 首次初始化：搜索设备+校验
  if (!m601_init_flag) {
    bool found = false;
    for (int retry = 0; retry < 5; retry++) { // 多轮搜索提高成功率
      oneWire.reset();                
      delayMicroseconds(1500);       // 10kΩ适配：复位后等待
      if (oneWire.search(m601_rom_addr)) { 
        found = true;
        break;
      }
      oneWire.reset_search();         
      delay(300);                     
    }
    if (!found) {
      Serial.println("M601：未找到设备（检查上拉/接线）");
      return -999.0;
    }

    // ROM地址校验
    if (m601_rom_addr[0] != 0x28) {
      Serial.println("M601：设备类型错误（首字节应为0x28）");
      return -999.0;
    }
    if (!(m601_rom_addr[6] == 0x00 && m601_rom_addr[7] == 0x00) && 
        OneWire::crc8(m601_rom_addr, 7) != m601_rom_addr[7]) {
      Serial.println("M601：ROM CRC校验失败");
      return -999.0;
    }

    configM601(m601_rom_addr);
    m601_init_flag = true;
    // 初始化后配置中断（测温完成唤醒CPU）
    attachInterrupt(digitalPinToInterrupt(ONE_WIRE_BUS), tempReadyISR, RISING);
  }

  // 读取温度核心逻辑
  byte scratchpad[9];
  int16_t temp_raw;

  // 1. 启动测温（寄生电源模式）
  oneWire.reset();
  oneWire.select(m601_rom_addr);
  oneWire.write(0x44, 1);  // 启动测温（第2参数1=寄生电源）
  temp_ready_flag = false; // 重置中断标志

  // 2. 等待测温完成（使用低功耗方式替代忙等）
  unsigned long start_time = millis();
  while (!temp_ready_flag) {
    // 使用低功耗延迟替代sleep，兼容更多平台
    delay(1);
    if (millis() - start_time > 50) { // 超时冗余
      Serial.println("M601：测温超时");
      return -999.0;
    }
  }

  // 3. 读取暂存器数据
  oneWire.reset();
  oneWire.select(m601_rom_addr);
  oneWire.write(0xBE);        // 读暂存器指令
  delayMicroseconds(200);     // 10kΩ适配
  for (int i = 0; i < 9; i++) {
    scratchpad[i] = oneWire.read();
  }

  // 4. CRC校验
  if (OneWire::crc8(scratchpad, 8) != scratchpad[8]) {
    Serial.println("M601：数据CRC失败");
    return -999.0;
  }

  // 5. 温度转换
  temp_raw = (scratchpad[1] << 8) | scratchpad[0];
  float temperature;
  if (temp_raw & 0x8000) {  // 负温度
    temp_raw = ~temp_raw + 1;
    temperature = 40 - (temp_raw * 0.00390625);
  } else {  // 正温度
    temperature = 40 + temp_raw * 0.00390625;
  }

  return temperature;
}

// -------------------------- 5. BLE MAC地址设置 --------------------------
bool setCustomMacAddress(uint8_t* custom_addr) {
  ble_gap_addr_t gap_addr;
  gap_addr.addr_type = 1;  // 静态随机地址（符合蓝牙规范）
  memcpy(gap_addr.addr, custom_addr, 6);

  if (Bluefruit.setAddr(&gap_addr)) {
    Serial.print("自定义MAC（大端序）：");
    for (int i = 5; i >= 0; i--) {
      Serial.printf("%02X", gap_addr.addr[i]);
      if (i > 0) Serial.print(":");
    }
    Serial.println();
    return true;
  } else {
    Serial.println("MAC设置失败！");
    return false;
  }
}

// -------------------------- 6. BLE信标广播配置 --------------------------
void startBeaconAdv() {
  beacon.setManufacturer(MANUFACTURER_ID);
  
  // 低功耗广播配置
  Bluefruit.Advertising.setBeacon(beacon);  
  Bluefruit.Advertising.restartOnDisconnect(true);  
  // 广播间隔：1000~2000ms（间隔越长，功耗越低）
  Bluefruit.Advertising.setInterval(BLE_ADV_INTERVAL_MIN, BLE_ADV_INTERVAL_MAX);  
  Bluefruit.Advertising.setFastTimeout(1);  // 快速广播1秒后进入慢速
  Bluefruit.Advertising.start(0);           // 无限广播

  Serial.println("BLE信标启动（低功耗模式）");
}

// -------------------------- 7. 未使用引脚配置（减少漏电流） --------------------------
void configUnusedPins() {
  // 根据开发板实际引脚调整
  pinMode(1, INPUT_PULLUP);
  pinMode(2, INPUT_PULLUP);
  pinMode(3, INPUT_PULLUP);
  // 补充其他未使用引脚...
}

// -------------------------- 8. 初始化与主循环 --------------------------
void setup() {
  // 1. 引脚初始化（DQ引脚输入，禁用内部上拉）
  pinMode(ONE_WIRE_BUS, INPUT);
  Serial.begin(115200);
  while (!Serial) delay(10);  // 等待串口连接
  Serial.println("=== M601+BLE低功耗信标 ===");

  // 2. 验证M601外部上拉
  int dq_idle_level = digitalRead(ONE_WIRE_BUS);
  Serial.printf("M601 DQ空闲电平：%d（1=正常）\n", dq_idle_level);
  if (dq_idle_level != 1) {
    Serial.println("错误：10kΩ上拉未生效！");
    while (1);
  }

  // 3. 未使用引脚高阻化
  configUnusedPins();

  // 4. BLE初始化
  if (!Bluefruit.begin()) {
    Serial.println("BLE初始化失败！");
    while (1);
  }
  Bluefruit.setName("M601-Beacon");
  Bluefruit.setTxPower(-20);  // 最低发射功率

  // 5. 设置自定义MAC
  if (!setCustomMacAddress(custom_mac_addr)) {
    ble_gap_addr_t default_addr;
    if (sd_ble_gap_addr_get(&default_addr) == NRF_SUCCESS) {
      Serial.print("默认MAC：");
      for (int i = 5; i >= 0; i--) {
        Serial.printf("%02X", default_addr.addr[i]);
        if (i > 0) Serial.print(":");
      }
      Serial.println();
    }
  }

  // 6. 信标初始化与启动
  beacon = BLEBeacon(beaconUuid, 0x0000, 0x0000, -54);
  startBeaconAdv();

  Serial.println("初始化完成，进入低功耗循环...");
  // Serial.end(); // 调试阶段保持串口开启
}

void loop() {
  // 1. 读取温度
  float temperature = readM601Temp();
  if (temperature != -999.0) {
    // 2. 温度变化超过阈值才更新广播
    if (fabs(temperature - last_temp) >= TEMP_THRESHOLD || last_temp == -999.0) {
      uint16_t temp_int = (uint16_t)temperature;                  // 整数部分
      uint16_t temp_dec = (uint16_t)round((temperature - temp_int) * 100); // 小数四舍五入

      // 更新信标参数并重启广播
      beacon.setMajorMinor(temp_int, temp_dec);
      Bluefruit.Advertising.stop();
      Bluefruit.Advertising.setBeacon(beacon);
      Bluefruit.Advertising.start(0);

      last_temp = temperature;
      Serial.printf("温度更新：%.2f ℃ (Major: %d, Minor: %d)\n", 
                   temperature, temp_int, temp_dec);
    }
  }

  // 3. 低功耗延迟（替代sleep方法）
  delay(1000);
}
