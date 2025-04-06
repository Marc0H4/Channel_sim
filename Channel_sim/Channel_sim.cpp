#include "Channel_sim.h"

Channel_sim::Channel_sim(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);
	connect(ui.file_action,&QAction::triggered, this, &Channel_sim::Readfile);
	connect(ui.Channel1_checkbox, &QCheckBox::stateChanged, this, &Channel_sim::getChannelState_1);
	connect(ui.channel2_checkbox, &QCheckBox::stateChanged, this, &Channel_sim::getChannelState_2);
	connect(ui.channel3_checkbox, &QCheckBox::stateChanged, this, &Channel_sim::getChannelState_3);

	connect(ui.packetlost_bar1, &QSlider::valueChanged, this, &Channel_sim::getLostrate_1);
	connect(ui.packetlost_bar2, &QSlider::valueChanged, this, &Channel_sim::getLostrate_2);
	connect(ui.packetlost_bar3, &QSlider::valueChanged, this, &Channel_sim::getLostrate_3);

	udp = new Udpserver(this, "225.0.10.101", 5557);
	connect(ui.Play_btn, &QPushButton::clicked, udp, &Udpserver::StartSending);

	connect(this, &Channel_sim::ChannelStateChanging, udp, &Udpserver::channelStateChange);

	 //state_1 = 0; // 定义全局变量
	 //state_2 = 0; // 定义全局变量
	 //state_3 = 0; // 定义全局变量

}

Channel_sim::~Channel_sim()
{

    disconnect(ui.file_action, &QAction::triggered, this, &Channel_sim::Readfile);

	disconnect(ui.Channel1_checkbox, &QCheckBox::stateChanged, this, &Channel_sim::getChannelState_1);
	disconnect(ui.channel2_checkbox, &QCheckBox::stateChanged, this, &Channel_sim::getChannelState_2);
	disconnect(ui.channel3_checkbox, &QCheckBox::stateChanged, this, &Channel_sim::getChannelState_3);

	disconnect(ui.packetlost_bar1, &QSlider::valueChanged, this, &Channel_sim::getLostrate_1);
	disconnect(ui.packetlost_bar2, &QSlider::valueChanged, this, &Channel_sim::getLostrate_2);
	disconnect(ui.packetlost_bar3, &QSlider::valueChanged, this, &Channel_sim::getLostrate_3);

}
void Channel_sim::Readfile()
{

    QString runPath = QCoreApplication::applicationDirPath();//获取项目的根路径
    file_name = QFileDialog::getOpenFileName(this, QStringLiteral("选择文件"), runPath, "", nullptr, QFileDialog::DontResolveSymlinks);
	udp->SetFileName(file_name);
}
void Channel_sim::getChannelState_1(int state)
{
	if (ui.Channel1_checkbox->checkState())
	{
		emit this->ChannelStateChanging(0,true);
	}
	else
	{
		emit this->ChannelStateChanging(0,false);
	}


}
void Channel_sim::getChannelState_2(int state)
{
	if (ui.channel2_checkbox->checkState())
	{
		emit this->ChannelStateChanging(1,true);
	}
	else
	{
		emit this->ChannelStateChanging(1,false);
	}

}
void Channel_sim::getChannelState_3(int state)
{
	if (ui.channel3_checkbox->checkState())
	{
		emit this->ChannelStateChanging(2,true);
	}
	else
	{
		emit this->ChannelStateChanging(2,false);
	}
}
void Channel_sim::getLostrate_1(int vaule)
{
	ui.lostrate_label1->setText(QString("Drop:%%1").arg(vaule));
}
void Channel_sim::getLostrate_2(int vaule)
{
	ui.lostrate_label2->setText(QString("Drop:%%1").arg(vaule));
}
void Channel_sim::getLostrate_3(int vaule)
{
	ui.lostrate_label3->setText(QString("Drop:%%1").arg(vaule));
}