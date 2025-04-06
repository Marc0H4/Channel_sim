#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_Channel_sim.h"
#include <QFileDialog>
#include "Udpserver.h"



class Channel_sim : public QMainWindow
{
    Q_OBJECT

public:
    Channel_sim(QWidget *parent = nullptr);
    ~Channel_sim();

signals:
    void ChannelStateChanging(int channel, bool state);


private:
    Ui::Channel_simClass ui;
	Udpserver* udp;
    QString file_name;
private slots:
    void Readfile();
    void getChannelState_1(int state);
    void getChannelState_2(int state);
    void getChannelState_3(int state);
    void getLostrate_1(int vaule);
    void getLostrate_2(int vaule);
    void getLostrate_3(int vaule);

};
