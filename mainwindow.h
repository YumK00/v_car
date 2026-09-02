#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include"mqttclient.h"
#include"bmsecu.h"
#include"vcuecu.h"
#include"bcmecu.h"
#include"dtcecu.h"
#include"vehicledata.h"
#include<QFile>
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_timeout();//时间定时器
    void on_CanFramesReceived();   // CAN 接收

    void on_btn_qidongcan_clicked();

    void on_btn_tingzhican_clicked();

    void on_btn_mqttlianjie_clicked();

    void onPhoneCommandArrived(const QJsonObject& cmdObj); //处理手机下发指令

    void on_tim2out();
    void on_pushButton_clicked();

private:
    Ui::MainWindow *ui;
    QCanBusDevice* m_candevice = nullptr;

    s_bcmdata m_bcmcurinfo;
    s_bmsdata m_bmscurinfo;
    s_dtcdata m_dtccurinfo;
    s_vcudata m_vcucurinfo;

    BmsECU* m_bms = nullptr;
    VcuECU* m_vcu = nullptr;
    BcmECU* m_bcm = nullptr;
    DtcECU* m_dtc = nullptr;

    MqttClient* m_mqttClient = nullptr;

    void parseCanFrame(const QCanBusFrame &frame);

    void updateBmsUi(const s_bmsdata &data);
    void updateVcuUi(const s_vcudata &data);
    void updateBcmUi(const s_bcmdata &data);
    void updateDtcUi(const s_dtcdata &data);
    void closeConnect();
    QString getCurrentTimeString();
    void writeLog(bool isRecv, bool isCan,const QString &logstr);

    uint32_t m_currentWindowBit; // 当前1秒窗口内累计bit，收到帧累加
    uint32_t m_showBps;          // UI用来显示的bps值，每秒更新一次

    QTimer* tim2 ;
    QFile* m_logFile;
};
#endif // MAINWINDOW_H
