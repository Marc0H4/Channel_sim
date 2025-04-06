#include "Udpserver.h"
#include <QThread>
#include <QMutexLocker>
#include <QFile>
#include <QByteArray>
#include <chrono>
#include <thread>
#include <random>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// 构造函数与初始化
Udpserver::Udpserver(QObject* parent, QString dest_host, int Port)
    : QObject(parent) {
    memset(&targetAddr, 0, sizeof(targetAddr));
    targetAddr.sin_family = AF_INET;
    targetAddr.sin_port = htons(Port);
    inet_pton(AF_INET, dest_host.toStdString().c_str(), &targetAddr.sin_addr);
    initializeSockets();
}

Udpserver::~Udpserver() {
    StopSending();
    WSACleanup();
}

// Socket初始化
void Udpserver::initializeSockets() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;

    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        sockets[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sockets[i] == INVALID_SOCKET) {
            for (int j = 0; j < i; ++j) closesocket(sockets[j]);
            WSACleanup();
            return;
        }
        u_long mode = 1;
        ioctlsocket(sockets[i], FIONBIO, &mode); // 非阻塞模式
    }
}

// 设置文件路径（线程安全）
void Udpserver::SetFileName(const QString& filePath) {
    QMutexLocker lock(&fileMutex);
    currentFilePath = filePath;
}

// 文件读取线程
void Udpserver::fileReaderTask() {
    QString filePath;
    {
        QMutexLocker lock(&fileMutex);
        filePath = currentFilePath;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;

    // 包头模板（避免重复初始化）
    videoStruct baseHeader;
    baseHeader.sysWord = 0x01;
    baseHeader.nowTime = 1;
    baseHeader.versionNumber = 0x01;
    baseHeader.totalChannel = SOCKET_POOL_SIZE;
    baseHeader.videoFormat = 0x01;
    baseHeader.packetSize = packetSize;

    while (is_running && !file.atEnd()) {
        QByteArray buffer = file.read(packetSize);
        if (buffer.isEmpty()) break;

        // 计算目标通道（轮询）
        const int target_channel = currentSocket.load();

        // 构造数据包
        SendPacket pkt;
        pkt.video = baseHeader; // 复制包头
        pkt.video.idWord = target_channel;
        pkt.video.whichChannel = target_channel;
        pkt.socketIndex = target_channel;
        memcpy(pkt.video.videoData, buffer.constData(), buffer.size());

        // 写入目标通道队列
        ChannelContext& ctx = channels[target_channel];
        {
            QMutexLocker lock(&ctx.queueMutex);
            ctx.packetQueue.push_back(pkt);
            ctx.queueCondition.wakeOne(); // 精确唤醒对应工作线程
        }

        // 更新轮询指针
        currentSocket = (target_channel + 1) % SOCKET_POOL_SIZE;
        baseHeader.sysWord++; // 全局序列号递增

        // 流量控制（3Mbps）
        std::this_thread::sleep_for(
            std::chrono::microseconds((packetSize * 8 * 1'000'000) / 3'000'000)
        );
    }
    file.close();
}

// 通道工作线程
void Udpserver::socketWorkerTask(int socket_index) {
    ChannelContext& ctx = channels[socket_index];
    std::mt19937 generator(std::random_device{}());

    while (is_running) {
        // === 阶段1：检查通道启用状态 ===
        {
            QMutexLocker stateLock(&ctx.stateMutex);
            while (ctx.enabled == 0 && is_running) {
                ctx.stateCondition.wait(&ctx.stateMutex);
            }
        }
        if (!is_running) break;

        // === 阶段2：处理数据包 ===
        SendPacket pkt;
        bool packet_found = false;
        {
            QMutexLocker queueLock(&ctx.queueMutex);
            if (!ctx.packetQueue.empty()) {
                pkt = ctx.packetQueue.front();
                ctx.packetQueue.pop_front();
                packet_found = true;
            }
        }

        if (packet_found) {
            // 丢包率检测
            if (ctx.lossRate > 0.0 &&
                std::uniform_real_distribution<double>(0, 1)(generator) < ctx.lossRate) {
                continue; // 模拟丢包
            }

            // 发送数据包
            size_t header_size = offsetof(videoStruct, videoData);
            size_t total_length = header_size + pkt.video.packetSize;
            sendto_fec(
                sockets[socket_index],
                reinterpret_cast<char*>(&pkt.video),
                total_length,
                0,
                (sockaddr*)&targetAddr,
                sizeof(targetAddr),
                10,   // fec组大小
                2,    // fec冗余包
                3000  // 超时（us）
            );
            qDebug("Channel %d sent seq=%u", socket_index, pkt.video.sysWord);
        }
        //// 队列空时短暂休眠防止CPU空转
        //else {
        //    std::this_thread::sleep_for(1ms);
        //}
    }
}

// 启动发送
void Udpserver::StartSending() {
    if (currentFilePath.isEmpty()) return;
    is_running = true;

    // 启动文件读取线程
    QThread* fileThread = QThread::create([this]() { fileReaderTask(); });
    fileThread->start();
    workerThreads.append(fileThread);

    // 启动工作线程
    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        QThread* thread = QThread::create([this, i]() { socketWorkerTask(i); });
        thread->start();
        workerThreads.append(thread);
    }
}

// 停止发送
void Udpserver::StopSending() {
    is_running = false;

    // 唤醒所有等待线程
    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        closesocket(sockets[i]);
        {
            QMutexLocker lock(&channels[i].stateMutex);
            channels[i].stateCondition.wakeAll();
        }
        {
            QMutexLocker lock(&channels[i].queueMutex);
            channels[i].queueCondition.wakeAll();
        }
    }

    // 等待所有线程退出
    foreach(QThread * thread, workerThreads) {
        thread->quit();
        thread->wait();
    }
    workerThreads.clear();
    WSACleanup();
}

// 通道状态控制
void Udpserver::channelStateChange(int channel, bool state) {
    const int idx = channel % SOCKET_POOL_SIZE;
    if (idx < 0 || idx >= SOCKET_POOL_SIZE) return;

    ChannelContext& ctx = channels[idx];
    {
        QMutexLocker lock(&ctx.stateMutex);
        ctx.enabled.store(state ? 1 : 0);
    }
    ctx.stateCondition.wakeOne(); // 精确唤醒状态等待
}

// 设置丢包率
void Udpserver::setLossRate(int channel, double rate) {
    if (channel >= 0 && channel < SOCKET_POOL_SIZE) {
        channels[channel].lossRate.store(rate);
    }
}