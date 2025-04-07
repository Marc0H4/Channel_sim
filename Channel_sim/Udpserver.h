#pragma once
#define SOCKET_POOL_SIZE 3
#include <QObject>
#include <winsock2.h>
#include <QMutex>
#include <QWaitCondition>
#include <deque>
#include <atomic>
#include "udp_with_ulpfec.h"
#include "simple_client_server.h"
#pragma pack(1)
struct videoStruct {
    unsigned int  sysWord;       // ϵͳ��ʶ
    unsigned char idWord;        // ͨ����ʶ
    unsigned int  nowTime;       // ʱ���
    unsigned char versionNumber; // Э��汾
    unsigned char totalChannel;  // ��ͨ����
    unsigned char whichChannel;  // ��ǰͨ��
    unsigned char videoFormat;   // ��Ƶ��ʽ
    unsigned short packetSize;   // ���ݰ���С
    unsigned char videoData[10000]; // �����غ�
};
#pragma pack()

struct SendPacket {
    videoStruct video;          // ��Ƶ���ݰ�
	uint32_t crc32;             // CRC32У��
	uint8_t stream_type;        // ������
	uint8_t channel_seq;        // ͨ�����к�
    uint8_t channel_index;      // Ŀ��Socket����
};

// ÿ��ͨ���Ķ���������
struct ChannelContext {
    std::deque<SendPacket> packetQueue;  // �������ݶ���
    QMutex queueMutex;                   // ����ר����
    QWaitCondition queueCondition;       // ������������

    QMutex stateMutex;                   // ״̬������
    std::atomic<int> enabled{ 0 };         // ͨ������״̬
    std::atomic<double> lossRate{ 0.0 };   // ������
    QWaitCondition stateCondition;       // ״̬��������
};

class Udpserver : public QObject {
    Q_OBJECT
public:
    Udpserver(QObject* parent, QString desthost, int baseport);
    ~Udpserver();

    // ���Ĺ��ܽӿ�
    void SetFileName(const QString& filePath);
    void StartSending();
    void StopSending();
    void channelStateChange(int channel, bool state);
    void setLossRate(int channel, double rate);

private:
    // �������
    SOCKET sockets[SOCKET_POOL_SIZE];
    sockaddr_in targetAddr[SOCKET_POOL_SIZE];

    // �ļ������
    QMutex fileMutex;                   // �ļ�·��ר����
    QString currentFilePath;
    std::atomic<int> currentSocket{ 0 };   // ��ѯָ��
    const int packetSize = 1009;         // ���ݰ���С

    // ͨ������
    ChannelContext channels[SOCKET_POOL_SIZE];
    QList<QThread*> workerThreads;
    std::atomic<bool> is_running{ false }; // ����״̬

    // �ڲ�����
    void initializeSockets();
    void fileReaderTask();
    void socketWorkerTask(int socket_index);
};