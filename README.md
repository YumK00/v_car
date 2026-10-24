# 车载 T-Box 仿真与手机中控（v_car）

> 一句话定位：用 Qt/C++ 在 PC 上复刻一台车联网终端（T-Box）——在虚拟 CAN 总线上跑 4 个 ECU 仿真节点，主程序解析 CAN 帧、按信号变化上云 MQTT，并给出一屏深色中控界面。
> 它是同一作品集里的**桌面端项目**：三个 MCU 仓库（`collecton_node` / `control_node` / `gateway_node`）构成一条真实 CAN 总线上的工业链路，本仓库则用 Qt 的**虚拟 CAN 总线**独立复现车载域的「ECU → CAN → T-Box 报文解析 → MQTT 上云 → 中控界面」全套流程，用于无硬件时演示报文解析、MQTT 交互与界面。

## 一、项目简介

`v_car` 是一个纯 Qt/C++ 的桌面程序，用 Qt SerialBus 的 `virtualcan` 插件搭起一条虚拟 CAN 总线作为「仿真车」的通信底座。四个 ECU 仿真对象（BMS/VCU/BCM/DTC）各用一个 `QTimer` 周期性地把信号打包成 8 字节 CAN 帧写进总线，主窗口作为 T-Box 从总线收帧、按帧 ID 分发、按字节偏移解析信号，再把变化量转成 JSON 通过 MQTT 发到远端 Broker；同时订阅手机端下发的 `phone/cmd` 指令，把车门、空调状态变化写进界面日志。界面是一屏深色中控台，含车辆状态卡、总线负载表、CAN 报文日志与 MQTT 收发日志两个滚动窗口。

- 虚拟 CAN 总线一键启动/停止，`Loopback + ReceiveOwn` 自环，模拟「ECU 发帧 → 总线 → T-Box 收帧」完整回路
- 4 路 ECU 周期仿真：BMS 1300 ms、VCU 300 ms、BCM 2400 ms、DTC 180 ms
- 字节级手写解析 CAN 信号（大端拼接、位图表达车门），不依赖 DBC 或第三方协议库
- 变化触发上云：仅当信号值真正变化才发布，稳态零流量
- MQTT 上行 4 主题（bms/vcu/bcm/dtc）+ 下行 1 主题（`phone/cmd`），JSON 紧凑格式并注入毫秒时间戳
- 中控界面：状态卡 + 进度条、故障码文字与优先级配色、发布/订阅计数、心跳延迟；支持车辆状态快照导出

## 二、整体架构

```text
┌──────── ECU 仿真层（QTimer 周期驱动 simTick()）────────┐
│ BmsECU 0x120   VcuECU 0x100   BcmECU 0x200   DtcECU 0x110 │
└──────────────────────────┬─────────────────────────────┘
                           │ writeFrame(QCanBusFrame)
                ┌──────────▼───────────┐
                │   虚拟 CAN 总线      │ Qt virtualcan 插件
                │   can0 / 5 kbit·s⁻¹  │ Loopback + ReceiveOwn
                └──────────┬───────────┘
                           │ framesReceived → readFrame()
                ┌──────────▼───────────┐
                │  T-Box 报文层        │ MainWindow::parseCanFrame()
                │  帧 ID 分发/字节解析 │ 变化检测 + 去重上云
                └────┬────────────┬────┘
      update*Ui()    │            │  send*Info()（JSON）
            ┌────────▼──────┐  ┌──▼───────────────────────┐
            │  中控界面 GUI │  │ MqttClient（QMQTT 库）    │
            │  .ui + QSS    │  │ publish car/ecu/* QoS 0  │
            └───────────────┘  │ subscribe phone/cmd QoS 1│
                               └──┬───────────────────────┘
                                  │ MQTT/TCP :1883
                        ┌─────────▼─────────┐   ┌──────────────────┐
                        │ 远端 Broker       │◄─►│ 手机/中控 MQTT 端 │
                        └───────────────────┘   │（外部客户端模拟）│
                                                └──────────────────┘
```

| 类 / 文件 | 角色 | 关键职责 |
| --- | --- | --- |
| `BaseECU`（`baseecu.h/.cpp`） | 仿真基类 | 持有 `QCanBusDevice*` 与 `QTimer* m_simTimer`，构造时把 `timeout` 连到纯虚槽 `simTick()`；`startSim(ms)` / `stopSim()` 控周期 |
| `BmsECU`/`VcuECU`/`BcmECU`/`DtcECU` | 四个 ECU 仿真体 | 重写 `simTick()`，用 `QRandomGenerator` 造数、按各自帧 ID 组 8 字节 payload 并 `writeFrame` |
| `MainWindow` | T-Box + 中控界面 | 创建/销毁 CAN 设备与 4 个 ECU、收帧解析、刷新 UI、调用 MQTT、处理手机指令、日志与快照 |
| `MqttClient` | MQTT 适配层 | 封装 `QMQTT::Client`，4 个 `send*Info()` 上行 + `sigRecvPhoneCmd` 下行信号 |
| `vehicledata.h` | 数据模型 | 4 个 POD 结构体 + 各自 `toJson()`/`fromJson()`，CAN 信号与 JSON 的唯一交汇点 |

## 三、目录结构

```text
v_car/
├── CMakeLists.txt          # 构建脚本：Qt6/Qt5 兼容写法、C++17、AUTOUIC/AUTOMOC/AUTORCC、链接 qmqtt
├── CMakeLists.txt.user     # Qt Creator 本机配置（含 C:\Qt\... 绝对路径，不应提交）
├── main.cpp                # 入口：从 qrc 读 QSS 并 a.setStyleSheet()，再显示 MainWindow
├── mainwindow.h/.cpp       # 主窗口：CAN 启停、帧解析、UI 刷新、MQTT 收发、日志、快照导出
├── mainwindow.ui           # Designer 界面（约 1400 行，深色中控多栏布局）
├── mqttclient.h/.cpp       # MQTT 封装：QMQTT 客户端、4 路发布、1 路订阅
├── baseecu.h/.cpp          # ECU 仿真基类（QTimer + 纯虚 simTick）
├── bmsecu.h/.cpp           # 电池 ECU，发 0x120
├── vcuecu.h/.cpp           # 整车 ECU，发 0x100
├── bcmecu.h/.cpp           # 车身 ECU，发 0x200
├── dtcecu.h/.cpp           # 故障 ECU，发 0x110
├── vehicledata.h           # 信号结构体 + JSON 序列化
├── dark_gateway.qss        # 深色主题样式表（270 行，按 objectName 选择器配色）
└── res.qrc                 # 资源清单：/stylesheet/dark_gateway.qss、/icon/image/logo.svg
```

`qmqtt/`（第三方 MQTT 库源码）与 `image/logo.svg`（`res.qrc` 声明的图标）**不在仓库内**，构建前需补齐，详见第七、九节。

## 四、核心模块说明

| 文件 | 职责 | 关键实现点（含仿真的信号） |
| --- | --- | --- |
| `main.cpp` | 程序入口 | `loadStyleSheet(":/stylesheet/dark_gateway.qss")` 读资源，成功则全局 `setStyleSheet()`，失败仅 `qDebug` |
| `baseecu.cpp` | 仿真基类实现 | 构造里 `new QTimer(this)` 并 `connect(timer, timeout, this, simTick)`；基类不含信号，只提供节拍 |
| `bmsecu.cpp` | SOC 仿真 → `0x120` | `static soc = 80`，每次加 `QRandomGenerator::bounded(-1, 1)`（只会是 -1 或 0）并夹在 0~100；`[0..1]` 大端放 SOC |
| `vcuecu.cpp` | 车速/电机温度 → `0x100` | `static speed = 80` 每次 ±5 随机游走并夹 0~180；`vcutemp = 50 + speed/2` 为**局部变量**，故温度随车速瞬时变化；`[0..1]` 车速、`[2..3]` 温度 |
| `bcmecu.cpp` | 车门/空调 → `0x200` | `static door=0`、`air_con=0`、`air_temperature=24`，三个变量在 tick 内从不被改，故以常量帧持续上报；`[0]` 车门位图、`[1]` 空调位图、`[2]` 温度 |
| `dtcecu.cpp` | 故障码 → `0x110` | `static guzhangma = 0`、`youxianji = 0`，恒发「无故障」；`[0]` 故障码、`[1]` 优先级 |
| `mainwindow.cpp` | T-Box 主逻辑 | CAN 启停、`parseCanFrame` 分发、4 个 `update*Ui`、`writeLog` 双日志、`onPhoneCommandArrived` 指令处理、快照导出 |
| `mqttclient.cpp` | MQTT 适配 | `QMQTT::Client` 设 host/port/clientId，4 个 `send*Info` 组 JSON 后 `publish`，`onReceived` 过滤主题后 `emit sigRecvPhoneCmd` |
| `dark_gateway.qss` | 主题 | 背景 `#0F1726`、卡片 `#172238`，用 `QPushButton#btn_qidongcan`、`QLabel#label_guzhanghuodong` 等 objectName 精确着色 |

`vehicledata.h` 的数据模型（4 个结构体均带 `toJson()` / `fromJson()`）：

| 结构体 | 字段 | 注释含义 |
| --- | --- | --- |
| `s_bmsdata` | `batterySoc` | 0~100 电量百分比 |
| `s_vcudata` | `vcuSpeed`、`vcuTemp` | 车速 0~180；温度 -40~150 ℃（注：`fromJson` 只还原 `vcuSpeed`，漏了温度） |
| `s_bcmdata` | `door_status`（bit0-3 左前/右前/左后/右后，1=关门）、`air_con_status`（bit0 开关、bit1 制冷、bit2 制热）、`air_temperature` | 车门位图 + 空调模式位图 + 设定温度 |
| `s_dtcdata` | `guzhangma`、`youxianji` | 故障码（0=无故障）；优先级 0 无 / 1 警告(橙) / 2 严重(红) |

## 五、CAN 与 MQTT 报文设计

CAN 帧（标准帧，DLC 固定 8 字节，多字节信号均为**大端**，未用字节补 0）：

| 帧 ID | 名称 | 周期 | 载荷布局 | 说明 |
| --- | --- | --- | --- | --- |
| `0x100` | VCU 整车运行 | 300 ms | `[0..1]` 车速、`[2..3]` 电机温度（uint16 BE） | 车速与温度任一变化才上云 |
| `0x110` | DTC 故障 | 180 ms | `[0]` 故障码、`[1]` 优先级（uint8） | 变化时刷新故障文字与颜色 |
| `0x120` | BMS 电池 | 1300 ms | `[0..1]` SOC（uint16 BE） | 驱动 SOC 数值与进度条 |
| `0x200` | BCM 车身控制 | 2400 ms | `[0]` 车门位图、`[1]` 空调位图、`[2]` 空调温度 | 上云；回写通路已注释，见第九节 |

总线参数：接口 `virtualcan`、设备名 `can0`，`LoopbackKey=true`、`ReceiveOwnKey=true`（自发自收），`BitRateKey = 5000`（5 kbit/s，与界面「5kbit/s」标签一致）。

MQTT 连接参数（`mqttclient.cpp` 硬编码）：

| 项 | 值 |
| --- | --- |
| Broker / 端口 | `8.138.233.135` / `1883`（TCP 明文，无 TLS） |
| Client ID | `vehicle`（固定） |
| 用户名 / 密码 | 空 |
| 上行 QoS | 0（`QMQTT::Message(0, topic, payload, 0)`） |
| 下行订阅 | `phone/cmd`，QoS 1（`onConnected` 中订阅） |

| 方向 | 主题 | payload 示例 |
| --- | --- | --- |
| 上行 | `car/ecu/bms` | `{"batterySoc":78,"timestamp":1710000000000}` |
| 上行 | `car/ecu/vcu` | `{"vcuSpeed":83,"vcuTemp":91,"timestamp":…}` |
| 上行 | `car/ecu/bcm` | `{"door_status":0,"air_con_status":0,"air_temperature":24,"timestamp":…}` |
| 上行 | `car/ecu/dtc` | `{"guzhangma":0,"youxianji":0,"timestamp":…}` |
| 下行 | `phone/cmd` | `{"door_status":15,"air_con_status":3,"air_temperature":24,"timestamp":…}` |

下行指令进入 `onPhoneCommandArrived()` 后：与本地 `m_bcmcurinfo` 逐项比较，空调位图按 bit 解析成「已关闭/制冷/制热/未知模式」中文描述，车门按 `oldDoor ^ newDoor` 逐位算「开门/关门」并 `join(" | ")`，全部写入 MQTT 日志窗口并累加订阅计数；`timestamp` 用于计算心跳延迟（`当前时间 - 指令时间`，写入界面 ms 标签）。

## 六、界面与交互

| 区域 | 关键控件 | 内容 / 行为 |
| --- | --- | --- |
| 顶栏 | `label_62`、`timeedit1`、`timeedit2`、`pushButton` | 标题 `VEHICLE TELEMATICS CONSOLE`、当前时间（1 s 刷新）、CAN 启动时刻、按钮「保存车辆状态」 |
| 左栏 `boxleft`「CAN控制」 | `label_yunxingzhuangtai`、`label_zongxianzhuangtai`、`btn_qidongcan`、`btn_tingzhican`、`groupBox_6`「运行模式」（`radionbtn_fangzhengmoshi` 仿真模式 / `radiobtn_zhenshicheliang` 真实车辆）、`progressbar_zhenlv`、`label_zhenlv` | 启停总线；运行状态文字（运行中绿 / 已停止红）；总线负载进度条上限 5000，配 `label_zhenlv` 显示每秒累计 bit 数 |
| 中栏 `boxcenter`「车辆状态显示」 | `label_chesu`、`label_dianchi`+`progressBar_3`、`label_dianjiwendu`+`progressBar_2`、`label_guzhanghuodong`、`label_4` | 车速、SOC（进度条 0~100）、电机温度（进度条 -40~150）、故障描述、系统自检结论 |
| 右栏 `groupBox_3`「MQTT控制」 | `label_mqttlianjiezhuangtai`、`label_mqttzaixian`、`label_mqttfabuxiaoxi`、`label_mqttdingyuezhuti`、`label_mqttxintiaoyanchi`、`btn_mqttlianjie` | 连接状态、会话状态、上行发布计数、下行订阅计数、心跳延迟（ms）、「连接Broker」按钮 |
| 底部 `groupBox_4` / `groupBox_5` | `listWidgetcan`、`listWidgetmqtt` | CAN 报文日志、MQTT 收发日志，带 `[hh:mm:ss]` 时间戳与 RX/TX 标记，写入后自动 `scrollToBottom()` |

主题加载链路：`dark_gateway.qss` → `res.qrc` 的 `/stylesheet` 前缀 → `main.cpp` 用 `QFile` 读 `:/stylesheet/dark_gateway.qss` → `QApplication::setStyleSheet()` 全局生效。QSS 用 `QGroupBox`、`QPushButton#btn_qidongcan`（绿色主操作）、`QPushButton#btn_mqttlianjie`（靛蓝）、`QLabel#label_guzhanghuodong`（默认绿、警告橙 `#FF8C00`、严重红）等选择器区分状态色。

刷新机制：1 s 的 `QTimer` 驱动 `on_timeout()` 更新时钟与总线负载；`tim2` 每 5 s 触发 `on_tim2out()` 往 CAN 日志追加一行 `[x km/h | SOC y% | MCU z℃]` 摘要；`updateBcmUi()` 直接改写 `listWidgetcan` 的最后一行（BCM 没有独立状态控件）。

1. 点「启动CAN」——创建 `virtualcan` 设备、连接 4 个 ECU 并按各自周期启动仿真，状态标签转绿；
2. 点「连接Broker」——构造 `MqttClient` 并 `connectToHost()`，成功后按钮置灰（当前实现不支持二次连接）；
3. 观察两个日志窗口与状态卡，等待信号变化触发上云；
4. 用 MQTTX 等客户端向 `phone/cmd` 发布 JSON，日志出现中文变更描述、心跳延迟刷新；点「保存车辆状态」导出 `carstatus_yyyyMMdd_hhmmss.txt` 快照。

## 七、构建与运行

| 项 | 要求 |
| --- | --- |
| CMake | ≥ 3.19（`CMakeLists.txt` 首行声明） |
| C++ 标准 | C++17（`CMAKE_CXX_STANDARD 17` + `REQUIRED ON`） |
| Qt | Qt 6（Qt Creator 记录为 Desktop Qt 6.11.1 MinGW 64-bit；`find_package` 虽写 Qt6/Qt5 双兼容，代码却用了 Qt6 专有 API，见第九节） |
| Qt 模块 | `Widgets`、`Svg`、`SvgWidgets`、`Network`、`SerialBus`（**不含 QtMqtt**，MQTT 用第三方 QMQTT 库） |
| 第三方依赖 | QMQTT 源码放在 `./qmqtt` 且带 `CMakeLists.txt`，以静态库 `qmqtt` 链接（`-DQMQTT_STATIC`） |
| CAN 插件 | Qt 自带 `virtualcan`，无需真实 CAN 硬件、无需 socketcan |

```bash
cd v_car
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64   # Windows 可加 -G "MinGW Makefiles"
cmake --build build -j
./build/vehicle_gateway
```

产物名为 `vehicle_gateway`（**不是** `v_car`），工程已设 `WIN32_EXECUTABLE TRUE`、`MACOSX_BUNDLE TRUE`。运行前提：`8.138.233.135:1883` 上的 Broker 可达（上行发布与下行 `phone/cmd` 都依赖它）；手机端 App 不在本仓库，用 MQTTX / mosquitto_pub 发 JSON 即可验证下行链路。

## 八、技术栈与技术亮点

- **Qt Widgets + Designer**：界面由 `mainwindow.ui` 描述，代码只做业务逻辑，`ui->控件名` 直接访问；`AUTOUIC/AUTOMOC/AUTORCC` 全开，无需手写 moc/uic 命令。
- **Qt SerialBus 虚拟总线**：用官方 `virtualcan` 插件把「多个 ECU + 一条总线 + 一个网关」的多节点结构放进单进程，`Loopback + ReceiveOwn` 让 ECU 写的帧能被主窗口收到，无硬件即可演示 CAN 通信。
- **手写字节级协议解析**：`parseCanFrame()` 按帧 ID `switch` 分发，用 `payload[0] << 8 | payload[1]` 拼 uint16、用 bit0-3 位图表达 4 个车门，完全按字节偏移实现，体现真实 CAN 信号层处理，而非调用现成 DBC 解析器。
- **变化触发上云**：每个信号都与上次值比较，只有变化才 `publish`，稳态零流量，对应车载终端常见的「事件上云」策略。
- **ECU 仿真抽象**：`BaseECU` 抽出「QTimer 节拍 + 纯虚 `simTick()`」，四个子类只需造数组帧，周期由 `startSim(ms)` 一行决定，易于横向扩展新 ECU。
- **QMQTT 而非 QtMqtt**：MQTT 走社区第三方库 QMQTT（静态编译），代码中没有手写 CONNECT/PUBLISH 字节流；好处是不必安装 Qt 官方 QtMqtt 附加模块，代价是仓库需额外携带 `qmqtt` 源码。
- **QSS 主题工程化**：270 行样式表全部基于 objectName/id 选择器，颜色统一为背景 `#0F1726`、卡片 `#172238`、成功 `#10B981`、异常 `#EF4444`，状态色由 C++ 端 `setStyleSheet("color: …")` 覆盖。
- **JSON 直出**：`vehicledata.h` 中每个结构体都有成对的 `toJson()`/`fromJson()`，CAN 信号 → 结构体 → JSON 的转换集中一处，MQTT 层无需关心字段。

## 九、典型问题与注意事项

### 9.1 构建与依赖

- **缺 `qmqtt/` 目录，configure 直接失败**：`CMakeLists.txt` 检查 `${CMAKE_CURRENT_SOURCE_DIR}/qmqtt/CMakeLists.txt`，缺失即 `FATAL_ERROR("未找到 QMQTT 源码")`。需自行把 QMQTT 源码放到仓库根 `qmqtt/`。
- **缺 `image/logo.svg`，AUTORCC 会报错**：`res.qrc` 声明了 `<file>image/logo.svg</file>`，仓库里没有 `image/` 目录；且代码与 `.ui` 均未使用该图标（`<resources/>` 为空），可从 `res.qrc` 删除。
- **`CMakeLists.txt.user` 不应提交**：Qt Creator 本地配置，含本机绝对路径（如 `C:\Qt\code\vehicle_gateway\build\Desktop_Qt_6_11_1_MinGW_64_bit-Debug`）与本机 Kit id，换机即失效，建议删除并加入 `.gitignore`。
- **Qt5 兼容写法有名无实**：`find_package(QT NAMES Qt6 Qt5 …)` 声称双兼容，但 `mainwindow.cpp` 用了 `QJsonObject::toInteger()`（Qt 6 引入），Qt5 下无法编译，实际按 Qt6 使用。
- **声明未用的依赖**：`Svg`/`SvgWidgets` 已链接、`QSvgRenderer`/`QPixmap`/`QPainter` 已 include，但全代码无任何 SVG 渲染或绘图调用；`MQTT_PROJECT_INCLUDE_SRC` 宏已定义却未在源码出现。

### 9.2 功能未闭环（规划中）

- **BCM 下发回路未闭环**：把手机指令写回 `0x200` 的 `writeFrame` 整段被注释（原 `mainwindow.cpp` 第 489~503 行），`BcmECU` 中 `door/air_con/air_temperature` 为 `static` 且从不修改，故车门/空调永远上报 `0/0/24`，手机指令只改主窗口本地副本。
- **故障注入未实现**：`DtcECU` 的故障码与优先级恒为 0，界面永远「无故障活动 / 系统自检通过」。QSS 为 `btn_guzhangmoni`（故障模拟）预留了红色按钮样式，但 `.ui` 无该控件、也无对应槽。
- **真实车辆模式仅 UI 占位**：`radiobtn_zhenshicheliang` 未接任何槽，切换无效果。
- **SOC 只会单向下滑**：`QRandomGenerator::bounded(-1, 1)` 区间为 `[-1, 1)`，只能取 -1 或 0，SOC 从 80 缓慢减到 0 后不再变化。
- **`s_vcudata::fromJson()` 漏读 `vcuTemp`**：回读结构体时温度为 0，若将来做双向数据流需补齐。

### 9.3 健壮性与实现细节

- **MQTT 无重连、UI 状态先行**：`on_btn_mqttlianjie_clicked()` 在真正 `connectToHost()` 之前就把状态标签置绿、把按钮 `setEnabled(false)`；`onDisconnected()` 只打日志，无重连逻辑，断开后界面仍显示「已连接」且按钮不可再点，只能重启程序。
- **波特率文案不一致**：`BitRateKey` 设为 `5000`（5 kbit/s，界面标签也是 5kbit/s），启动日志却写死 `CAN已启动 波特率1kbit/s`；失败分支提示 `Check socketcan plugin` 也是遗留文案（实际用 `virtualcan`）。
- **发布/订阅计数不精确**：`label_mqttfabuxiaoxi` 在调用 `send*Info()` 之后无条件自增，而 `send*Info()` 未连接时直接 `return`，故「已连接又掉线」窗口期计数虚高。
- **全局静态状态**：`m_countfasong`、`m_countdingyue`、`m_fps` 是 `mainwindow.cpp` 的文件级 `static` 变量（带 `m_` 前缀却非成员），窗口重建不会复位。
- **快照导出只写一次**：每次点击都新建 `carstatus_*.txt` 并写入当次状态，`m_logFile` 存为成员但之后无人再写；重复点击还会泄漏上一个 `QFile` 句柄，默认目录 `%1../../../car_state_log` 是相对 `QDir::currentPath()` 拼接，未必存在。
- **`updateBcmUi()` 借用日志控件**：BCM 状态被写进 `listWidgetcan` 最后一行（`setText` 覆盖），会顶掉真正的报文日志。
- **`parseCanFrame()` 健壮性**：payload 不足 8 字节只 `qDebug` 丢弃；未识别帧 ID 走 `default` 静默忽略；`m_fps += 47 + 8 * payload.size()` 是粗略估算（固定 47 bit 开销），且在长度检查之前就累加。
- **进度条与温度量程**：`progressBar_2` 量程 -40~150，代码按 `vcuTemp` 直接 `setValue()`，负温度时填充比例视觉偏低；`progressBar_3` 未在代码中校验 0~100 越界。
- **单实例假设**：Client ID 固定 `vehicle`，同一 Broker 多开互相顶下线；用户名/密码为空，明文 1883 端口。
- **源码无 `TODO`/`FIXME` 标记**：未完成部分统一以「注释掉的代码块」遗留（9.2 的前两条即此类），后续改造时建议统一改成显式的占位注释。

## 十、相关仓库与备注

| 仓库 | 角色 | 说明 |
| --- | --- | --- |
| [collecton_node](https://github.com/YumK00/collecton_node) | CAN 节点 A | STM32F103 采集节点，负责传感器数据采集与上报 |
| [control_node](https://github.com/YumK00/control_node) | CAN 节点 B | STM32F103 执行节点，接收控制指令驱动执行机构 |
| [gateway_node](https://github.com/YumK00/gateway_node) | 边缘网关 | ESP32-S3，CAN ⇄ MQTT 协议转换，把总线数据经 WiFi 上云 |
| [weather_bureau](https://github.com/YumK00/weather_bureau) | Linux C++ 上位机 | 气象数据平台，服务端 / 数据处理方向 |
| **v_car（本仓库）** | 车载 T-Box 仿真 + 中控界面 | 用 Qt 虚拟 CAN 总线替代真实硬件，独立演示「ECU → CAN → 报文解析 → MQTT → 中控界面」链路，是作品集里的桌面端项目 |

- 与 `gateway_node` 的关系：两者**不是同一条链路**，也没有共用 Broker——本仓库是车载域的自有主题树（`car/ecu/*`、`phone/cmd`，Broker `8.138.233.135:1883`），网关是工业域的 `gw0805/*`（Broker `broker.emqx.io`）。可复用的是**同一套软件套路**：手写 CAN 帧的字节级解析 + 变化触发上云 + 用主题承载命令类型，因此本仓库可以作为网关项目的桌面演示工装，但不能直接与真实网关互连。
- 手机端 App 未包含在本仓库，「手机中控」由桌面 GUI 呈现，下行指令用任意 MQTT 客户端模拟即可。
- 明确标注**规划中**的部分：BCM 指令回写 CAN（`0x200` 下发）、故障注入按钮与对应 ECU 行为、真实车辆模式（`radiobtn_zhenshicheliang` 未接任何槽，仅 UI 占位）、MQTT 断线自动重连、SVG 图标/绘图资源的使用。
- 提交前建议：删除 `CMakeLists.txt.user`、补齐 `qmqtt/` 或改用 QtMqtt、处理 `res.qrc` 中缺失的 `image/logo.svg`，并补 `.gitignore`（`build/`、`*.user`）。
