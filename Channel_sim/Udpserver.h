#pragma once
#define SOCKET_POOL_SIZE 3
#include <QObject>
#include <winsock2.h>
#include <QMutex>
#include <QWaitCondition>
#include <deque>
#include <atomic>
#include <memory> // <--- 添加 include

#include "udp_with_ulpfec.h" // 包含 ForwardErrorCorrection 定义

#pragma pack(1) // 使用 push 保存当前对齐设置
struct videoStruct { 
    unsigned int  sysWord;       // 系统标识
    unsigned char idWord;        // 通道标识
    unsigned int  nowTime;       // 时间戳
    unsigned char versionNumber; // 协议版本
    unsigned char totalChannel;  // 总通道数
    unsigned char whichChannel;  // 当前通道
    unsigned char videoFormat;   // 视频格式
    unsigned short packetSize;   // 数据包大小
    unsigned char videoData[1009]; // 视频数据
};
#pragma pack() // 恢复之前的对齐设置

//#pragma pack(1)
struct SendPacket {
    std::unique_ptr<ForwardErrorCorrection::Packet> packet_to_send;  //FEC包
    uint32_t crc32 = 0;         // CRC32校验
    uint8_t stream_type = 0x01; // 流类型 
    uint8_t channel_index;      // 目标通道号
    uint8_t seq;                // 序列号
    // 负载大小不是头信息，只是用于方便计算
    size_t actual_payload_size; // 负载大小
};
//#pragma pack()
// 每个通道的上下文
struct ChannelContext {
    // 队列现在存储 SendPacket 对象
    std::deque<SendPacket> packetQueue;
    QMutex queueMutex;
    QWaitCondition queueCondition;

    QMutex stateMutex;
    std::atomic<int> enabled{ 0 };
    std::atomic<double> lossRate{ 0.0 };
    QWaitCondition stateCondition;
};

class Udpserver : public QObject {
    Q_OBJECT
public:
    Udpserver(QObject* parent, QString desthost, int baseport);
    ~Udpserver();

    // --- 防止拷贝 ---
    Udpserver(const Udpserver&) = delete;
    Udpserver& operator=(const Udpserver&) = delete;

    // --- （可选）允许移动 ---
    // QObject 通常不可移动，所以这里也删除移动操作
    Udpserver(Udpserver&&) = delete;
    Udpserver& operator=(Udpserver&&) = delete;


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
    QMutex fileMutex;
    QString currentFilePath;
    std::atomic<int> currentSocket{ 0 };
    const int flightpkt_header_size = 15; // 试飞院的头大小
    const size_t fec_header_size = 6;     // FEC 的头大小
    const size_t packet_header_size = 7;  // 协议的头大小
    const int readChunkSize = 1009; // 文件读取块大小 (可以调整)
    const int total_length = readChunkSize + packet_header_size + fec_header_size + flightpkt_header_size;
    int sysword = 0;
    std::atomic<uint8_t> globalSeqCounter{ 0 };
    // 通道上下文
    ChannelContext channels[SOCKET_POOL_SIZE];
    QList<QThread*> workerThreads;
    std::atomic<bool> is_running{ false };

    //FEC相关
    ForwardErrorCorrection fec; // FEC对象 (包含内部 buffer_packets)
    const int fec_k = 10; // 每组多少个源数据包
    const int fec_r = 2;  // 每组多少个冗余包

    // 内部函数
    void initializeSockets();
    void fileReaderTask();
    void socketWorkerTask(int socket_index);
};