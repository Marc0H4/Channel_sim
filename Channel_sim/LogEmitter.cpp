// logemitter.cpp (或者你的 main.cpp, 或者一个工具类 .cpp)
#include "logemitter.h"
#include <QDebug> // 为了原始的 qDebug 输出 (如果需要)

// 初始化 LogEmitter 构造函数 (可以为空)
LogEmitter::LogEmitter(QObject* parent) : QObject(parent) {}

// 定义一个全局变量来存储旧的消息处理器
QtMessageHandler previousMessageHandler = nullptr;

// 自定义消息处理函数的实现
void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    QString formattedMessage;
    QTextStream stream(&formattedMessage);

    stream << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ");

    switch (type) {
    case QtDebugMsg:
        // stream << "DEBUG: "; // 可以选择不加前缀，因为qDebug本身就是调试信息
        break;
    case QtInfoMsg:
        stream << "INFO: ";
        break;
    case QtWarningMsg:
        stream << "警告: ";
        break;
    case QtCriticalMsg:
        stream << "严重错误: ";
        break;
    case QtFatalMsg:
        stream << "致命错误: ";
        break;
    }
    stream << msg;

    // 通过 LogEmitter 的单例发射信号
    // 确保 LogEmitter::instance() 返回的是有效的实例
    if (LogEmitter::instance()) {
        // 因为 customMessageHandler 是 LogEmitter 的友元，
        // 它可以直接发射 LogEmitter 实例的信号
        emit LogEmitter::instance()->newLogMessage(formattedMessage);
    }

    // 可选：如果还想在控制台看到输出，调用之前的消息处理器
    if (previousMessageHandler) {
        previousMessageHandler(type, context, msg);
    }
}