// logemitter.h
#ifndef LOGEMITTER_H
#define LOGEMITTER_H

#include <QObject>
#include <QString>
#include <QCoreApplication> // For Q_GLOBAL_STATIC
#include <QDateTime>
#include <QTextStream>
#include <QMessageLogContext> // 需要包含这个头文件

class LogEmitter : public QObject
{
    Q_OBJECT
public:
    static LogEmitter* instance();
    explicit LogEmitter(QObject* parent = nullptr);
signals:
    void newLogMessage(const QString& message); // 信号，携带日志消息

private:
   
    // 允许自定义消息处理函数访问并调用 LogEmitter 的私有成员或发射信号
    friend void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
};

// 使用 Q_GLOBAL_STATIC 来创建 LogEmitter 的单例
// 这样可以确保在任何地方（包括自定义消息处理函数）都能安全地访问它
Q_GLOBAL_STATIC(LogEmitter, globalLogEmitterInstance)

inline LogEmitter* LogEmitter::instance() {
    return globalLogEmitterInstance();
}

// 全局的自定义消息处理函数声明 (定义可以在 .cpp 文件中)
void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
// 用于保存之前消息处理器的指针
extern QtMessageHandler previousMessageHandler;


#endif // LOGEMITTER_H