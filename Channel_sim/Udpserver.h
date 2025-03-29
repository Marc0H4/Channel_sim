#pragma once
#define SOCKET_POOL_SIZE 3
#include <QObject>
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <fstream>
#include <stdio.h>
#include <winsock2.h>
#include <winsock.h>
#include <iostream>
#include <tchar.h>
#include <time.h>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include <string>
#include "udp_with_ulpfec.h"
#include "simple_client_server.h"
#include <QMutex>
#include <QWaitCondition>
#include <QThread>
#include <queue>
#include <deque>
#include <QFile>
#include <Qqueue>
#include <Ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#pragma pack(1) //��һ���ֽڶ���
struct videoStruct
{
    unsigned int  sysWord; //4�ֽ�
    unsigned char idWord; //1�ֽ�
    unsigned int  nowTime; //4�ֽ�
    unsigned char versionNumber; //1�ֽ�
    unsigned char totalChannel; //1�ֽ�
    unsigned char whichChannel; //1�ֽ�
    unsigned char videoFormat; //1�ֽ�
    unsigned short packetSize; //2�ֽ�
    unsigned char videoData[10000] = { 0 };
};
#pragma pack() 
struct SendPacket {
  
    struct videoStruct video;
    int socketIndex;
};
class Udpserver : public QObject
{
	Q_OBJECT
public:
	Udpserver(QObject* parent, QString desthost, int Port, int channel);
	~Udpserver();
    //file
    QString currentFilePath;
    bool is_running = false;
    void SetFileName(const QString& filePath);
    const int packetSize = 1009;

    void StartSending();


private:

    //net
    SOCKET sockets[SOCKET_POOL_SIZE];
    SOCKADDR_IN targetAddr;
    //thread
    std::deque<SendPacket> packetQueue;
    QMutex queueMutex;
    QWaitCondition queueCondition;
    QList<QThread*> workerThreads;
    int currentSocket = 0;

	SendPacket packet{};
	SendPacket packet_buffer{};
	void StopSending();
    void initializeSockets();
    void fileReaderTask();
    void socketWorkerTask(int socket_index);
};

    