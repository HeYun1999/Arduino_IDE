/*********************************************************************
 BLE外设端（最终修复版）
 适配Seeeduino nRF52 1.1.10库，解决回调参数和连接状态判断报错
*********************************************************************/
#include <bluefruit.h>

// 1. UART服务对象
BLEUart bleuart;

// 2. 全局变量：记录连接状态
bool is_connected = false;

// 3. 接收中心设备数据的回调函数（关键修复：参数为连接句柄）
void recv_from_central(uint16_t conn_handle) {
  (void)conn_handle; // 消除未使用参数警告
  
  // 从UART读取数据（循环读取所有可用字节）
  while (bleuart.available()) {
    char data = bleuart.read();
    Serial.printf("收到中心设备数据：%c\n", data);
  }
}

// 4. 连接成功回调
void connect_callback(uint16_t conn_handle) {
  (void)conn_handle;
  is_connected = true; // 更新连接状态
  Serial.println("中心设备已连接！");
}

// 5. 断开连接回调
void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void)conn_handle;
  (void)reason;
  is_connected = false; // 更新连接状态
  Serial.println("中心设备已断开，重新开始广播...");
  Bluefruit.Advertising.start(0); // 重新广播
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10); // 等待串口初始化

  Serial.println("BLE外设初始化中...");
  
  // 6. 初始化Bluefruit为外设模式
  Bluefruit.begin();
  Bluefruit.setTxPower(4); // 设置发射功率
  Bluefruit.setName("BLE-Peripheral"); // 设备名称（与中心设备匹配）

  // 7. 初始化UART服务
  bleuart.begin();
  // 关键修复1：设置接收回调（参数为连接句柄，符合库要求）
  bleuart.setRxCallback(recv_from_central);

  // 8. 设置连接/断开回调
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  // 9. 配置广播内容（包含UART服务，让中心设备可发现）
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart); // 广播UART服务UUID

  // 10. 启动广播（0=无限期广播）
  Bluefruit.Advertising.start(0);
  Serial.println("外设已启动，等待中心设备连接...");
}

void loop() {
  // 11. 已连接时：从串口接收数据并发送给中心设备
  if (is_connected) { // 关键修复2：用全局变量判断连接状态
    if (Serial.available()) {
      char send_data = Serial.read(); // 读取串口字符
      // 通过UART发送给中心设备
      if (bleuart.write(&send_data, 1) > 0) {
        Serial.printf("发送给中心设备：%c\n", send_data);
      } else {
        Serial.println("发送失败（连接可能已断开）");
      }
    }
  }

  delay(10); // 降低CPU占用
}
