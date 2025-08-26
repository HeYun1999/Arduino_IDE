/*********************************************************************
 BLE信标示例（最终修复版）
 功能：适配仅支持单参数setAddr的库版本，实现自定义MAC地址
 适配：Seeeduino nRF52 1.1.10库及配套Bluefruit52Lib
*********************************************************************/
#include <bluefruit.h>

// 1. 制造商ID（0x0059=Nordic）
#define MANUFACTURER_ID   0x0059

// 2. 信标核心参数
uint8_t beaconUuid[16] = {
  0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78,
  0x89, 0x9a, 0xab, 0xbc, 0xcd, 0xde, 0xef, 0xf0
};
BLEBeacon beacon(beaconUuid, 0x0102, 0x0304, -54);

// 3. 自定义MAC地址（6字节，小端序，实际显示为FF:EE:DD:CC:BB:AA）
// 注意：最高2位必须为11（如0xAA、0xBB等，符合蓝牙规范）
uint8_t custom_mac_addr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

void setup() 
{
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("=== BLE信标（最终修复版） ===");
  Serial.println("-----------------------------------");

  // 初始化BLE模块
  if (!Bluefruit.begin()) {
    Serial.println("BLE模块初始化失败！");
    while (1);
  }
  // 在Bluefruit.begin()之后添加
  Bluefruit.setName("MyCustomBeacon"); // 设置为你需要的名称（如"MyBeacon-001"）
  // 4. 核心修复：构造ble_gap_addr_t结构体设置MAC地址
  ble_gap_addr_t gap_addr;
  // 地址类型：1 = 静态随机地址（BLE_GAP_ADDR_TYPE_RANDOM_STATIC的实际值）
  gap_addr.addr_type = 1;
  // 复制自定义MAC地址到结构体（小端序）
  memcpy(gap_addr.addr, custom_mac_addr, 6);

  // 调用单参数版本的setAddr函数
  if (Bluefruit.setAddr(&gap_addr)) {
    Serial.print("自定义MAC地址设置成功！地址（大端序）：");
    for (int i = 5; i >= 0; i--) {
      Serial.printf("%02X", gap_addr.addr[i]);
      if (i > 0) Serial.print(":");
    }
    Serial.println();
  } else {
    Serial.println("自定义MAC地址设置失败！使用默认地址");
    printDefaultMacAddress();
  }

  // 5. 信标配置
  Bluefruit.autoConnLed(false); // 关闭指示灯省电
  Bluefruit.setTxPower(0);      // 最低发射功率
  beacon.setManufacturer(MANUFACTURER_ID);
  startAdv();

  Serial.println("\n信标已开始广播，使用nRF Connect扫描验证");
  suspendLoop();
}

// 6. 广播配置函数
void startAdv(void)
{  
  Bluefruit.Advertising.setBeacon(beacon);
  Bluefruit.ScanResponse.addName(); // 扫描响应包添加设备名称
  
  // 标准信标广播参数
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(160, 160); // 100ms间隔
  Bluefruit.Advertising.setFastTimeout(30);     // 30秒快速广播
  Bluefruit.Advertising.start(0);               // 无限广播
}

// 7. 打印默认MAC地址（供对比）
void printDefaultMacAddress()
{
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

void loop() 
{
  // 暂停循环以节省功耗
}
