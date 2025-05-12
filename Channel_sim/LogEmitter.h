// logemitter.h
#ifndef LOGEMITTER_H
#define LOGEMITTER_H

#include <QObject>
#include <QString>
#include <QCoreApplication> // For Q_GLOBAL_STATIC
#include <QDateTime>
#include <QTextStream>
#include <QMessageLogContext> // ��Ҫ�������ͷ�ļ�

class LogEmitter : public QObject
{
    Q_OBJECT
public:
    static LogEmitter* instance();
    explicit LogEmitter(QObject* parent = nullptr);
signals:
    void newLogMessage(const QString& message); // �źţ�Я����־��Ϣ

private:
   
    // �����Զ�����Ϣ���������ʲ����� LogEmitter ��˽�г�Ա�����ź�
    friend void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
};

// ʹ�� Q_GLOBAL_STATIC ������ LogEmitter �ĵ���
// ��������ȷ�����κεط��������Զ�����Ϣ�����������ܰ�ȫ�ط�����
Q_GLOBAL_STATIC(LogEmitter, globalLogEmitterInstance)

inline LogEmitter* LogEmitter::instance() {
    return globalLogEmitterInstance();
}

// ȫ�ֵ��Զ�����Ϣ���������� (��������� .cpp �ļ���)
void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
// ���ڱ���֮ǰ��Ϣ��������ָ��
extern QtMessageHandler previousMessageHandler;


#endif // LOGEMITTER_H