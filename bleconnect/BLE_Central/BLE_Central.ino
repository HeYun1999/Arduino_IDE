/*********************************************************************
 BLE双向通信 - 服务器（终极适配版）
 核心：使用 BLEPeripheral 框架，完全规避 addCharacteristic/addService 问题
 适配：Seeeduino nRF52 1.1.10 + Bluefruit52Lib
*********************************************************************/
#include <bluefruit.h>

// -------------------------- 1. 手动定义蓝牙规范常量（库未定义） --------------------------
#define BLE_GATT_CHAR_PROP_WRITE          0x08
#define BLE_GATT_CHAR_PROP_WRITE_WO_RESP  0x04
#define BLE_GATT_CHAR_PROP_READ           0x02
#define BLE_GATT_CHAR_PROP_NOTIFY         0x10

// -------------------------- 2. 基础配置 --------------------------
#define SERVER_NAME         "BLE-Server"
uint8_t server_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01}; // 自定义MAC

// -------------------------- 3. BLE核心对象（用 BLEPeripheral 框架） --------------------------
// 3.1 定义服务和特征值的UUID（128位UUID，避免16位UUID的兼容性问题）
const uint8_t COMM_SERVICE_UUID[16] = {
  0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
  0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x01
};
const uint8_t RX_CHAR_UUID[16] = {
  0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
  0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x02
};
const uint8_t TX_CHAR_UUID[16] = {
  0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
  0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x03
};

// 3.2 创建特征值（直接关联到服务，无需手动add）
BLECharacteristic rxChar(RX_CHAR_UUID, BLE_GATT_CHAR_PROP_WRITE | BLE_GATT_CHAR_PROP_WRITE_WO_RESP, 20);
BLECharacteristic txChar(TX_CHAR_UUID, BLE_GATT_CHAR_PROP_READ | BLE_GATT_CHAR_PROP_NOTIFY, 20);

// 3.3 创建外围设备（核心：直接将特征值传入，自动关联服务）
BLEPeripheral blePeripheral;
BLEDfu bledfu; // 可选DFU服务

// -------------------------- 4. 状态变量 --------------------------
bool is_connected = false;
uint16_t conn_handle = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("=== BLE服务器（终极版） ===");

  // 4.1 初始化BLE（必须先初始化Peripheral）
  blePeripheral.begin();
  bledfu.begin(); // 使能DFU（可选）

  // 4.2 设置设备信息
  blePeripheral.setDeviceName(SERVER_NAME);
  set_custom_mac(server_mac); // 设置自定义MAC

  // 4.3 配置服务和特征值（核心：用Peripheral的addAttribute方法，替代addCharacteristic）
  blePeripheral.addAttribute(COMM_SERVICE_UUID); // 添加服务UUID
  blePeripheral.addAttribute(rxChar);            // 服务自动关联接收特征值
  blePeripheral.addAttribute(txChar);            // 服务自动关联发送特征值

  // 4.4 绑定接收回调（客户端写数据时触发）
  rxChar.setWriteCallback(rx_callback);

  // 4.5 配置广播（直接用Peripheral的广播方法）
  blePeripheral.setAdvertisedServiceUuid(COMM_SERVICE_UUID); // 广播服务UUID
  blePeripheral.advertise(); // 开始广播（无限循环）

  Serial.println("服务器已启动，等待客户端连接...");
}

// -------------------------- 5. 自定义MAC设置（复用原逻辑） --------------------------
void set_custom_mac(uint8_t* addr) {
  ble_gap_addr_t gap_addr;
  gap_addr.addr_type = 1; // 静态随机地址
  memcpy(gap_addr.addr, addr, 6);

  if (blePeripheral.setAddr(&gap_addr)) { // 用 blePeripheral 调用 setAddr
    Serial.print("服务器MAC: ");
    print_mac(gap_addr.addr);
  } else {
    Serial.print("MAC设置失败，默认MAC: ");
    print_default_mac();
  }
}

// -------------------------- 6. 接收回调（客户端发数据触发） --------------------------
void rx_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
  conn_handle = conn_hdl;
  is_connected = true;

  // 打印客户端数据
  Serial.print("收到客户端: ");
  for (int i=0; i<len; i++) Serial.print((char)data[i]);
  Serial.println();

  // 回复客户端（用notify，传连接句柄+数据+长度）
  char response[20];
  sprintf(response, "已收: %.*s", len, data);
  txChar.notify(conn_handle, response, strlen(response));
  Serial.print("回复客户端: "); Serial.println(response);
}

// -------------------------- 7. 辅助函数：打印MAC --------------------------
void print_mac(uint8_t* addr) {
  for (int i=5; i>=0; i--) { // 小端转大端（日常格式）
    Serial.printf("%02X", addr[i]);
    if (i>0) Serial.print(":");
  }
  Serial.println();
}

void print_default_mac() {
  ble_gap_addr_t default_addr;
  if (sd_ble_gap_addr_get(&default_addr) == NRF_SUCCESS) {
    print_mac(default_addr.addr);
  } else {
    Serial.println("无法获取默认MAC");
  }
}

void loop() {
  // 核心：处理BLE事件（替代 waitForEvent，库原生支持）
  blePeripheral.poll();
  // 低功耗延迟
  delay(is_connected ? 100 : 500);
}