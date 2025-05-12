#include "Channel_sim.h"

Channel_sim::Channel_sim(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

	stbar = this->statusBar();
	this->setStatusBar(stbar);

	connect(ui.file_action,&QAction::triggered, this, &Channel_sim::Readfile);
	connect(ui.Channel1_checkbox, &QCheckBox::stateChanged, this, &Channel_sim::getChannelState_1);
	connect(ui.channel2_checkbox, &QCheckBox::stateChanged, this, &Channel_sim::getChannelState_2);
	connect(ui.channel3_checkbox, &QCheckBox::stateChanged, this, &Channel_sim::getChannelState_3);

	connect(ui.packetlost_bar1, &QSlider::valueChanged, this, &Channel_sim::getLostrate_1);
	connect(ui.packetlost_bar2, &QSlider::valueChanged, this, &Channel_sim::getLostrate_2);
	connect(ui.packetlost_bar3, &QSlider::valueChanged, this, &Channel_sim::getLostrate_3);

	udp = new Udpserver(this, "225.0.10.101", 600);
	connect(ui.Play_btn, &QPushButton::clicked, udp, &Udpserver::StartSending);
	connect(ui.Play_btn, &QPushButton::clicked, this, &Channel_sim::start_message);
	connect(this, &Channel_sim::ChannelLostRateChanging, udp, &Udpserver::setLossRate);
	connect(this, &Channel_sim::ChannelStateChanging, udp, &Udpserver::channelStateChange);
	if (LogEmitter::instance()) {
		connect(LogEmitter::instance(), &LogEmitter::newLogMessage,
			this, &Channel_sim::appendLogToUi);
	}
	else {
		// 这种情况理论上不应该发生，因为 Q_GLOBAL_STATIC 会确保实例存在
		qWarning("LogEmitter 实例获取失败，无法连接日志信号。");
	}

	qDebug("Channel_sim 构造函数执行完毕。"); // 这条日志也应该显示在UI上
}



Channel_sim::~Channel_sim()
{
	if (LogEmitter::instance()) {
		bool disconnected = disconnect(LogEmitter::instance(), &LogEmitter::newLogMessage,
			this, &Channel_sim::appendLogToUi);

	}


	disconnect(ui.file_action, &QAction::triggered, this, &Channel_sim::Readfile);
	disconnect(ui.Channel1_checkbox, &QCheckBox::stateChanged, this, &Channel_sim::getChannelState_1);

	disconnect(ui.Play_btn, &QPushButton::clicked, udp, &Udpserver::StartSending);
    disconnect(ui.file_action, &QAction::triggered, this, &Channel_sim::Readfile);

	disconnect(ui.Channel1_checkbox, &QCheckBox::stateChanged, this, &Channel_sim::getChannelState_1);
	disconnect(ui.channel2_checkbox, &QCheckBox::stateChanged, this, &Channel_sim::getChannelState_2);
	disconnect(ui.channel3_checkbox, &QCheckBox::stateChanged, this, &Channel_sim::getChannelState_3);

	disconnect(ui.packetlost_bar1, &QSlider::valueChanged, this, &Channel_sim::getLostrate_1);
	disconnect(ui.packetlost_bar2, &QSlider::valueChanged, this, &Channel_sim::getLostrate_2);
	disconnect(ui.packetlost_bar3, &QSlider::valueChanged, this, &Channel_sim::getLostrate_3);
	disconnect(ui.Play_btn, &QPushButton::clicked, udp, &Udpserver::StartSending);

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
		stbar->showMessage("Channel 1 open", 3000); 

	}
	else
	{
		emit this->ChannelStateChanging(0,false);
		stbar->showMessage("Channel 1 close ", 3000); 

	}


}
void Channel_sim::getChannelState_2(int state)
{
	if (ui.channel2_checkbox->checkState())
	{
		emit this->ChannelStateChanging(1,true);
		stbar->showMessage("Channel 2 open", 3000);  

	}
	else
	{
		emit this->ChannelStateChanging(1,false);
		stbar->showMessage("Channel 2 close", 3000);  

	}

}
void Channel_sim::getChannelState_3(int state)
{
	if (ui.channel3_checkbox->checkState())
	{
		emit this->ChannelStateChanging(2,true);
		stbar->showMessage("Channel 3 open", 3000); 

	}
	else
	{
		emit this->ChannelStateChanging(2,false);
		stbar->showMessage("Channel 3 close", 3000);  

	}
}
void Channel_sim::getLostrate_1(int vaule)
{
	ui.lostrate_label1->setText(QString("Drop:%%1").arg((double)vaule/10));
	emit this->ChannelLostRateChanging(0, (double)vaule / 1000.0);
}
void Channel_sim::getLostrate_2(int vaule)
{
	ui.lostrate_label2->setText(QString("Drop:%%1").arg((double)vaule/10));
	emit this->ChannelLostRateChanging(1, (double)vaule / 1000.0);
}
void Channel_sim::getLostrate_3(int vaule)
{
	ui.lostrate_label3->setText(QString("Drop:%%1").arg((double)vaule/10));
	emit this->ChannelLostRateChanging(2, (double)vaule / 1000.0);
}
void Channel_sim::start_message()
{
	stbar->showMessage("Start sending", 3000);
}
void Channel_sim::appendLogToUi(const QString& message)
{

	if (ui.logTextEdit) { 
		ui.logTextEdit->append(message); 
		// 可以选择滚动到底部
		// ui.logTextEdit->verticalScrollBar()->setValue(ui.logTextEdit->verticalScrollBar()->maximum());
	}
	else {
		if (previousMessageHandler) {
			previousMessageHandler(QtDebugMsg, QMessageLogContext(), "UI日志控件未找到: " + message);
		}
		else {
			fprintf(stderr, "UI日志控件未找到: %s\n", qPrintable(message));
		}
	}
}