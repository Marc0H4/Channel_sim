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
    unsigned int  sysWord;       // 系统标识
    unsigned char idWord;        // 通道标识
    unsigned int  nowTime;       // 时间戳
    unsigned char versionNumber; // 协议版本
    unsigned char totalChannel;  // 总通道数
    unsigned char whichChannel;  // 当前通道
    unsigned char videoFormat;   // 视频格式
    unsigned short packetSize;   // 数据包大小
    unsigned char videoData[10000]; // 视频数据
};
#pragma pack()

struct SendPacket {
    videoStruct video;          // 视频数据包
    uint32_t crc32;             // CRC32校验
    uint8_t stream_type;        // 流类型
    uint8_t channel_seq;        // 通道序号
    uint8_t channel_index;      // 目标Socket索引
};

// 每个通道的上下文
struct ChannelContext {
    std::deque<SendPacket> packetQueue;  // 数据包队列
    QMutex queueMutex;                   // 队列专用锁
    QWaitCondition queueCondition;       // 队列条件变量

    QMutex stateMutex;                   // 状态专用锁
    std::atomic<int> enabled{ 0 };         // 通道启用状态
    std::atomic<double> lossRate{ 0.0 };   // 丢包率
    QWaitCondition stateCondition;       // 状态条件变量
};

class Udpserver : public QObject {
    Q_OBJECT
public:
    Udpserver(QObject* parent, QString desthost, int baseport);
    ~Udpserver();

    // 文件功能接口
    void SetFileName(const QString& filePath);
    void StartSending();
    void StopSending();
    void channelStateChange(int channel, bool state);
    void setLossRate(int channel, double rate);

private:
    // 网络相关
    SOCKET sockets[SOCKET_POOL_SIZE];
    sockaddr_in targetAddr[SOCKET_POOL_SIZE];

    // 文件相关
    QMutex fileMutex;                   // 文件路径专用锁
    QString currentFilePath;
    std::atomic<int> currentSocket{ 0 };   // 轮询指针
    const int packetSize = 1009;         // 数据包大小

    // 通道上下文
    ChannelContext channels[SOCKET_POOL_SIZE];
    QList<QThread*> workerThreads;
    std::atomic<bool> is_running{ false }; // 运行状态

    // 内部函数
    void initializeSockets();
    void fileReaderTask();
    void socketWorkerTask(int socket_index);
};
