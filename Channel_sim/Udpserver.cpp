#include "Udpserver.h"
#include <QThread>
#include <QMutexLocker>
#include <QFile>
#include <QByteArray>
#include <chrono>
#include <thread>
#include <random>
#include <ws2tcpip.h>
#include <QDebug>
#include <stdexcept> // 用于 offsetof 检查

#pragma comment(lib, "ws2_32.lib")

using namespace std::chrono_literals; // 为了使用 1ms

// 构造函数初始化
Udpserver::Udpserver(QObject* parent, QString dest_host, int baseport)
    : QObject(parent) {
    memset(&targetAddr, 0, sizeof(targetAddr));
    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        targetAddr[i].sin_family = AF_INET;
        targetAddr[i].sin_port = htons(baseport + i);
        inet_pton(AF_INET, dest_host.toStdString().c_str(), &targetAddr[i].sin_addr);
    }
    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        channels[i].enabled.store(0);
        channels[i].lossRate.store(0.0);
    }
    initializeSockets();
}

Udpserver::~Udpserver() {
    StopSending();
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
            for (int j = 0; j < i; ++j) closesocket(sockets[j]);
            WSACleanup();
            return; 
        }
    }
    qDebug("Sockets initialized.");
}

// 设置文件路径
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
            is_running = false;
            return;
        }
        filePath = currentFilePath;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file:" << filePath;
        is_running = false;
        return;
    }
    qDebug() << "File opened successfully:" << filePath;

 
    char data_buf[ForwardErrorCorrection::Packet::kMaxDataSize]; // 缓冲区用于读取文件和传递给FEC

    while (is_running.load() && !file.atEnd()) {
        // 1. 查找下一个启用的通道 
        int target_channel = -1;
        int search_start_index = currentSocket.load();
        int attempts = 0;
        while (attempts < SOCKET_POOL_SIZE && is_running.load()) {
            int current_index = search_start_index % SOCKET_POOL_SIZE;
            if (channels[current_index].enabled.load() == 1) {
                target_channel = current_index;
                currentSocket.store((current_index + 1) % SOCKET_POOL_SIZE);
                break;
            }
            search_start_index++;
            attempts++;
        }

        // 2. 如果没有找到启用的通道 
        if (target_channel == -1) {
            if (!is_running.load()) break;
            std::this_thread::sleep_for(10ms);
            continue;
        }

        // 3. 读取文件数据块
        qint64 bytesRead = file.read(data_buf, readChunkSize); // 读取数据到缓冲区
        if (bytesRead <= 0) {
            if (file.atEnd()) {
                qDebug("End of file reached.");
            }
            else {
                qWarning("File read error or empty read before EOF. Error: %s", qPrintable(file.errorString()));
            }
            break; // 文件结束或读取错误
        }
        videoStruct flightpkt;
        memcpy(flightpkt.videoData, data_buf, sizeof(data_buf));
        flightpkt.idWord = 1;
        flightpkt.nowTime = 1;
        flightpkt.packetSize = 1009;
        flightpkt.sysWord = sysword;
        flightpkt.totalChannel = 6;
        flightpkt.versionNumber = 1;
        flightpkt.videoFormat = 1;
        flightpkt.whichChannel = 1;
        // 4. 将读取的数据块交给 FEC 处理

        size_t fec_input_size = flightpkt_header_size + bytesRead;

        char fec_input_buffer[2000];
        memcpy(fec_input_buffer, &flightpkt, fec_input_size);

        fec.PacketByFEC(fec_input_buffer, static_cast<int>(fec_input_size), fec_k, fec_r);

        // 5. 检查 FEC 模块是否产出了一组完整的包 (源 + FEC)
        if (!fec.buffer_packets.empty()) {
            ChannelContext& ctx = channels[target_channel]; // 获取目标通道上下文

            // 将 fec.buffer_packets 中的所有包移动到目标通道队列
            while (!fec.buffer_packets.empty()) {
                // 从 fec.buffer_packets 移动出第一个 unique_ptr
                std::unique_ptr<ForwardErrorCorrection::Packet> pkt_ptr = std::move(fec.buffer_packets.front());
                fec.buffer_packets.pop_front();

                // 创建 SendPacket
                SendPacket sendPkt;
                sendPkt.packet_to_send = std::move(pkt_ptr); // 移动 unique_ptr 到 SendPacket
                sendPkt.channel_index = static_cast<uint8_t>(target_channel);
                sendPkt.stream_type = 1; 
                sendPkt.crc32 = 0;       
                sendPkt.seq = 1;
                sendPkt.actual_payload_size = fec_input_size;
                // 将 SendPacket 移动到通道队列
                { // 加锁保护队列操作
                    QMutexLocker lock(&ctx.queueMutex);
                    ctx.packetQueue.push_back(std::move(sendPkt)); // 移动 SendPacket
                }
                ctx.queueCondition.wakeOne(); // 唤醒对应的 socketWorkerTask 线程
                long long sleep_duration_us = (static_cast<long long>(bytesRead) * 8 * 1'000'000) / 3'000'0000;
                if (sleep_duration_us > 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(sleep_duration_us));
                }

            }
            // 此时 fec.buffer_packets 应该是空的
        }

        // 6. 流量控制（基于读取的数据量 bytesRead）
        // 3 Mbps = 3,000,000 bits/sec
        //long long sleep_duration_us = (static_cast<long long>(bytesRead) * 8 * 1'000'000) / 3'000'0000;
        //if (sleep_duration_us > 0) {
        //    std::this_thread::sleep_for(std::chrono::microseconds(sleep_duration_us));
        //}
    }

    // --- 文件读取结束后 ---
    // TODO: 需要调用 FEC 模块的某种机制来处理最后一组可能不足 k 个的包
    // 例如: fec.Flush(); // 假设有这样一个函数
    // Flush 函数内部应该将 fec.media_packets 中剩余的包生成 FEC (如果可能/需要)
    // 并将所有剩余的包（源和可能的FEC）放入 fec.buffer_packets
    // 然后，我们还需要将这些最后的包放入队列：
    // if (!fec.buffer_packets.empty()) {
    //     int last_target_channel = currentSocket.load(); // 或者选择一个默认通道
    //     ChannelContext& ctx = channels[last_target_channel];
    //     while (!fec.buffer_packets.empty()) {
    //         std::unique_ptr<ForwardErrorCorrection::Packet> pkt_ptr = std::move(fec.buffer_packets.front());
    //         fec.buffer_packets.pop_front();
    //         SendPacket sendPkt;
    //         sendPkt.packet_to_send = std::move(pkt_ptr);
    //         sendPkt.channel_index = static_cast<uint8_t>(last_target_channel);
    //         {
    //             QMutexLocker lock(&ctx.queueMutex);
    //             ctx.packetQueue.push_back(std::move(sendPkt));
    //         }
    //         ctx.queueCondition.wakeOne();
    //      }
    // }


    file.close();
    qDebug("File reader task finished.");
    // StopSending 会处理 is_running 和线程清理
}
// 通道工作线程

void Udpserver::socketWorkerTask(int socket_index) {
    ChannelContext& ctx = channels[socket_index];
    std::mt19937 generator(std::random_device{}());
    std::uniform_real_distribution<double> distribution(0.0, 1.0);

    while (is_running.load()) {
        SendPacket sendPkt; // 本地 SendPacket 实例
        bool packet_found = false;

        // === 阶段1: 等待数据或状态改变 === 
        {
            QMutexLocker lock(&ctx.queueMutex);
            while (is_running.load() && ctx.enabled.load() == 1 && ctx.packetQueue.empty()) {
                ctx.queueCondition.wait(&ctx.queueMutex);
            }

            if (!is_running.load()) break;
            if (ctx.enabled.load() == 0) continue; // 如果等待时被禁用，重新检查状态

            if (!ctx.packetQueue.empty()) {
                sendPkt = std::move(ctx.packetQueue.front());  //从队列移动 SendPacket
                ctx.packetQueue.pop_front();
                packet_found = true;
            }
        } // 队列锁释放

        // === 阶段 2: 如果通道被禁用，等待启用 === 
        {
            QMutexLocker stateLock(&ctx.stateMutex);
            while (is_running.load() && ctx.enabled.load() == 0) {
                ctx.stateCondition.wait(&ctx.stateMutex);
            }
        }
        if (!is_running.load()) break;

        // === 阶段 3: 处理并发送数据包 ===
        // 检查从队列取出的 SendPacket 和其包含的 unique_ptr 是否有效
        if (packet_found && sendPkt.packet_to_send && sendPkt.actual_payload_size > 0) {
            // 模拟丢包
            double currentLossRate = ctx.lossRate.load();
            if (currentLossRate > 0.0 && distribution(generator) < currentLossRate) {
                qDebug("Channel %d: Packet group=%u seq=%u dropped due to loss simulation.",
                    socket_index, sendPkt.packet_to_send->group_number, sendPkt.packet_to_send->sequence_number);
                continue;
            }

            // 为 sendPkt 分配全局递增的序列号
            sendPkt.seq = globalSeqCounter.fetch_add(1, std::memory_order_relaxed);

            // 最终确定一下大小
            const size_t payload_size = sendPkt.actual_payload_size; // <-- 从 SendPacket 获取大小

            // 发送缓存
            char send_buffer[2000];

            // 获取裸指针以便访问成员
            ForwardErrorCorrection::Packet* packet_ptr = sendPkt.packet_to_send.get();

            // 序列化
            size_t bytes_serialized = packet_ptr->Serialize(send_buffer, payload_size + fec_header_size, payload_size);

            // 加上协议的其他头信息
            memcpy(send_buffer + payload_size + fec_header_size, &sendPkt.crc32, sizeof(sendPkt.crc32));
            memcpy(send_buffer + payload_size + fec_header_size + sizeof(sendPkt.crc32), &sendPkt.stream_type, sizeof(sendPkt.stream_type));
            memcpy(send_buffer + payload_size + fec_header_size + sizeof(sendPkt.crc32) + sizeof(sendPkt.stream_type), &sendPkt.channel_index, sizeof(sendPkt.channel_index));
            memcpy(send_buffer + payload_size + fec_header_size + sizeof(sendPkt.crc32) + sizeof(sendPkt.stream_type) +sizeof( sendPkt.channel_index), &sendPkt.seq, sizeof(sendPkt.seq));

            // 发送数据
            if (bytes_serialized == payload_size + fec_header_size) {
                int bytes_sent = sendto(
                    sockets[socket_index],
                    send_buffer,
                    static_cast<int>(total_length), // 发送实际总长度
                    0,
                    (sockaddr*)&(targetAddr[socket_index]),
                    sizeof(targetAddr[socket_index])
                );
                qDebug("channel:%d  seq:%d", sendPkt.channel_index, sendPkt.seq);

                if (bytes_sent == SOCKET_ERROR) {
                    qWarning("Channel %d: sendto failed with error %d for group=%u seq=%u",
                        socket_index, WSAGetLastError(), packet_ptr->group_number, packet_ptr->sequence_number);
                    // 可以考虑错误处理
                }
                else if (bytes_sent != static_cast<int>(total_length)) {
                    qWarning("Channel %d: sendto sent %d bytes, expected %d bytes for group=%u seq=%u.",
                        socket_index, bytes_sent, (int)total_length, packet_ptr->group_number, packet_ptr->sequence_number);
                }
                else {
                    // qDebug("Channel %d: Sent packet group=%u seq=%u, size=%d",
                    //        socket_index, packet_ptr->group_number, packet_ptr->sequence_number, bytes_sent);
                }
            }
        } // 当 sendPkt 离开作用域时，其拥有的 packet_to_send (unique_ptr) 会自动 delete Packet 对象
          // 无需手动 delete
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

    // --- 确保清理干净 ---
    if (!workerThreads.isEmpty()) {
        qWarning("StartSending called while threads potentially exist. Forcing stop first.");
        // 先强制停止并等待旧线程结束
        StopSending();
        // 可能需要短暂等待以确保资源完全释放
        QThread::msleep(100);
    }
    workerThreads.clear(); // 再次确认清空

    // --- 清空所有通道队列 ---
    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        {
            QMutexLocker lock(&channels[i].queueMutex);
            channels[i].packetQueue.clear(); // 清空旧数据
        }

    }


    qDebug("Starting sending process...");
    is_running.store(true);
    currentSocket.store(0); // 重置轮询指针


    // 启动文件读取线程
    QThread* fileThread = QThread::create([this]() {
        qDebug("File reader thread starting...");
        fileReaderTask();
        qDebug("File reader thread exiting...");
        });
    if (fileThread) {
        fileThread->setObjectName("FileReaderThread");
        workerThreads.append(fileThread); // 先添加再启动
        fileThread->start();
    }
    else {
        qWarning("Failed to create file reader thread.");
        is_running.store(false);
        return;
    }

    // 启动通道工作线程
    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        QThread* thread = QThread::create([this, i]() {
            qDebug("Socket worker thread %d starting...", i);
            socketWorkerTask(i);
            qDebug("Socket worker thread %d exiting...", i);
            });
        if (thread) {
            thread->setObjectName(QString("SocketWorkerThread_%1").arg(i));
            workerThreads.append(thread); // 先添加再启动
            thread->start();
        }
        else {
            qWarning("Failed to create socket worker thread for channel %d.", i);
            StopSending(); // 停止已启动的线程
            return;
        }
    }
    qDebug("All threads started.");
}

// 停止发送 (逻辑基本不变)
void Udpserver::StopSending() {
    if (!is_running.exchange(false) && workerThreads.isEmpty()) {
        // 如果已经停止且没有线程，直接返回，防止重复清理
        qDebug("StopSending called but already stopped or no threads running.");
        // 确保 WSA Cleanup 只在确实需要时调用
        // WSACleanup(); // 不在这里调用，或者需要计数器
        return;
    }

    qDebug("Stopping sending process...");
    // is_running 已设置为 false

    // 唤醒所有可能在等待的线程
    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        {
            QMutexLocker lock(&channels[i].stateMutex);
            channels[i].stateCondition.wakeAll();
        }
        {
            QMutexLocker lock(&channels[i].queueMutex);
            channels[i].queueCondition.wakeAll();
        }
    }

    // 等待所有线程结束
    qDebug("Waiting for threads to finish...");
    int finished_count = 0;
    bool terminated_any = false;
    // 使用迭代器或基于索引的循环来安全地删除
    for (int i = workerThreads.size() - 1; i >= 0; --i) {
        QThread* thread = workerThreads[i];
        if (thread) {
            if (!thread->wait(5000)) { // 等待最多5秒
                qWarning("Thread %s did not finish in time, terminating...", thread->objectName().toStdString().c_str());
                thread->terminate(); // 强制终止（有风险）
                terminated_any = true;
                thread->wait();      // 等待终止完成
            }
            else {
                finished_count++;
            }
            delete thread;
        }
    }
    workerThreads.clear(); // 清空列表

    if (terminated_any) {
        qWarning("Some threads were terminated forcefully. Potential resource leaks or unstable state.");
    }
    qDebug("%d threads finished gracefully.", finished_count);


    // 关闭所有套接字 (在线程完全结束后关闭)
    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        if (sockets[i] != INVALID_SOCKET) {
            closesocket(sockets[i]);
            sockets[i] = INVALID_SOCKET;
        }
    }
    qDebug("Sockets closed.");

    // WSACleanup 应该在所有 Winsock 操作完成后调用
    // 如果其他地方也用了 WSAStartup，需要确保这是最后一次调用
    WSACleanup();
    qDebug("WSA Cleaned up. Sending process stopped.");
}

// 通道状态变更 (逻辑不变)
void Udpserver::channelStateChange(int channel, bool state) {
    if (channel < 0 || channel >= SOCKET_POOL_SIZE) {
        qWarning("Invalid channel index %d for state change.", channel);
        return;
    }

    ChannelContext& ctx = channels[channel];
    int previous_state = ctx.enabled.exchange(state ? 1 : 0);

    if (previous_state != (state ? 1 : 0)) {
        qDebug("Channel %d state changed to %s", channel, state ? "ENABLED" : "DISABLED");
        if (state) { // 从禁用变为启用，唤醒等待状态的线程
            QMutexLocker lock(&ctx.stateMutex);
            ctx.stateCondition.wakeOne();
        }
        // else: 从启用变为禁用，worker 线程会在循环中检查 enabled 状态并停止或等待
        else {

        }
    }
}

// 设置丢包率 
void Udpserver::setLossRate(int channel, double rate) {
    if (channel >= 0 && channel < SOCKET_POOL_SIZE) {
        double clamped_rate = std::max(0.0, std::min(1.0, rate)); 
        channels[channel].lossRate.store(clamped_rate);
        qDebug("Channel %d loss rate set to %.2f", channel, clamped_rate);
    }
    else {
        qWarning("Invalid channel index %d for setting loss rate.", channel);
    }
}