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

// ���캯�����ʼ��
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

// Socket��ʼ��
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
        ioctlsocket(sockets[i], FIONBIO, &mode); // ������ģʽ
    }
}

// �����ļ�·�����̰߳�ȫ��
void Udpserver::SetFileName(const QString& filePath) {
    QMutexLocker lock(&fileMutex);
    currentFilePath = filePath;
}

// �ļ���ȡ�߳�
void Udpserver::fileReaderTask() {
    QString filePath;
    {
        QMutexLocker lock(&fileMutex);
        filePath = currentFilePath;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;

    // ��ͷģ�壨�����ظ���ʼ����
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

        // ����Ŀ��ͨ������ѯ��
        const int target_channel = currentSocket.load();

        // �������ݰ�
        SendPacket pkt;
        pkt.video = baseHeader; // ���ư�ͷ
        pkt.video.idWord = target_channel;
        pkt.video.whichChannel = target_channel;
        pkt.socketIndex = target_channel;
        memcpy(pkt.video.videoData, buffer.constData(), buffer.size());

        // д��Ŀ��ͨ������
        ChannelContext& ctx = channels[target_channel];
        {
            QMutexLocker lock(&ctx.queueMutex);
            ctx.packetQueue.push_back(pkt);
            ctx.queueCondition.wakeOne(); // ��ȷ���Ѷ�Ӧ�����߳�
        }

        // ������ѯָ��
        currentSocket = (target_channel + 1) % SOCKET_POOL_SIZE;
        baseHeader.sysWord++; // ȫ�����кŵ���

        // �������ƣ�3Mbps��
        std::this_thread::sleep_for(
            std::chrono::microseconds((packetSize * 8 * 1'000'000) / 3'000'000)
        );
    }
    file.close();
}

// ͨ�������߳�
void Udpserver::socketWorkerTask(int socket_index) {
    ChannelContext& ctx = channels[socket_index];
    std::mt19937 generator(std::random_device{}());

    while (is_running) {
        // === �׶�1�����ͨ������״̬ ===
        {
            QMutexLocker stateLock(&ctx.stateMutex);
            while (ctx.enabled == 0 && is_running) {
                ctx.stateCondition.wait(&ctx.stateMutex);
            }
        }
        if (!is_running) break;

        // === �׶�2���������ݰ� ===
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
            // �����ʼ��
            if (ctx.lossRate > 0.0 &&
                std::uniform_real_distribution<double>(0, 1)(generator) < ctx.lossRate) {
                continue; // ģ�ⶪ��
            }

            // �������ݰ�
            size_t header_size = offsetof(videoStruct, videoData);
            size_t total_length = header_size + pkt.video.packetSize;
            sendto_fec(
                sockets[socket_index],
                reinterpret_cast<char*>(&pkt.video),
                total_length,
                0,
                (sockaddr*)&targetAddr,
                sizeof(targetAddr),
                10,   // fec���С
                2,    // fec�����
                3000  // ��ʱ��us��
            );
            qDebug("Channel %d sent seq=%u", socket_index, pkt.video.sysWord);
        }
        //// ���п�ʱ�������߷�ֹCPU��ת
        //else {
        //    std::this_thread::sleep_for(1ms);
        //}
    }
}

// ��������
void Udpserver::StartSending() {
    if (currentFilePath.isEmpty()) return;
    is_running = true;

    // �����ļ���ȡ�߳�
    QThread* fileThread = QThread::create([this]() { fileReaderTask(); });
    fileThread->start();
    workerThreads.append(fileThread);

    // ���������߳�
    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        QThread* thread = QThread::create([this, i]() { socketWorkerTask(i); });
        thread->start();
        workerThreads.append(thread);
    }
}

// ֹͣ����
void Udpserver::StopSending() {
    is_running = false;

    // �������еȴ��߳�
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

    // �ȴ������߳��˳�
    foreach(QThread * thread, workerThreads) {
        thread->quit();
        thread->wait();
    }
    workerThreads.clear();
    WSACleanup();
}

// ͨ��״̬����
void Udpserver::channelStateChange(int channel, bool state) {
    const int idx = channel % SOCKET_POOL_SIZE;
    if (idx < 0 || idx >= SOCKET_POOL_SIZE) return;

    ChannelContext& ctx = channels[idx];
    {
        QMutexLocker lock(&ctx.stateMutex);
        ctx.enabled.store(state ? 1 : 0);
    }
    ctx.stateCondition.wakeOne(); // ��ȷ����״̬�ȴ�
}

// ���ö�����
void Udpserver::setLossRate(int channel, double rate) {
    if (channel >= 0 && channel < SOCKET_POOL_SIZE) {
        channels[channel].lossRate.store(rate);
    }
}