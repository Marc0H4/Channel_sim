#include "Udpserver.h"
#include <QThread>
#include <QMutexLocker>
#include <QFile>
#include <QByteArray>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstring>
#include <ws2tcpip.h>

using namespace std;

Udpserver::Udpserver(QObject* parent, QString dest_host, int Port, int channel)
    : QObject(parent), is_running(false), currentFilePath("")
{
    // 初始化目标地址
    memset(&targetAddr, 0, sizeof(targetAddr));
    targetAddr.sin_family = AF_INET;
    targetAddr.sin_port = htons(DEST_PORT);
    inet_pton(AF_INET, dest_host.toStdString().c_str(), &targetAddr.sin_addr);

    // 初始化网络
    initializeSockets();
}

Udpserver::~Udpserver()
{
    StopSending();
    WSACleanup();
}

void Udpserver::initializeSockets() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        // emit errorOccurred("Winsock初始化失败");
        return;
    }

    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        sockets[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sockets[i] == INVALID_SOCKET) {
            // emit errorOccurred(QString("创建Socket%1失败").arg(i));
            closesocket(sockets[i]);
            return;
        }

        // 设置非阻塞模式
        u_long mode = 1;
        ioctlsocket(sockets[i], FIONBIO, &mode);
    }
}

void Udpserver::SetFileName(const QString& filePath)
{
    QMutexLocker lock(&queueMutex);
    currentFilePath = filePath;
}

void Udpserver::fileReaderTask() {
    //感觉都不需要上锁
    QString filePath;
    {
        QMutexLocker lock(&queueMutex);
        filePath = currentFilePath;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        // emit errorOccurred("无法打开文件: " + file.errorString());
        return;
    }

    //SendPacket packet{
    //videoStruct {
    //    0x01,
    //    static_cast<unsigned char>(currentSocket),
    //    1,
    //    0x01,
    //    0x06,
    //    static_cast<unsigned char>(currentSocket),
    //    0x01,
    //    1009,
    //    {0}
    //},
    //currentSocket
    //};
	packet.video.sysWord = 0x01;
	packet.video.nowTime = 1;
	packet.video.versionNumber = 0x01;
	packet.video.totalChannel = 0x06;
	packet.video.videoFormat = 0x01;
	packet.video.packetSize = 1009;
	// 读取文件
    while (is_running && !file.atEnd()) {
        QByteArray buffer = file.read(packetSize);
        if (buffer.isEmpty()) break;

        packet.video.idWord = static_cast<unsigned char>(currentSocket);
        packet.video.whichChannel = static_cast<unsigned char>(currentSocket);
        packet.socketIndex = currentSocket;

        memcpy(packet.video.videoData, buffer.constData(), buffer.size());

        {
            QMutexLocker lock(&queueMutex);
            packetQueue.push_back(packet);
            queueCondition.wakeAll();
        }

        // 轮询选择Socket
        currentSocket = (currentSocket + 1) % SOCKET_POOL_SIZE;
        packet.video.sysWord++;
        // 流量控制（3Mbps）
        std::this_thread::sleep_for(
            std::chrono::microseconds((packetSize * 8 * 1000000) / 3000000)
        );
    }

    file.close();
}

void Udpserver::socketWorkerTask(int socket_index) {
    while (is_running) {
        bool packetFound = false;

        
            QMutexLocker lock(&queueMutex);
            while (packetQueue.empty() && is_running) {
                queueCondition.wait(&queueMutex);
            }

            if (!is_running) break;

            for (auto it = packetQueue.begin(); it != packetQueue.end(); ++it) {
                if (it->socketIndex == socket_index) {
                    packet_buffer = *it;
                    packetQueue.erase(it);
                    packetFound = true;
                    break;
                }
            }
        

        if (packetFound && packet_buffer.video.videoData[0] != 0) {
            sendto_fec(
                sockets[socket_index],      // socket
                reinterpret_cast<char*>(&packet_buffer.video),  // buf
                sizeof(packet_buffer.video.sysWord) + sizeof(packet_buffer.video.idWord) +
                sizeof(packet_buffer.video.nowTime) + sizeof(packet_buffer.video.versionNumber) + sizeof(packet_buffer.video.totalChannel) +
                sizeof(packet_buffer.video.whichChannel) + sizeof(packet_buffer.video.videoFormat) + sizeof(packet_buffer.video.packetSize) + packetSize,              // length
                0,                                 // flags
                (SOCKADDR*)&targetAddr,            // addr 
                sizeof(targetAddr),
                10,
                2,
                3000000
            );
			qDebug("send packet:%d", packet_buffer.video.sysWord);
			//qDebug("size:%d", sizeof(packet_buffer.video.sysWord) + sizeof(packet_buffer.video.idWord) +
   //             sizeof(packet_buffer.video.nowTime) + sizeof(packet_buffer.video.versionNumber) + sizeof(packet_buffer.video.totalChannel) +
   //             sizeof(packet_buffer.video.whichChannel) + sizeof(packet_buffer.video.videoFormat) + sizeof(packet_buffer.video.packetSize) + packetSize);
		    //	qDebug("channel:%d", packet_buffer.video.whichChannel);
        }
    }
}


void Udpserver::StartSending() {
    if (currentFilePath.isEmpty()) {
        // emit errorOccurred("未设置文件路径");
        return;
    }

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
 

void Udpserver::StopSending() {
    is_running = false;

    queueCondition.wakeAll();

    for (int i = 0; i < SOCKET_POOL_SIZE; ++i) {
        if (sockets[i] != INVALID_SOCKET) {
            closesocket(sockets[i]);
        }
    }
    foreach(QThread * thread, workerThreads) {
        thread->quit();
        thread->wait();
    }
    workerThreads.clear();

    WSACleanup();
}