#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QSvgRenderer>
#include <QPixmap>
#include <QPainter>
#include <QTime>
#include <QTimer>
#include <QDebug>
#include <QDateTime>

using namespace QMQTT;

static uint m_countfasong = 0;
static uint m_countdingyue = 0;
#define MAX_ZHENLV 5000
static uint  m_fps=0;
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_currentWindowBit(0)
    , m_showBps(0)
    ,tim2(new QTimer(this))
{
    ui->setupUi(this);

    this->setFocusPolicy(Qt::StrongFocus);
    this->setFocus();

    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::on_timeout);
    timer->start(1000);

    connect(tim2, &QTimer::timeout, this, &MainWindow::on_tim2out);

}
//can日志发出定时器
void MainWindow::on_tim2out(){
    QString str = QString("[%1 km/h | SOC %2% | MCU %3℃]")
                        .arg(m_vcucurinfo.vcuSpeed)
                      .arg(m_bmscurinfo.batterySoc)
                      .arg(m_vcucurinfo.vcuTemp);
    writeLog(0,1,str);
}

//日志工具函数
QString MainWindow::getCurrentTimeString() {
    return QTime::currentTime().toString("hh:mm:ss");
}
//isRecv：0=tx，1=rx
//isCan 1=can，0=mqtt
void MainWindow::writeLog(bool isRecv,bool isCan,const QString& logstr){
    QString writelog;
    if(isRecv){
        writelog = "[" + getCurrentTimeString() + "]RX " +logstr;
    }
    else{
        writelog = "[" + getCurrentTimeString() + "]TX " + logstr;
    }
    if(isCan){
        ui->listWidgetcan->addItem(writelog);
        ui->listWidgetcan->scrollToBottom();
    }
    else{
        ui->listWidgetmqtt->addItem(writelog);
        ui->listWidgetmqtt->scrollToBottom();
    }
}
// ---------- CAN 接收 ----------
void MainWindow::on_CanFramesReceived()
{
    while (m_candevice && m_candevice->framesAvailable()) {
        QCanBusFrame frame = m_candevice->readFrame();
        if (frame.isValid()) {
            parseCanFrame(frame);
        }
    }
}

// ---------- 界面更新 ----------
void MainWindow::updateBmsUi(const s_bmsdata &data)
{
    ui->label_dianchi->setText(QString("%1").arg(data.batterySoc));
    ui->progressBar_3->setValue(static_cast<int>(data.batterySoc));
}

void MainWindow::updateVcuUi(const s_vcudata &data)
{
    ui->label_chesu->setText(QString("%1").arg(data.vcuSpeed));
    ui->label_dianjiwendu->setText(QString("%1").arg(data.vcuTemp));
    ui->progressBar_2->setValue(static_cast<int>(data.vcuTemp));
}

void MainWindow::updateBcmUi(const s_bcmdata &data)
{
    QString text = QString("车门:%1 空调:%2 温度:%3")
                       .arg(data.door_status)
                       .arg(data.air_con_status)
                       .arg(data.air_temperature);
    if (ui->listWidgetcan->count() > 0) {
        ui->listWidgetcan->item(ui->listWidgetcan->count() - 1)->setText(text);
    } else {
        ui->listWidgetcan->addItem(text);
    }
}

void MainWindow::updateDtcUi(const s_dtcdata &data)
{
    QString faultText;
    switch (data.guzhangma) {
    case 0x00:
        faultText = "无故障活动";
        ui->label_4->setText("系统自检通过");
        break;
    case 0x01:
        faultText = "左前门锁故障";
        ui->label_4->setText("系统自检异常");
        break;
    case 0x02:
        faultText = "右前门锁故障";
        ui->label_4->setText("系统自检异常");
        break;
    case 0x03:
        faultText = "左后门锁故障";
        ui->label_4->setText("系统自检异常");
        break;
    case 0x04:
        faultText = "右后门锁故障";
        ui->label_4->setText("系统自检异常");
        break;
    case 0x05:
        faultText = "空调压缩机过流";
        ui->label_4->setText("系统自检异常");
        break;
    case 0x06:
        faultText = "空调制冷剂不足";
        ui->label_4->setText("系统自检异常");
        break;
    default:
        faultText = QString("故障码: 0x%1").arg(data.guzhangma, 2, 16, QChar('0'));
        ui->label_4->setText("系统自检异常");
        break;
    }
    ui->label_guzhanghuodong->setText(faultText);

    if (data.guzhangma == 0x00) {
        ui->label_guzhanghuodong->setStyleSheet("color: #10B981;");
    } else {
        if (data.youxianji == 1)
            ui->label_guzhanghuodong->setStyleSheet("color: #FF8C00;");
        else if (data.youxianji == 2)
            ui->label_guzhanghuodong->setStyleSheet("color: red;");
    }
}

// ---------- CAN 帧解析 ----------
void MainWindow::parseCanFrame(const QCanBusFrame &frame)
{
    QByteArray payload = frame.payload();

    m_fps=m_fps+47+8*payload.size();
    //qDebug()<<"m_fps="<<m_fps;

    if (payload.size() < 8) {
        qDebug() << "payload length error, len=" << payload.size();
        return;
    }
    switch (frame.frameId()) {
    case 0x120: // BMS电池报文
    {
        uint16_t soc = static_cast<uint16_t>((payload[0] << 8) | (payload[1] & 0xFF));
        if (m_bmscurinfo.batterySoc != soc) {
            //数据变化才改ui和发送mqtt报文
            m_bmscurinfo.batterySoc = soc;
            updateBmsUi(m_bmscurinfo);
            if(m_mqttClient != nullptr)
            {
                m_mqttClient->sendBmsInfo(m_bmscurinfo);
                ui->label_mqttfabuxiaoxi->setText(QString::number(++m_countfasong));
            }
        }
        break;
    }

    case 0x100: // VCU整车运行报文
    {
        uint16_t speed = static_cast<uint16_t>((payload[0] << 8) | (payload[1] & 0xFF));
        uint16_t vcutemp = static_cast<uint16_t>((payload[2] << 8) | (payload[3] & 0xFF));
        if (m_vcucurinfo.vcuSpeed != speed || vcutemp != m_vcucurinfo.vcuTemp) {
            m_vcucurinfo.vcuSpeed = speed;
            m_vcucurinfo.vcuTemp = vcutemp ;
            updateVcuUi(m_vcucurinfo);
            if(m_mqttClient != nullptr)
            {
                m_mqttClient->sendVcuInfo(m_vcucurinfo);
                ui->label_mqttfabuxiaoxi->setText(QString::number(++m_countfasong));
            }
        }
        break;
    }

    case 0x110: // DTC故障报文
    {
        uint8_t guzhangma = static_cast<uint8_t>(payload[0]);
        uint8_t youxianji = static_cast<uint8_t>(payload[1]);

        if (m_dtccurinfo.guzhangma != guzhangma || m_dtccurinfo.youxianji != youxianji) {
            m_dtccurinfo.guzhangma = guzhangma;
            m_dtccurinfo.youxianji = youxianji;
            updateDtcUi(m_dtccurinfo);
            if(m_mqttClient != nullptr)
            {
                m_mqttClient->sendDtcInfo(m_dtccurinfo);
                ui->label_mqttfabuxiaoxi->setText(QString::number(++m_countfasong));
            }
        }
        break;
    }

    case 0x200: // BCM车身控制报文
    {
        uint8_t door_state = static_cast<uint8_t>(payload[0]);
        uint8_t ac_state = static_cast<uint8_t>(payload[1]);
        uint8_t air_temp = static_cast<uint8_t>(payload[2]);

        if (m_bcmcurinfo.door_status != door_state ||
            m_bcmcurinfo.air_con_status != ac_state ||
            m_bcmcurinfo.air_temperature != air_temp)
        {
            m_bcmcurinfo.door_status = door_state;
            m_bcmcurinfo.air_con_status = ac_state;
            m_bcmcurinfo.air_temperature = air_temp;
            updateBcmUi(m_bcmcurinfo);
            if(m_mqttClient != nullptr)
            {
                m_mqttClient->sendBcmInfo(m_bcmcurinfo);
                ui->label_mqttfabuxiaoxi->setText(QString::number(++m_countfasong));
            }
        }
        break;
    }

    default:
        break;
    }
}

// ---------- 断开连接 ----------
void MainWindow::closeConnect()
{
    if (m_bms) {
        m_bms->stopSim();
        delete m_bms;
        m_bms = nullptr;
    }
    if (m_vcu) {
        m_vcu->stopSim();
        delete m_vcu;
        m_vcu = nullptr;
    }
    if (m_bcm) {
        m_bcm->stopSim();
        delete m_bcm;
        m_bcm = nullptr;
    }
    if (m_dtc) {
        m_dtc->stopSim();
        delete m_dtc;
        m_dtc = nullptr;
    }
    if (m_candevice) {
        if (m_candevice->state() == QCanBusDevice::ConnectedState) {
            m_candevice->disconnectDevice();
        }
        delete m_candevice;
        m_candevice = nullptr;
    }
}

MainWindow::~MainWindow()
{
    closeConnect();
    if(m_mqttClient != nullptr)
    {
        delete m_mqttClient;
        m_mqttClient = nullptr;
    }
    delete ui;
}

// ---------- 定时器 ----------
void MainWindow::on_timeout()
{
    ui->timeedit1->setDateTime(QDateTime::currentDateTime());

    ui->progressbar_zhenlv->setValue(m_fps);
    ui->label_zhenlv->setText(QString::number(m_fps));
    //qDebug()<<"m_fps="<<m_fps;
    m_fps = 0;
}

// ---------- 启动 CAN ----------
void MainWindow::on_btn_qidongcan_clicked()
{
    closeConnect();
    ui->timeedit2->setTime(QTime::currentTime());

    QCanBus *canBus = QCanBus::instance();
    m_candevice = canBus->createDevice("virtualcan", "can0");

    if (m_candevice) {
        m_candevice->setConfigurationParameter(QCanBusDevice::LoopbackKey, true);
        m_candevice->setConfigurationParameter(QCanBusDevice::ReceiveOwnKey, true);
        if (m_candevice->connectDevice()) {
            connect(m_candevice, &QCanBusDevice::framesReceived,
                    this, &MainWindow::on_CanFramesReceived);
            qDebug() << "CAN device connected successfully.";
            m_candevice->setConfigurationParameter(QCanBusDevice::BitRateKey,MAX_ZHENLV);

            m_bms = new BmsECU(m_candevice, this);
            m_vcu = new VcuECU(m_candevice, this);
            m_bcm = new BcmECU(m_candevice, this);
            m_dtc = new DtcECU(m_candevice, this);

            m_bms->startSim(1300);//电池
            m_vcu->startSim(300);//整车运行
            m_bcm->startSim(2400);//车门
            m_dtc->startSim(180);//故障

            tim2->start(5000);
            ui->listWidgetcan->addItem(QString("[%1]CAN已启动 波特率1kbit/s").arg(getCurrentTimeString()));
            ui->listWidgetcan->scrollToBottom();
            ui->label_yunxingzhuangtai->setText("运行中");
            ui->label_yunxingzhuangtai->setStyleSheet("color: #10B981;");
            ui->label_zongxianzhuangtai->setText("通讯运行");
        } else {
            qWarning() << "Failed to connect CAN device:" << m_candevice->errorString();
            delete m_candevice;
            m_candevice = nullptr;
            ui->label_yunxingzhuangtai->setText("连接失败");
            ui->label_yunxingzhuangtai->setStyleSheet("color: #EF4444;");
        }
    } else {
        qWarning() << "Failed to create CAN device. Check socketcan plugin.";
        ui->label_yunxingzhuangtai->setText("设备创建失败");
        ui->label_yunxingzhuangtai->setStyleSheet("color: #EF4444;");
    }
}

// ---------- 停止 CAN ----------
void MainWindow::on_btn_tingzhican_clicked()
{
    closeConnect();

    tim2->stop();
    ui->listWidgetcan->addItem(QString("[%1]CAN已安全停止").arg(getCurrentTimeString()));
    ui->listWidgetcan->scrollToBottom();

    ui->label_yunxingzhuangtai->setText("已停止");
    ui->label_yunxingzhuangtai->setStyleSheet("color: #EF4444;");
    ui->label_chesu->setText("--");
    ui->label_dianchi->setText("--");
    ui->label_dianjiwendu->setText("--");
    ui->label_zongxianzhuangtai->setText("等待启动");
}

//---------------连接mqtt-----------------
void MainWindow::on_btn_mqttlianjie_clicked()
{
    if(m_mqttClient != nullptr)
    {
        m_mqttClient->disconnectBroker();
        delete m_mqttClient;
        m_mqttClient = nullptr;
    }
    m_mqttClient = new MqttClient(this);
    if(!m_mqttClient->mqttclientIsConnected()){
        connect(m_mqttClient, &MqttClient::sigRecvPhoneCmd,
                this, &MainWindow::onPhoneCommandArrived);
        ui->label_mqttzaixian->setText("会话稳定");
        ui->label_mqttzaixian->setStyleSheet("color: #10B981;");
        ui->label_mqttlianjiezhuangtai->setText("已连接");
        ui->label_mqttlianjiezhuangtai->setStyleSheet("color: #10B981;");
        //ui->btn_mqttlianjie->setText("断开Broker");
        ui->btn_mqttlianjie->setEnabled(false);
        ui->listWidgetmqtt->addItem(QString("[%1] CONNACK Broker连接已开启").arg(getCurrentTimeString()));
        ui->listWidgetmqtt->scrollToBottom();
        m_mqttClient->connectBroker();
    }
}

void MainWindow::onPhoneCommandArrived(const QJsonObject &cmdObj)
{
    //qDebug()<<"cmdobj["<<cmdObj<<"]";
    //cmdobj[ QJsonObject({"air_con_status":3,"air_temperature":24,"door_status":15}) ]
    uint8_t airconstatus_temp = static_cast<uint8_t>(cmdObj["air_con_status"].toInt(0));
    uint8_t airtemperature_temp = static_cast<uint8_t>(cmdObj["air_temperature"].toInt(24));
    uint8_t doorstatus_temp = static_cast<uint8_t>(cmdObj["door_status"].toInt(0));
    qint64 objtimestamp = cmdObj["timestamp"].toInteger();

    qint64 curtimetamp = QDateTime::currentMSecsSinceEpoch();
    //qDebug()<<curtimetamp-objtimestamp;
    ui->label_mqttxintiaoyanchi->setText(QString::number(curtimetamp-objtimestamp));
    bool isAirStatusChanged = false;
    bool isAirTempChanged = false;
    bool isDoorChanged = false;

    uint8_t oldDoor = m_bcmcurinfo.door_status;
    uint8_t newDoor = doorstatus_temp;

    if(m_bcmcurinfo.air_con_status != airconstatus_temp){
        m_bcmcurinfo.air_con_status = airconstatus_temp;
        isAirStatusChanged = true;
    }
    if(m_bcmcurinfo.air_temperature != airtemperature_temp){
        m_bcmcurinfo.air_temperature = airtemperature_temp;
        isAirTempChanged =true;
    }
    if(oldDoor != newDoor){
        m_bcmcurinfo.door_status = newDoor;
        isDoorChanged = true;
    }

    // ========== 空调逻辑 ==========
    if(isAirStatusChanged){
        QString air_str;
        uint8_t ac = m_bcmcurinfo.air_con_status;
        if((ac & 0x01) == 0)
        {
            air_str="空调已关闭";
        }
        else if( (ac & 0x02) !=0 ) //bit1 制冷
        {
            air_str= QString("空调已开启制冷模式,当前温度:%1").arg(m_bcmcurinfo.air_temperature);
        }
        else if( (ac & 0x04) !=0 ) //bit2 制热
        {
            air_str= QString("空调已开启制热模式,当前温度:%1").arg(m_bcmcurinfo.air_temperature);
        }
        else
        {
            air_str="空调开启，未知模式";
        }
        writeLog(1,0,air_str);
    }
    if(isAirTempChanged){
        QString airtem_str= QString("调节温度:%1℃").arg(m_bcmcurinfo.air_temperature);
        writeLog(1,0,airtem_str);
    }

    // ========== 车门状态 1=关门，0=开门==========
    if(isDoorChanged){
        QStringList changeDoorList;

        if( (oldDoor ^ newDoor) & 0x01 )
        {
            if( newDoor & 0x01 )
                changeDoorList << "左前门关门";
            else
                changeDoorList << "左前门开门";
        }
        if( (oldDoor ^ newDoor) & 0x02 )
        {
            if( newDoor & 0x02 )
                changeDoorList << "右前门关门";
            else
                changeDoorList << "右前门开门";
        }
        if( (oldDoor ^ newDoor) & 0x04 )
        {
            if( newDoor & 0x04 )
                changeDoorList << "左后门关门";
            else
                changeDoorList << "左后门开门";
        }
        if( (oldDoor ^ newDoor) & 0x08 )
        {
            if( newDoor & 0x08 )
                changeDoorList << "右后门关门";
            else
                changeDoorList << "右后门开门";
        }

        QString door_str = "车门状态变更：" + changeDoorList.join(" | ");
        writeLog(1,0,door_str);
    }

    ui->label_mqttdingyuezhuti->setText(QString::number(++m_countdingyue));

    // ========== 组装CAN 0x200报文发送给BCM ==========
    // if(m_candevice == nullptr || m_candevice->state() != QCanBusDevice::ConnectedState)
    // {
    //     qDebug()<<"CAN设备未连接，不发送BCM指令";
    //     return;
    // }
    // QCanBusFrame frame;
    // frame.setFrameId(0x200);
    // QByteArray payload(8, 0x00);
    // payload[0] = m_bcmcurinfo.door_status;
    // payload[1] = m_bcmcurinfo.air_con_status;
    // payload[2] = m_bcmcurinfo.air_temperature;
    // frame.setPayload(payload);
    // bool ok = m_candevice->writeFrame(frame);
    // qDebug()<<"下发BCM CAN0x200 "<<(ok?"成功":"失败");
}

#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QDir>

void MainWindow::on_pushButton_clicked()
{
    writeLog(0,0,"发起快照请求...");
    QString dirPath = QFileDialog::getExistingDirectory(
        this,
        tr("选择日志保存文件夹"),
        QString("%1../../../car_state_log").arg(QDir::currentPath()),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );

    if(dirPath.isEmpty())
    {
        writeLog(0,0,"用户取消选择文件夹");
        return;
    }
    QDir m_dir(dirPath);
    if (!m_dir.exists()) {
        QMessageBox::warning(this, "警告", "路径不存在", QMessageBox::Yes);
        writeLog(0,0,"用户选择路径不存在");
        return;
    }
    QString fileName = QDateTime::currentDateTime().toString("'carstatus_'yyyyMMdd_hhmmss'.txt'");
    QString fullFilePath = QDir(dirPath).filePath(fileName);

    QFile tmpFile(fullFilePath);
    bool createOk = tmpFile.open(QIODevice::WriteOnly | QIODevice::Text);
    if (!createOk) {
        QMessageBox::warning(this, tr("错误"),
                             tr("创建文件失败:\n%1").arg(tmpFile.errorString()));
        writeLog(0,0,"用户创建文件失败");
        return;
    }
    tmpFile.close();

    m_logFile = new QFile(fullFilePath, this);
    if (!m_logFile->open(QIODevice::Append | QIODevice::Text)) {
        QMessageBox::warning(this, tr("错误"),
                             tr("文件创建成功，但打开失败:\n%1").arg(m_logFile->errorString()));
        delete m_logFile;
        m_logFile = nullptr;
        writeLog(0,0,"用户文件创建成功，但打开失败");
        return;
    }
    QTextStream stream(m_logFile);
    stream << "车辆状态日志记录\n";
    stream << "启动时间: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
    stream << "batterySoc: " << m_bmscurinfo.batterySoc << "\n";
    stream << "vcuSpeed: " << m_vcucurinfo.vcuSpeed << "\n";
    stream << "air_con_status: " << m_bcmcurinfo.air_con_status << "\n";
    stream << "air_temperature: " << m_bcmcurinfo.air_temperature << "\n";
    stream << "door_status: " << m_bcmcurinfo.door_status << "\n";
    stream << "guzhangma: " << m_dtccurinfo.guzhangma << "\n";
    stream.flush();

    writeLog(1,0,QString("已保存状态快照到文件(%1)").arg(fullFilePath));
}

