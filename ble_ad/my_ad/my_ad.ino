/*********************************************************************
 BLE自定义信息广播示例（修复版）
 功能：通过广播包发送自定义信息（如温度数据），适配nRF52840
 基于：Seeeduino nRF52 1.1.10库及配套Bluefruit52Lib
*********************************************************************/
#include <bluefruit.h>

// 1. 配置参数
#define MANUFACTURER_ID   0x0059  // 制造商ID (Nordic)
#define BROADCAST_INTERVAL 160    // 广播间隔 (160*0.625ms = 100ms)
#define TX_POWER          0       // 发射功率 (0dBm)

// 2. 自定义MAC地址（6字节，小端序，实际显示为FF:EE:DD:CC:BB:AA）
// 注意：最高2位必须为11（符合蓝牙规范）
uint8_t custom_mac_addr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

// 3. 自定义数据结构 - 根据需要修改
struct CustomData {
  float temperature;  // 温度数据
  uint8_t battery;    // 电池电量 (0-100%)
  uint16_t counter;   // 数据包计数器
} customData;

// 4. 自定义数据缓冲区（不包含类型和长度字段）
uint8_t custom_payload[27] = {0};  // 预留足够空间

void setup() 
{
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("=== BLE自定义信息广播器 ===");
  Serial.println("-----------------------------------");

  // 初始化自定义数据
  customData.temperature = 25.6f;
  customData.battery = 87;
  customData.counter = 0;

  // 初始化BLE模块
  if (!Bluefruit.begin()) {
    Serial.println("BLE模块初始化失败！");
    while (1);
  }

  // 设置设备名称
  Bluefruit.setName("CustomDataBeacon");
  
  // 设置自定义MAC地址
  ble_gap_addr_t gap_addr;
  gap_addr.addr_type = 1;  // 静态随机地址
  memcpy(gap_addr.addr, custom_mac_addr, 6);

  if (Bluefruit.setAddr(&gap_addr)) {
    Serial.print("自定义MAC地址设置成功：");
    for (int i = 5; i >= 0; i--) {
      Serial.printf("%02X", gap_addr.addr[i]);
      if (i > 0) Serial.print(":");
    }
    Serial.println();
  } else {
    Serial.println("自定义MAC地址设置失败，使用默认地址");
  }

  // 配置BLE参数
  Bluefruit.autoConnLed(false);  // 关闭指示灯省电
  Bluefruit.setTxPower(TX_POWER);
  
  // 开始广播
  startAdvertising();

  Serial.println("已开始广播自定义数据，使用nRF Connect扫描验证");
}

void loop() 
{
  // 定期更新数据（每2秒）
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 2000) {
    lastUpdate = millis();
    
    // 更新自定义数据（示例：模拟温度变化和计数器递增）
    customData.temperature += 0.1f;
    if (customData.temperature > 30.0f) customData.temperature = 20.0f;
    
    customData.counter++;
    if (customData.counter >= 1000) customData.counter = 0;

    // 更新广播数据
    updateAdvertisingData();
    
    // 重启广播以应用新数据
    Bluefruit.Advertising.stop();
    Bluefruit.Advertising.start(0);
    
    // 打印调试信息
    Serial.printf("更新数据 - 温度: %.1f°C, 电量: %d%%, 计数: %d\n",
                 customData.temperature,
                 customData.battery,
                 customData.counter);
  }
  
  delay(100);
}

// 构建并设置广播数据
void updateAdvertisingData() {
  // 清空之前的广播数据
  Bluefruit.Advertising.clearData();
  
  // 构建厂商自定义数据负载
  uint8_t payload_len = 0;
  
  // 厂商ID (2字节，小端序)
  custom_payload[payload_len++] = (MANUFACTURER_ID >> 0) & 0xFF;  // 低8位
  custom_payload[payload_len++] = (MANUFACTURER_ID >> 8) & 0xFF;  // 高8位
  
  // 温度数据 (4字节，float类型)
  memcpy(&custom_payload[payload_len], &customData.temperature, 4);
  payload_len += 4;
  
  // 电池电量 (1字节)
  custom_payload[payload_len++] = customData.battery;
  
  // 计数器 (2字节)
  custom_payload[payload_len++] = (customData.counter >> 0) & 0xFF;  // 低8位
  custom_payload[payload_len++] = (customData.counter >> 8) & 0xFF; // 高8位
  
  // 添加厂商自定义数据到广播包
  // 第一个参数：0xFF 表示厂商自定义数据类型
  // 第二个参数：数据指针
  // 第三个参数：数据长度
  Bluefruit.Advertising.addData(0xFF, custom_payload, payload_len);
  
  // 可选：添加设备名称到广播包（会占用广播包空间）
  // Bluefruit.Advertising.addName();
}

// 配置并启动广播
void startAdvertising()
{
  // 初始化广播数据
  updateAdvertisingData();
  
  // 配置广播参数
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(BROADCAST_INTERVAL, BROADCAST_INTERVAL);
  Bluefruit.Advertising.setFastTimeout(0);  // 禁用快速广播超时
  
  // 启动无限广播
  Bluefruit.Advertising.start(0);
}
    