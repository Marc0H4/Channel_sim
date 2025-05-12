// logemitter.cpp (������� main.cpp, ����һ�������� .cpp)
#include "logemitter.h"
#include <QDebug> // Ϊ��ԭʼ�� qDebug ��� (�����Ҫ)

// ��ʼ�� LogEmitter ���캯�� (����Ϊ��)
LogEmitter::LogEmitter(QObject* parent) : QObject(parent) {}

// ����һ��ȫ�ֱ������洢�ɵ���Ϣ������
QtMessageHandler previousMessageHandler = nullptr;

// �Զ�����Ϣ��������ʵ��
void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    QString formattedMessage;
    QTextStream stream(&formattedMessage);

    stream << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ");

    switch (type) {
    case QtDebugMsg:
        // stream << "DEBUG: "; // ����ѡ�񲻼�ǰ׺����ΪqDebug������ǵ�����Ϣ
        break;
    case QtInfoMsg:
        stream << "INFO: ";
        break;
    case QtWarningMsg:
        stream << "����: ";
        break;
    case QtCriticalMsg:
        stream << "���ش���: ";
        break;
    case QtFatalMsg:
        stream << "��������: ";
        break;
    }
    stream << msg;

    // ͨ�� LogEmitter �ĵ��������ź�
    // ȷ�� LogEmitter::instance() ���ص�����Ч��ʵ��
    if (LogEmitter::instance()) {
        // ��Ϊ customMessageHandler �� LogEmitter ����Ԫ��
        // ������ֱ�ӷ��� LogEmitter ʵ�����ź�
        emit LogEmitter::instance()->newLogMessage(formattedMessage);
    }

    // ��ѡ����������ڿ���̨�������������֮ǰ����Ϣ������
    if (previousMessageHandler) {
        previousMessageHandler(type, context, msg);
    }
}