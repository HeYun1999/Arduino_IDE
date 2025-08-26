#include <bluefruit.h>
#include <OneWire.h>

// -------------------------- 1. 硬件引脚与核心实例定义 --------------------------
// M601单总线配置（DQ接D0，依赖外部10kΩ上拉电阻）
#define ONE_WIRE_BUS 0  
OneWire oneWire(ONE_WIRE_BUS);

// BLE信标核心实例
BLEBeacon beacon;                  // BLE信标实例
bool m601_init_flag = false;       // M601初始化完成标记
byte m601_rom_addr[8] = {0};       // M601唯一64位ROM地址

// -------------------------- 2. BLE信标与MAC地址参数 --------------------------
// 制造商ID（0x0059=Nordic，可根据需求修改）
#define MANUFACTURER_ID   0x0059

// 信标UUID（自定义，扫描时可识别）
uint8_t beaconUuid[16] = {
  0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78,
  0x89, 0x9a, 0xab, 0xbc, 0xcd, 0xde, 0xef, 0xf0
};

// 自定义MAC地址（6字节，小端序！实际显示为 FF:EE:DD:CC:BB:AA）
// 规则：最高2位必须为11（如0xAA、0xBB等，符合蓝牙静态随机地址规范）
uint8_t custom_mac_addr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

// -------------------------- 3. M601配置与温度读取函数 --------------------------
// M601参数配置（10kΩ上拉适配，1次/秒测量，高重复性）
void configM601(byte rom_addr[]) {
  // 写暂存器（配置测量参数）
  oneWire.reset();                
  oneWire.select(rom_addr);       
  oneWire.write(0x4E);            // 写暂存器指令
  oneWire.write(0x00);            // Tha_Set_lsb（默认）
  oneWire.write(0x00);            // Tla_Set_lsb（默认）
  oneWire.write(0x0A);            // 配置：1次/秒 + 高重复性
  delayMicroseconds(180);         // 10kΩ上拉写操作等待

  // 保存配置到E2PROM
  oneWire.reset();
  oneWire.select(rom_addr);
  oneWire.write(0x48);            // 复制暂存器到E2PROM
  delay(40);                      // E2PROM写周期（≤40ms，规格书要求）
  Serial.println("M601配置完成：1次/秒测量，高重复性");
}

// M601温度读取（含设备搜索、CRC校验、10kΩ时序适配）
float readM601Temp(void) {
  // 首次调用时搜索M601设备
  if (!m601_init_flag) {
    bool found = false;
    // 多轮搜索（最多5次，提高成功率）
    for (int retry = 0; retry < 5; retry++) {
      oneWire.reset();                
      delayMicroseconds(1500);       // 10kΩ适配：复位后等待1.5ms
      if (oneWire.search(m601_rom_addr)) { 
        found = true;
        break;
      }
      oneWire.reset_search();         
      delay(300);                     
    }

    // 设备未找到处理
    if (!found) {
      Serial.println("M601：5次搜索未找到（检查10kΩ上拉/接线/供电）");
      return -999.0;
    }

    // 打印ROM地址（用于调试）
    Serial.print("M601 ROM地址：");
    for (int i = 0; i < 8; i++) {
      Serial.print(m601_rom_addr[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    // ROM地址校验（符合M601规格书）
    if (m601_rom_addr[0] != 0x28) {
      Serial.println("M601：设备类型错误（首字节应为0x28）");
      return -999.0;
    }
    // 最后两字节为00时跳过CRC（规格书11.2备注）
    if (m601_rom_addr[6] == 0x00 && m601_rom_addr[7] == 0x00) {
      Serial.println("M601：ROM尾字节为00，跳过CRC校验");
    } else if (OneWire::crc8(m601_rom_addr, 7) != m601_rom_addr[7]) {
      Serial.println("M601：ROM CRC校验失败（地址读取错误）");
      return -999.0;
    }

    // 配置M601并标记初始化完成
    configM601(m601_rom_addr);
    m601_init_flag = true;
  }

  // 读取温度数据
  byte scratchpad[9];  // 暂存器缓存（8字节数据+1字节CRC）
  int16_t temp_raw;    // 原始温度值（16位）

  // 1. 启动温度转换
  oneWire.reset();
  oneWire.select(m601_rom_addr);
  oneWire.write(0x44, 1);  // 启动测温（带寄生电源模式）

  // 2. 等待测温完成（10kΩ适配超时25ms）
  unsigned long start_time = millis();
  while (true) {
    if (oneWire.read_bit() == 1) break;  // 测温完成（DQ引脚变高）
    if (millis() - start_time > 25) {
      Serial.println("M601：测温超时（检查总线通信）");
      return -999.0;
    }
    delayMicroseconds(120);  // 10kΩ适配：读取间隔
  }

  // 3. 读取暂存器数据
  oneWire.reset();
  oneWire.select(m601_rom_addr);
  oneWire.write(0xBE);        // 读暂存器指令
  delayMicroseconds(200);     // 10kΩ适配：数据准备等待
  for (int i = 0; i < 9; i++) {
    scratchpad[i] = oneWire.read();
  }

  // 4. 暂存器CRC校验
  if (OneWire::crc8(scratchpad, 8) != scratchpad[8]) {
    Serial.println("M601：温度数据CRC校验失败");
    return -999.0;
  }

  // 5. 温度数据转换（规格书9.1公式）
  temp_raw = (scratchpad[1] << 8) | scratchpad[0];
  float temperature;
  if (temp_raw & 0x8000) {  // 负温度处理
    temp_raw = ~temp_raw + 1;
    temperature = 40 - (temp_raw * 0.00390625);
  } else {  // 正温度处理
    temperature = 40 + temp_raw * 0.00390625;
  }

  return temperature;
}

// -------------------------- 4. BLE MAC地址设置函数 --------------------------
// 设置自定义MAC地址（需在Bluefruit.begin()后调用）
bool setCustomMacAddress(uint8_t* custom_addr) {
  ble_gap_addr_t gap_addr;
  gap_addr.addr_type = 1;  // 地址类型：1=静态随机地址（符合蓝牙规范）
  memcpy(gap_addr.addr, custom_addr, 6);  // 复制自定义地址到结构体

  // 调用单参数setAddr函数（适配Seeeduino nRF52 1.1.10库）
  if (Bluefruit.setAddr(&gap_addr)) {
    Serial.print("自定义MAC地址（大端序）：");
    for (int i = 5; i >= 0; i--) {  // 小端转大端显示（常规MAC格式）
      Serial.printf("%02X", gap_addr.addr[i]);
      if (i > 0) Serial.print(":");
    }
    Serial.println();
    return true;
  } else {
    Serial.println("自定义MAC地址设置失败！");
    return false;
  }
}

// 打印默认出厂MAC地址（用于对比调试）
void printDefaultMacAddress() {
  ble_gap_addr_t default_addr;
  if (sd_ble_gap_addr_get(&default_addr) == NRF_SUCCESS) {
    Serial.print("默认出厂MAC地址：");
    for (int i = 5; i >= 0; i--) {
      Serial.printf("%02X", default_addr.addr[i]);
      if (i > 0) Serial.print(":");
    }
    Serial.println();
  } else {
    Serial.println("无法获取默认MAC地址");
  }
}

// -------------------------- 5. BLE信标广播配置 --------------------------
void startBeaconAdv() {
  // 配置信标参数（UUID、Major、Minor、RSSI）
  beacon.setManufacturer(MANUFACTURER_ID);
  
  // 配置广播参数
  Bluefruit.Advertising.setBeacon(beacon);  // 广播包加载信标数据
  Bluefruit.ScanResponse.addName();         // 扫描响应包添加设备名称
  Bluefruit.Advertising.restartOnDisconnect(true);  // 断开后重新广播
  Bluefruit.Advertising.setInterval(160, 160);      // 广播间隔：100ms（160*0.625ms）
  Bluefruit.Advertising.setFastTimeout(30);         // 快速广播超时：30秒
  Bluefruit.Advertising.start(0);                   // 无限广播（0=不超时）

  Serial.println("BLE信标已开始广播（用nRF Connect扫描）");
}

// -------------------------- 6. 初始化与主循环 --------------------------
void setup() {
  // 1. 初始化单总线引脚（禁用内部上拉，依赖外部10kΩ）
  pinMode(ONE_WIRE_BUS, INPUT);
  Serial.begin(115200);
  while (!Serial) delay(10);  // 等待串口连接
  Serial.println("=== M601+BLE信标（自定义MAC） ===");

  // 2. 验证M601外部10kΩ上拉（空闲电平应为高）
  int dq_idle_level = digitalRead(ONE_WIRE_BUS);
  Serial.printf("M601 DQ引脚空闲电平：%d（1=正常，0=上拉失效）\n", dq_idle_level);
  if (dq_idle_level != 1) {
    Serial.println("错误：外部10kΩ上拉未生效（检查DQ-VDD接线）");
    while (1);  // 上拉错误时卡死，提示用户检查
  }

  // 3. 初始化BLE模块
  if (!Bluefruit.begin()) {
    Serial.println("BLE模块初始化失败！");
    while (1);
  }
  Bluefruit.setName("M601-Beacon");  // 设备名称（扫描时可见）
  Bluefruit.setTxPower(0);           // 发射功率：0dBm（平衡功耗与距离）

  // 4. 设置自定义MAC地址
  if (!setCustomMacAddress(custom_mac_addr)) {
    printDefaultMacAddress();  // 失败时打印默认地址
  }

  // 5. 初始化信标参数（不使用begin()方法，直接通过构造函数初始化）
  beacon = BLEBeacon(beaconUuid, 0x0000, 0x0000, -54);
  startBeaconAdv();

  Serial.println("初始化完成，等待M601温度采集...");
}

void loop() {
  // 1. 读取M601温度
  float temperature = readM601Temp();
  if (temperature != -999.0) {  // 温度读取成功
    Serial.printf("M601温度：%.2f ℃\n", temperature);

    // 2. 将温度嵌入BLE信标（Major=整数部分，Minor=小数部分*100）
    uint16_t temp_int = (uint16_t)temperature;                  // 整数部分（如25.67→25）
    uint16_t temp_dec = (uint16_t)((temperature - temp_int)*100); // 小数部分（如25.67→67）
    beacon.setMajorMinor(temp_int, temp_dec);  // 更新信标Major/Minor

    // 3. 重启广播（更新Major/Minor后需重启广播生效）
    Bluefruit.Advertising.stop();
    Bluefruit.Advertising.setBeacon(beacon);
    Bluefruit.Advertising.start(0);
  }

  delay(1000);  // 与M601测量周期同步（1次/秒）
}