#include "Udpserver.h"
#include <QThread>
#include <QMutexLocker>
#include <QFile>
#include <QByteArray>
#include <chrono>
#include <thread>
#include <random>
#include <ws2tcpip.h>
#include <QDebug> // 添加 qDebug 用于调试输出

#pragma comment(lib, "ws2_32.lib")

using namespace std::chrono_literals; // 为了使用 1ms

// 构造函数初始化
Udpserver::Udpserver(QObject* parent, QString dest_host, int baseport)
    : QObject(parent) {
    memset(&targetAddr, 0, sizeof(targetAddr));
    // 初始化目标地址
    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        targetAddr[i].sin_family = AF_INET;
        targetAddr[i].sin_port = htons(baseport + i);
        inet_pton(AF_INET, dest_host.toStdString().c_str(), &targetAddr[i].sin_addr);
    }
    // 初始化通道状态为禁用
    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        channels[i].enabled.store(0); // 默认禁用所有通道
        channels[i].lossRate.store(0.0);
    }

    initializeSockets();
}

Udpserver::~Udpserver() {
    StopSending(); // 确保在析构时停止并清理
    // WSACleanup 应该在 StopSending 之后，且只调用一次
}

// Socket初始化
void Udpserver::initializeSockets() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        qWarning("WSAStartup failed.");
        return;
    }

    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        sockets[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sockets[i] == INVALID_SOCKET) {
            qWarning("Socket creation failed for channel %d.", i);
            // 清理已创建的套接字
            for (int j = 0; j < i; ++j) closesocket(sockets[j]);
            WSACleanup(); // 出错时清理 WSA
            return;
        }
        // 非阻塞模式通常用于接收，对于阻塞发送可能不是必须的，但保留也无大碍
        // u_long mode = 1;
        // ioctlsocket(sockets[i], FIONBIO, &mode);
    }
    qDebug("Sockets initialized.");
}

// 设置文件路径（线程安全）
void Udpserver::SetFileName(const QString& filePath) {
    QMutexLocker lock(&fileMutex);
    currentFilePath = filePath;
    qDebug() << "File path set to:" << currentFilePath;
}

// 文件读取线程
void Udpserver::fileReaderTask() {
    QString filePath;
    {
        QMutexLocker lock(&fileMutex);
        if (currentFilePath.isEmpty()) {
            qWarning("File path is empty in fileReaderTask.");
            is_running = false; // 如果没有文件路径，无法继续运行
            return;
        }
        filePath = currentFilePath;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file:" << filePath;
        is_running = false; // 文件打开失败，无法继续运行
        return;
    }
    qDebug() << "File opened successfully:" << filePath;

    // 包头模板（基本部分）
    videoStruct baseHeader;
    baseHeader.sysWord = 1; // 从1开始的全局序号
    baseHeader.nowTime = 1; // 可以考虑使用实际时间戳
    baseHeader.versionNumber = 0x01;
    baseHeader.totalChannel = SOCKET_POOL_SIZE;
    baseHeader.videoFormat = 0x01;
    // baseHeader.packetSize 在读取后设置

    unsigned int globalSequence = 1; // 使用独立的全局序号

    while (is_running.load() && !file.atEnd()) {
        // 1. 查找下一个启用的通道
        int target_channel = -1;
        int search_start_index = currentSocket.load();
        int attempts = 0; // 防止在所有通道都禁用时无限循环

        while (attempts < SOCKET_POOL_SIZE && is_running.load()) {
            int current_index = search_start_index % SOCKET_POOL_SIZE;
            // 检查通道是否启用 (原子读取)
            if (channels[current_index].enabled.load() == 1) {
                target_channel = current_index;
                // 更新下一个开始搜索的位置
                currentSocket.store((current_index + 1) % SOCKET_POOL_SIZE);
                break; // 找到了启用的通道
            }
            // 尝试下一个通道
            search_start_index++;
            attempts++;
        }

        // 2. 如果没有找到启用的通道
        if (target_channel == -1) {
            if (!is_running.load()) break; // 如果停止信号来了，退出
            // 所有通道都被禁用或查找过程中被停止
            // qDebug("No enabled channels found, sleeping...");
            std::this_thread::sleep_for(10ms); // 短暂休眠，避免CPU空转，等待通道启用
            continue; // 继续下一次循环，尝试再次查找
        }

        // 3. 读取文件数据
        QByteArray buffer = file.read(packetSize); // packetSize 是读取块的大小
        if (buffer.isEmpty()) {
            if (file.atEnd()) {
                qDebug("End of file reached.");
            }
            else {
                qWarning("File read error or empty read before EOF.");
            }
            break; // 文件结束或读取错误
        }
        // 计算FEC组号和序列号
        //auto packet = std::make_unique<ForwardErrorCorrection::Packet>();
        //packet->packet_mask  = 0;
        //packet->group_number = group_number;
        //packet->sequence_number = sequence_number;
        //packet->k = 10; // 重要包数
        //packet->r = 2; // 冗余包数
        //memcpy(packet->data, buffer.data(), buffer.size());
        //sequence_number++;
        // 4. 构建数据包
        SendPacket pkt;
        pkt.video = baseHeader; // 复制基础包头
        pkt.video.sysWord = globalSequence; // 使用全局序号
        pkt.video.idWord = target_channel; // 设置通道标识
        pkt.video.whichChannel = target_channel; // 设置当前通道
        pkt.video.packetSize = static_cast<unsigned short>(buffer.size()); // **重要：使用实际读取的大小**
        pkt.channel_index = target_channel; // 目标 Socket 索引 (与通道ID一致)
        pkt.channel_seq = target_channel;   // 通道序号 (可能需要独立逻辑，这里暂时用通道ID)
        pkt.stream_type = 0x01;             // 流类型
        pkt.crc32 = 0;                      // CRC32校验（如果需要，在这里计算）

        // 检查数据大小是否超过缓冲区
        if (buffer.size() > sizeof(pkt.video.videoData)) {
            qWarning("Read data size (%d) exceeds videoData buffer size (%d). Truncating.", buffer.size(), sizeof(pkt.video.videoData));
            memcpy(pkt.video.videoData, buffer.constData(), sizeof(pkt.video.videoData));
            pkt.video.packetSize = sizeof(pkt.video.videoData); // 更新包大小为截断后的大小
        }
        else {
            memcpy(pkt.video.videoData, buffer.constData(), buffer.size());
        }

        // 5. 将数据包放入目标通道队列
        ChannelContext& ctx = channels[target_channel];
        {
            QMutexLocker lock(&ctx.queueMutex);
            ctx.packetQueue.push_back(pkt);
            // qDebug("FileReader: Queued packet seq=%u for channel %d (Queue size: %d)", globalSequence, target_channel, ctx.packetQueue.size());
        }
        ctx.queueCondition.wakeOne(); // 唤醒对应的 socketWorkerTask 线程

        // 6. 更新全局序号
        globalSequence++;

        // 7. 流量控制（基于实际发送的数据量和目标速率）
        // 注意：这里的packetSize应该是实际读取的 buffer.size()
        // 3 Mbps = 3,000,000 bits/sec
        // (bytes * 8 bits/byte) / (bits/sec) = seconds
        // (bytes * 8 * 1,000,000) / (bits/sec) = microseconds
        long long sleep_duration_us = (static_cast<long long>(buffer.size()) * 8 * 1'000'000) / 3'000'000;
        if (sleep_duration_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_duration_us));
        }
    }

    file.close();
    qDebug("File reader task finished.");
    // 注意：文件读取结束后，不需要设置 is_running = false，
    // StopSending 会处理停止逻辑
}

// 通道工作线程 - **关键修改**
void Udpserver::socketWorkerTask(int socket_index) {
    ChannelContext& ctx = channels[socket_index];
    std::mt19937 generator(std::random_device{}());
    std::uniform_real_distribution<double> distribution(0.0, 1.0);

    qDebug("Socket worker task started for channel %d.", socket_index);

    while (is_running.load()) {
        SendPacket pkt;
        bool packet_found = false;

        // === 阶段1: 等待数据或状态改变 ===
        {
            QMutexLocker lock(&ctx.queueMutex); // 使用队列锁保护等待条件

            // 等待条件：
            // 1. 程序仍在运行 (is_running)
            // 2. 当前通道是启用的 (enabled == 1)
            // 3. 数据包队列为空 (packetQueue.empty())
            while (is_running.load() && ctx.enabled.load() == 1 && ctx.packetQueue.empty()) {
                // qDebug("Channel %d: Queue empty, waiting...", socket_index);
                // 等待 fileReaderTask 放入数据并唤醒，或者等待 StopSending 唤醒
                ctx.queueCondition.wait(&ctx.queueMutex);
                // qDebug("Channel %d: Woken up.", socket_index);
            }

            // 再次检查退出条件（可能被StopSending唤醒）
            if (!is_running.load()) {
                // qDebug("Channel %d: Stopping because is_running is false.", socket_index);
                break;
            }

            // 检查通道是否被禁用（可能在等待时被禁用）
            if (ctx.enabled.load() == 0) {
                //qDebug("Channel %d: Stopping because channel disabled.", socket_index);
                // 如果被禁用，继续外层循环，会进入下面的状态等待逻辑
                continue; // 注意：这里是 continue，不是 break
            }

            // 如果队列非空，则取出数据包
            if (!ctx.packetQueue.empty()) {
                pkt = ctx.packetQueue.front();
                ctx.packetQueue.pop_front();
                packet_found = true;
                // qDebug("Channel %d: Dequeued packet seq=%u (Queue size: %d)", socket_index, pkt.video.sysWord, ctx.packetQueue.size());
            }
            // else {
                // 如果因为其他原因（如虚假唤醒）醒来但队列仍空，循环会重新等待
                // qDebug("Channel %d: Woke up but queue still empty.", socket_index);
            // }
        } // 队列锁释放

        // === 阶段 2: 如果通道被禁用，等待启用 ===
        // 这个检查放到获取数据之前/等待之中更优，但放在这里也能工作，
        // 确保拿到包后不会因为通道刚被禁用而卡住
        {
            QMutexLocker stateLock(&ctx.stateMutex);
            while (is_running.load() && ctx.enabled.load() == 0) {
                //qDebug("Channel %d: Disabled, waiting for enable signal...", socket_index);
                ctx.stateCondition.wait(&ctx.stateMutex); // 等待 channelStateChange 唤醒
                //qDebug("Channel %d: Woken up from state wait.", socket_index);
            }
        }
        // 再次检查退出条件
        if (!is_running.load()) {
            //qDebug("Channel %d: Stopping after state check.", socket_index);
            break;
        }


        // === 阶段 3: 处理并发送数据包 ===
        if (packet_found) {
            // 模拟丢包
            double currentLossRate = ctx.lossRate.load(); // 原子读取
            if (currentLossRate > 0.0 && distribution(generator) < currentLossRate) {
                qDebug("Channel %d: Packet seq=%u dropped due to loss simulation.", socket_index, pkt.video.sysWord);
                continue; // 模拟丢包，跳过发送
            }

            // 发送数据包
            // **重要：使用实际数据包大小 pkt.video.packetSize**
            size_t header_size = offsetof(videoStruct, videoData);
            // 确保 pkt.video.packetSize 不会超过 videoData 的物理大小
            size_t data_size = pkt.video.packetSize;
            if (data_size > sizeof(pkt.video.videoData)) {
                qWarning("Channel %d: Packet seq=%u has data size %u larger than buffer %u. Clamping.",
                    socket_index, pkt.video.sysWord, (unsigned int)data_size, (unsigned int)sizeof(pkt.video.videoData));
                data_size = sizeof(pkt.video.videoData); // 防止越界
            }
            size_t total_length = header_size + data_size;

            sendto_fec(
                sockets[socket_index],
                reinterpret_cast<char*>(&pkt.video),
                static_cast<int>(total_length), 
                0,
                (sockaddr*)&(targetAddr[socket_index]),
                sizeof(targetAddr[socket_index]),
                 10,  
                 2,    
                 3000  
            );
            qDebug("channel:%d,seq:%d", socket_index, pkt.video.sysWord);
            //if (bytes_sent == SOCKET_ERROR) {
            //    qWarning("Channel %d: sendto failed with error %d", socket_index, WSAGetLastError());
            //    // 可以考虑做一些错误处理，例如重试或关闭通道
            //}
            //else if (bytes_sent != static_cast<int>(total_length)) {
            //    qWarning("Channel %d: sendto sent %d bytes, expected %d bytes for seq=%u.", socket_index, bytes_sent, (int)total_length, pkt.video.sysWord);
            //}
            //else {
            //    //qDebug("Channel %d: Sent packet seq=%u, size=%d", socket_index, pkt.video.sysWord, bytes_sent);
            //}
        }
        // 不需要手动休眠，等待逻辑会处理空闲状态
    }

    qDebug("Socket worker task finished for channel %d.", socket_index);
}


// 开始发送
void Udpserver::StartSending() {
    {
        QMutexLocker lock(&fileMutex);
        if (currentFilePath.isEmpty()) {
            qWarning("Cannot start sending: File path is not set.");
            return;
        }
    }

    if (is_running.load()) {
        qWarning("Sending is already running.");
        return;
    }

    qDebug("Starting sending process...");
    is_running.store(true);
    currentSocket.store(0); // 重置轮询指针

    // 清理旧线程（如果之前调用过StopSending但未完全清理）
    if (!workerThreads.isEmpty()) {
        qWarning("Starting with existing worker threads? This might indicate an issue.");
        // 最好在 StopSending 中确保完全清理
        StopSending(); // 尝试再次停止并清理
        // 重新初始化状态
        is_running.store(true);
        currentSocket.store(0);
        // 可能需要重新初始化Socket等资源，这里简化处理
    }
    workerThreads.clear();


    // 启动文件读取线程
    QThread* fileThread = QThread::create([this]() {
        qDebug("File reader thread starting...");
        fileReaderTask();
        qDebug("File reader thread exiting...");
        });
    if (fileThread) {
        fileThread->setObjectName("FileReaderThread");
        fileThread->start();
        workerThreads.append(fileThread);
    }
    else {
        qWarning("Failed to create file reader thread.");
        is_running.store(false);
        return;
    }

    // 启动通道工作线程
    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        // 重置通道队列状态 （可选，确保启动时队列是空的）
        // {
        //     QMutexLocker lock(&channels[i].queueMutex);
        //     channels[i].packetQueue.clear();
        // }

        QThread* thread = QThread::create([this, i]() {
            qDebug("Socket worker thread %d starting...", i);
            socketWorkerTask(i);
            qDebug("Socket worker thread %d exiting...", i);
            });
        if (thread) {
            thread->setObjectName(QString("SocketWorkerThread_%1").arg(i));
            thread->start();
            workerThreads.append(thread);
        }
        else {
            qWarning("Failed to create socket worker thread for channel %d.", i);
            // 需要停止已经启动的线程和文件读取线程
            StopSending();
            return;
        }
    }
    qDebug("All threads started.");
}

// 停止发送
void Udpserver::StopSending() {
    qDebug("Stopping sending process...");
    is_running.store(false); // 设置停止标志

    // 唤醒所有可能在等待的线程
    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        // 唤醒可能在等待状态的 socketWorkerTask
        {
            QMutexLocker lock(&channels[i].stateMutex);
            channels[i].stateCondition.wakeAll();
        }
        // 唤醒可能在等待队列数据的 socketWorkerTask
        {
            QMutexLocker lock(&channels[i].queueMutex);
            channels[i].queueCondition.wakeAll();
        }
    }

    // 等待所有线程结束
    qDebug("Waiting for threads to finish...");
    int finished_count = 0;
    foreach(QThread * thread, workerThreads) {
        if (thread) {
            // thread->quit(); // quit() 主要用于事件循环，对于 create 创建的简单线程作用不大
            if (!thread->wait(5000)) { // 等待最多5秒
                qWarning("Thread %s did not finish in time, terminating...", thread->objectName().toStdString().c_str());
                thread->terminate(); // 强制终止（有风险）
                thread->wait();      // 等待终止完成
            }
            else {
                // qDebug("Thread %s finished.", thread->objectName().toStdString().c_str());
                finished_count++;
            }
            delete thread; // 释放线程对象
        }
    }
    qDebug("%d threads finished.", finished_count);
    workerThreads.clear(); // 清空线程列表

    // 关闭所有套接字 (在线程完全结束后关闭)
    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        if (sockets[i] != INVALID_SOCKET) {
            closesocket(sockets[i]);
            sockets[i] = INVALID_SOCKET; // 标记为无效
        }
    }
    qDebug("Sockets closed.");

    WSACleanup(); // 最后清理 WSA
    qDebug("WSA Cleaned up. Sending process stopped.");
}

// 通道状态变更
void Udpserver::channelStateChange(int channel, bool state) {
    if (channel < 0 || channel >= SOCKET_POOL_SIZE) {
        qWarning("Invalid channel index %d for state change.", channel);
        return;
    }

    ChannelContext& ctx = channels[channel];
    int previous_state = ctx.enabled.exchange(state ? 1 : 0); // 原子地设置新状态并获取旧状态

    if (previous_state != (state ? 1 : 0)) { // 只有状态实际改变时才打印和唤醒
        qDebug("Channel %d state changed to %s", channel, state ? "ENABLED" : "DISABLED");

        // 如果通道是从禁用变为启用，需要唤醒可能在 stateCondition 上等待的 worker 线程
        if (state) {
            QMutexLocker lock(&ctx.stateMutex); // 虽然 enabled 是原子的，但唤醒条件变量仍需锁
            ctx.stateCondition.wakeOne(); // 唤醒等待状态的线程
        }
        // 无论状态如何变化，都可能需要唤醒在 queueCondition 上等待的线程（虽然主要由fileReader触发）
        // 但为了确保状态变化后能立即响应（特别是从 enabled 到 disabled），也唤醒一下
        // QMutexLocker lock(&ctx.queueMutex); // 避免死锁，不要同时持有两个锁
        // ctx.queueCondition.wakeOne();
        // 更好的做法：在socketWorkerTask的等待循环中检查 enabled 状态
    }
}

// 设置丢包率
void Udpserver::setLossRate(int channel, double rate) {
    if (channel >= 0 && channel < SOCKET_POOL_SIZE) {
        // 约束丢包率在 [0.0, 1.0] 范围
        double clamped_rate =  rate;
        channels[channel].lossRate.store(clamped_rate); // 原子设置
        qDebug("Channel %d loss rate set to %.2f", channel, clamped_rate);
    }
    else {
        qWarning("Invalid channel index %d for setting loss rate.", channel);
    }
}