#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define MY_FEC_API _declspec(dllexport)

#include <fstream>
#include <chrono>
#include <thread>
#include <ws2tcpip.h> 
#include "modules/rtp_rtcp/include/simple_client_server.h"
#include "modules/rtp_rtcp/source/forward_error_correction.h" 
#include "modules/rtp_rtcp/source/forward_error_correction_internal.h"
#include "rtc_base/checks.h"
#include "modules/rtp_rtcp/source/fec_private_tables_bursty.h"
#include "modules/rtp_rtcp/source/fec_private_tables_random.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "udp_with_ulpfec.h"

// 修改后的发送函数
void sendto_fec(SOCKET so, const char* buf, int len, int flags, const sockaddr* to, int tolen, int k, int r, int bitrate) {
	static ForwardErrorCorrection fec_instance;
	fec_instance.SendByUlpfec(so, buf, len, flags, to, tolen, k, r, bitrate);
}

// 将Encode函数制作为dll
int Encode_FEC(const ForwardErrorCorrection::PacketList& media_packets,
	UINT32 r,
	int num_important_packets,
	bool use_unequal_protection,
	FecMaskType fec_mask_type,
	std::list<ForwardErrorCorrection::Packet*>* fec_packets) {
	// 调用EncodeFec函数
	static ForwardErrorCorrection fec_instance;
	int ret = fec_instance.EncodeFec(media_packets, r, num_important_packets, use_unequal_protection, fec_mask_type, fec_packets);
	return ret;
}

#pragma pack(1) //按一个字节对齐
struct videoStruct
{
	unsigned int  sysWord; //4字节
	unsigned char idWord; //1字节
	unsigned int  nowTime; //4字节
	unsigned char versionNumber; //1字节
	unsigned char totalChannel; //1字节
	unsigned char whichChannel; //1字节
	unsigned char videoFormat; //1字节
	unsigned short packetSize; //2字节
	unsigned char videoData[10000] = { 0 };
};
#pragma pack() 

int main(int argc, char* argv[])
{
	UINT32		k;					//块中的源符号数量
	UINT32      r;                  //块中的修复符号数量
	UINT32		n;					//块中的编码符号数量（即源 + 修复）
	UINT32		i;
	SOCKET		so = INVALID_SOCKET;	//用于服务器 => 客户端通信的 UDP 套接字
	char* pkt_with_fpi = NULL;			//包含固定大小数据包和仅由 FPI 组成的头的缓冲区
	SOCKADDR_IN	dst_host;
	UINT32		ret = -1;
	std::ifstream tsFile;
	videoStruct m_videodata;
	const int packetsize = SYMBOL_SIZE - 15; // 设置每个数据包大小为SYMBOL_SIZE - 15字节
	std::unique_ptr<char[]> readBuffer(new char[packetsize]); //管理动态分配的内存
	int bitrate = 20000000;
	if (argc == 1)
	{
		//如果 k 值省略，则使用默认值
		k = DEFAULT_K;
	}
	else
	{
		k = atoi(argv[1]);
	}
	// 初始化 FEC 参数
	r = 2;

	// 初始化 UDP 套接字
	if ((so = init_socket(&dst_host)) == INVALID_SOCKET)
	{
		OF_PRINT_ERROR(("Error initializing socket!\n"))
			ret = -1;
		goto end;
	}
	// 打开视频文件
	tsFile.open("C:\\Users\\11299\\Desktop\\output.ts", std::ios::binary);
	if (!tsFile) {
		printf("Cannot open video file!");
		goto end;
	}
	//设置起始值
	m_videodata.sysWord = 1;  //4字节
	m_videodata.idWord = 1;   //1字节
	m_videodata.nowTime = 1;  //4字节
	m_videodata.versionNumber = 1;  //1字节
	m_videodata.totalChannel = 6;  //1字节
	m_videodata.whichChannel = 1;  //1字节
	m_videodata.videoFormat = 1;  //1字节
	m_videodata.packetSize = SYMBOL_SIZE - 15;  //2字节
	// 读取视频文件直到文件结束
	while (tsFile.read(readBuffer.get(), packetsize) || tsFile.gcount() > 0) {
		int bytesToSend = static_cast<int>(tsFile.gcount()); //实际读取的字节数
		m_videodata.packetSize = bytesToSend;
		// 从文件读取数据并填充
		memcpy(m_videodata.videoData, readBuffer.get(), bytesToSend);
		sendto_fec(so, reinterpret_cast<char*>(&m_videodata), sizeof(m_videodata.sysWord) + sizeof(m_videodata.idWord) +
			sizeof(m_videodata.nowTime) + sizeof(m_videodata.versionNumber) + sizeof(m_videodata.totalChannel) +
			sizeof(m_videodata.whichChannel) + sizeof(m_videodata.videoFormat) + sizeof(m_videodata.packetSize) + bytesToSend,
			0, (SOCKADDR*)&dst_host, sizeof(dst_host), k, r, bitrate);
	}

	ret = 1;

end:
	// 清理所有资源...
	if (so != INVALID_SOCKET)
	{
		closesocket(so);
	}
	if (pkt_with_fpi)
	{
		free(pkt_with_fpi);
	}
	// 关闭视频文件
	if (tsFile.is_open())
	{
		tsFile.close();
	}
	return ret;
}


//初始化我们的 UDP 套接字
static SOCKET
init_socket(SOCKADDR_IN* dst_host)
{
	WSADATA wsaData;
	int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);

	if (ret != 0) {
		printf("Failed to load library\n");
		return INVALID_SOCKET;
	}
	else {
		printf("Library loaded successfully\n");
	}

	SOCKET s;

	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET)
	{
		printf("Error: call to socket() failed\n");
		WSACleanup();
		return INVALID_SOCKET;
	}
	dst_host->sin_family = AF_INET;
	dst_host->sin_port = htons((short)DEST_PORT);
	dst_host->sin_addr.s_addr = inet_addr(DEST_IP);
	return s;

}

int ForwardErrorCorrection::EncodeFec(const PacketList& media_packets,
	UINT32 r,
	int num_important_packets,
	bool use_unequal_protection,
	FecMaskType fec_mask_type,
	std::list<Packet*>* fec_packets) {
	const size_t num_media_packets = media_packets.size();

	// 检查参数有效性
	RTC_DCHECK_GT(num_media_packets, 0); // 确保有媒体包
	RTC_DCHECK_GE(num_important_packets, 0); // 确保重要包数量不为负
	RTC_DCHECK_LE(num_important_packets, num_media_packets); // 确保重要包数量不超过总包数
	RTC_DCHECK(fec_packets->empty()); // 确保FEC包列表为空
	RTC_DCHECK_LE(num_media_packets, 16); // 设置最大数据包数量为16

	// 准备生成的FEC包
	int num_fec_packets = r;
	RTC_DCHECK_LE(num_fec_packets, num_media_packets);
	if (num_fec_packets == 0) {
		return 0;
	}

	// FEC包内存申请，清空
	for (int i = 0; i < num_fec_packets; ++i) {
		auto fec_packet = new Packet();
		memset(fec_packet->data, 0, Packet::kMaxDataSize);
		fec_packets->push_back(fec_packet);
	}

	// 创建包掩码表
	int num_media_packets_int = static_cast<int>(num_media_packets);
	PacketMaskTable mask_table(fec_mask_type, num_media_packets_int);
	packet_mask_size_ = PacketMaskSize(num_media_packets);
	memset(packet_masks_, 0, num_fec_packets * packet_mask_size_);
	GeneratePacketMasks(num_media_packets, num_fec_packets,
		num_important_packets, use_unequal_protection,
		&mask_table, packet_masks_);

	// 生成FEC包
	for (int i = 0; i < num_fec_packets; ++i) {
		auto fec_packet = fec_packets->front();
		fec_packets->pop_front();
		int j = 0;
		for (const auto& media_packet : media_packets) {
			if (packet_masks_[i * packet_mask_size_ + j / 8] & (1 << (7 - (j % 8)))) {
				XorPayloads(media_packet->data, fec_packet->data, packet_size);
			}
			j++;
		}
		// 填充FEC包头
		memcpy(&fec_packet->packet_mask, &packet_masks_[(sequence_number - num_media_packets) * packet_mask_size_], packet_mask_size_);
		fec_packet->group_number = group_number;
		fec_packet->sequence_number = sequence_number;
		fec_packet->k = num_media_packets;
		fec_packet->r = num_fec_packets;
		
		// 更新sequence_number
		sequence_number++;

		fec_packets->push_back(fec_packet);
	}

	return num_fec_packets;
}

size_t PacketMaskSize(size_t num_sequence_numbers) {
	RTC_DCHECK_LE(num_sequence_numbers, 8 * kUlpfecPacketMaskSizeLBitSet);
	if (num_sequence_numbers > 8 * kUlpfecPacketMaskSizeLBitClear) {
		return kUlpfecPacketMaskSizeLBitSet;
	}
	return kUlpfecPacketMaskSizeLBitClear;
}
// 生成包掩码
void GeneratePacketMasks(int num_media_packets,
	int num_fec_packets,
	int num_imp_packets,
	bool use_unequal_protection,
	PacketMaskTable* mask_table,
	uint8_t* packet_mask) {
	RTC_DCHECK_GT(num_media_packets, 0);
	RTC_DCHECK_GT(num_fec_packets, 0);
	RTC_DCHECK_LE(num_fec_packets, num_media_packets);
	RTC_DCHECK_LE(num_imp_packets, num_media_packets);
	RTC_DCHECK_GE(num_imp_packets, 0);

	const int num_mask_bytes = PacketMaskSize(num_media_packets); // 计算包掩码所需的字节数

	// 以下是等保护的情况
	if (!use_unequal_protection || num_imp_packets == 0) {
		// 直接从掩码表中获取对应的掩码：用于等保护情况。
		// 掩码 = (k, n-k)，保护因子 = (n-k)/k，
		// 其中 k = 媒体包数量，n = 总包数，(n-k) = FEC包数量。
		rtc::ArrayView<const uint8_t> mask =
			mask_table->LookUp(num_media_packets, num_fec_packets);
		memcpy(packet_mask, &mask[0], mask.size());
	}
}  // 结束 GetPacketMasks

rtc::ArrayView<const uint8_t> PacketMaskTable::LookUp(int num_media_packets,
	int num_fec_packets) {
	RTC_DCHECK_GT(num_media_packets, 0);
	RTC_DCHECK_GT(num_fec_packets, 0);
	RTC_DCHECK_LE(num_media_packets, kUlpfecMaxMediaPackets);
	RTC_DCHECK_LE(num_fec_packets, num_media_packets);
	// 如果媒体包数量小于等于12，直接从预定义的FEC表中查找掩码
	if (num_media_packets <= 12) {
		return LookUpInFecTable(table_, num_media_packets - 1, num_fec_packets - 1);
	}
	// 计算掩码长度
	int mask_length =
		static_cast<int>(PacketMaskSize(static_cast<size_t>(num_media_packets)));

	// 生成FEC码掩码，用于保护媒体包。
	// 在掩码中，每个FEC包占一行，每个位/列代表一个媒体包。
	// 例如，如果行A，列/位B被设置为1，表示FEC包A将保护媒体包B。

	// 遍历每个FEC包。
	for (int row = 0; row < num_fec_packets; row++) {
		// 遍历掩码中的每个码，每个码有8位。
		// 如果媒体包X应由当前FEC包保护，则将位X设置为1。
		// 在此实现中，保护是交错的，因此媒体包X将由FEC包(X % N)保护。
		// 交错保护的方式确保了每个媒体包的保护是分布均匀的
		for (int col = 0; col < mask_length; col++) {
			fec_packet_mask_[row * mask_length + col] =
				((col * 8) % num_fec_packets == row && (col * 8) < num_media_packets
					? 0x80
					: 0x00) |
				((col * 8 + 1) % num_fec_packets == row &&
					(col * 8 + 1) < num_media_packets
					? 0x40
					: 0x00) |
				((col * 8 + 2) % num_fec_packets == row &&
					(col * 8 + 2) < num_media_packets
					? 0x20
					: 0x00) |
				((col * 8 + 3) % num_fec_packets == row &&
					(col * 8 + 3) < num_media_packets
					? 0x10
					: 0x00) |
				((col * 8 + 4) % num_fec_packets == row &&
					(col * 8 + 4) < num_media_packets
					? 0x08
					: 0x00) |
				((col * 8 + 5) % num_fec_packets == row &&
					(col * 8 + 5) < num_media_packets
					? 0x04
					: 0x00) |
				((col * 8 + 6) % num_fec_packets == row &&
					(col * 8 + 6) < num_media_packets
					? 0x02
					: 0x00) |
				((col * 8 + 7) % num_fec_packets == row &&
					(col * 8 + 7) < num_media_packets
					? 0x01
					: 0x00);
		}
	}
	return { &fec_packet_mask_[0],
			static_cast<size_t>(num_fec_packets * mask_length) };
}
rtc::ArrayView<const uint8_t> LookUpInFecTable(const uint8_t* table,
	int media_packet_index,
	int fec_index) {
	RTC_DCHECK_LT(media_packet_index, table[0]);

	// Skip over the table size.
	const uint8_t* entry = &table[1];

	uint8_t entry_size_increment = 2;  // 0-16 are 2 byte wide, then changes to 6.

	// Hop over un-interesting array entries.
	for (int i = 0; i < media_packet_index; ++i) {
		if (i == 16)
			entry_size_increment = 6;
		uint8_t count = entry[0];
		++entry;  // skip over the count.
		for (int j = 0; j < count; ++j) {
			entry += entry_size_increment * (j + 1);  // skip over the data.
		}
	}

	if (media_packet_index == 16)
		entry_size_increment = 6;

	RTC_DCHECK_LT(fec_index, entry[0]);
	++entry;  // Skip over the size.

	// Find the appropriate data in the second dimension.

	// Find the specific data we're looking for.
	for (int i = 0; i < fec_index; ++i)
		entry += entry_size_increment * (i + 1);  // skip over the data.

	size_t size = entry_size_increment * (fec_index + 1);
	return { &entry[0], size };
}

void ForwardErrorCorrection::XorPayloads(const uint8_t* src, uint8_t* dst, size_t length) {
	for (size_t i = 0; i < length; ++i) {
		dst[i] ^= src[i];
	}
}
ForwardErrorCorrection::~ForwardErrorCorrection() = default;

ForwardErrorCorrection::Packet::Packet() {
	packet_mask = 0; // 初始化掩码内容
	group_number = 0; // 初始化组号信息
	sequence_number = 0; // 初始化组内序号信息
	k = 0; // 初始化数据包数量信息
	r = 0; // 初始化冗余包数量信息
	memset(data, 0, kMaxDataSize); // 初始化数据字段内容
}

ForwardErrorCorrection::Packet::~Packet() = default;

PacketMaskTable::PacketMaskTable(FecMaskType fec_mask_type,
	int num_media_packets)
	: table_(PickTable(fec_mask_type, num_media_packets)) {}

PacketMaskTable::~PacketMaskTable() = default;

const uint8_t* PacketMaskTable::PickTable(FecMaskType fec_mask_type,
	int num_media_packets) {
	RTC_DCHECK_GE(num_media_packets, 0);
	RTC_DCHECK_LE(static_cast<size_t>(num_media_packets), kUlpfecMaxMediaPackets);

	if (fec_mask_type != kFecMaskRandom &&
		num_media_packets <=
		static_cast<int>(kPacketMaskBurstyTbl[0])) {
		return &kPacketMaskBurstyTbl[0];
	}

	return &kPacketMaskRandomTbl[0];
}

void ForwardErrorCorrection::SendByUlpfec(SOCKET so, const char* buf, int len, int flags, const sockaddr* to, int tolen, int k, int r, int bitrate) {
	int ret = -1;

	// 若为第一次调用，先记录一下包的大小信息，以便最后一包进行填充
	if (fec_first_use) {
		packet_size = len;
		fec_first_use = false;
	}

	//判断len长度是否符合要求
	if (len < packet_size) {
		memset(const_cast<char*>(buf) + len, 0, packet_size - len);
		//默认最后一包数据长度不足
		fec_last_use = true;
	}

	// 创建 ForwardErrorCorrection::Packet 并填充数据包
	auto packet = std::make_unique<ForwardErrorCorrection::Packet>();
	packet->packet_mask = 0; // 数据包无掩码，直接设置为0
	packet->group_number = group_number;
	packet->sequence_number = sequence_number;
	packet->k = k;
	packet->r = r;
	memcpy(packet->data, buf, packet_size);
	// 更新sequence_number
	sequence_number++;

	// 设置发送缓冲区并填充数据
	char send_buffer[2006]; // 2006 = 2000 + 6
	memcpy(send_buffer, &packet->packet_mask, sizeof(packet->packet_mask));
	memcpy(send_buffer + sizeof(packet->packet_mask), &packet->group_number, sizeof(packet->group_number));
	memcpy(send_buffer + sizeof(packet->packet_mask) + sizeof(packet->group_number), &packet->sequence_number, sizeof(packet->sequence_number));
	memcpy(send_buffer + sizeof(packet->packet_mask) + sizeof(packet->group_number) + sizeof(packet->sequence_number), &packet->k, sizeof(packet->k));
	memcpy(send_buffer + sizeof(packet->packet_mask) + sizeof(packet->group_number) + sizeof(packet->sequence_number) + sizeof(packet->k), &packet->r, sizeof(packet->r));
	memcpy(send_buffer + sizeof(packet->packet_mask) + sizeof(packet->group_number) + sizeof(packet->sequence_number) + sizeof(packet->k) + sizeof(packet->r), packet->data, packet_size);
	// 打印数据包信息
	printf("sending SRC symbol: group_number=%u, sequence_number=%u, packet_size=%u\n", packet->group_number, packet->sequence_number, packet_size);

	// 将数据包储存在列表中
	media_packets.push_back(std::move(packet));

	// 发送数据包
	auto start = std::chrono::high_resolution_clock::now(); //记录当前时间
	if ((ret = sendto(so, send_buffer, packet_size + 6, 0, to, tolen)) == SOCKET_ERROR) {
		OF_PRINT_ERROR(("sendto() failed!\n"))
			ret = -1;
		return;
	}
	auto end = std::chrono::high_resolution_clock::now(); //记录当前时间
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
	double timeTaken = duration.count() / 1000.0; // 实际时间
	double desiredTime = (packet_size + 6) * 8 / static_cast<double>(bitrate); // 理想时间
	total_sent_packets++;
	total_sent_src_packets++;
	double sleepTime = desiredTime - timeTaken;
	if (sleepTime > 0) {
		std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(sleepTime));
	}

	// 当数据包列表中有k个数据包时，执行一次编码函数
	if (media_packets.size() == k) {
		EncodeFec(media_packets, r, kNumImportantPackets, kUseUnequalProtection, fec_mask_type, &fec_packets);

		// 发送冗余包（速率控制）
		for (ForwardErrorCorrection::Packet* fec_packet : fec_packets) {
			// 设置发送缓冲区并填充数据
			char send_buffer[2006]; // 2006 = 2000 + 6
			memcpy(send_buffer, &fec_packet->packet_mask, sizeof(fec_packet->packet_mask));
			memcpy(send_buffer + sizeof(fec_packet->packet_mask), &fec_packet->group_number, sizeof(fec_packet->group_number));
			memcpy(send_buffer + sizeof(fec_packet->packet_mask) + sizeof(fec_packet->group_number), &fec_packet->sequence_number, sizeof(fec_packet->sequence_number));
			memcpy(send_buffer + sizeof(fec_packet->packet_mask) + sizeof(fec_packet->group_number) + sizeof(fec_packet->sequence_number), &fec_packet->k, sizeof(fec_packet->k));
			memcpy(send_buffer + sizeof(fec_packet->packet_mask) + sizeof(fec_packet->group_number) + sizeof(fec_packet->sequence_number) + sizeof(fec_packet->k), &fec_packet->r, sizeof(fec_packet->r));
			memcpy(send_buffer + sizeof(fec_packet->packet_mask) + sizeof(fec_packet->group_number) + sizeof(fec_packet->sequence_number) + sizeof(fec_packet->k) + sizeof(fec_packet->r), fec_packet->data, packet_size);

			// 发送冗余包
			printf("sending FEC symbol: group_number=%u, sequence_number=%u, packet_size=%u\n", fec_packet->group_number, fec_packet->sequence_number, packet_size);
			auto start = std::chrono::high_resolution_clock::now(); //记录当前时间
			if ((ret = sendto(so, send_buffer, packet_size + 6, 0, to, tolen)) == SOCKET_ERROR) {
				OF_PRINT_ERROR(("sendto() failed!\n"))
					ret = -1;
				return;
			}
			auto end = std::chrono::high_resolution_clock::now(); //记录当前时间
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
			double timeTaken = duration.count() / 1000.0; // 实际时间
			double desiredTime = (packet_size + 6) * 8 / static_cast<double>(bitrate); // 理想时间
			total_sent_packets++;
			total_sent_fec_packets++;
			double sleepTime = desiredTime - timeTaken;
			if (sleepTime > 0) {
				std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(sleepTime));
			}
		}
		// 清理资源
		media_packets.clear();
		for (auto fec_packet : fec_packets) {
			delete fec_packet;
		}
		fec_packets.clear();

		// 更新group_number和sequence_number
		group_number++;
		sequence_number = 0;
	}

	// 最后一轮数据发送，打印信息
	if (fec_last_use) {
		printf("\ntotal_sent_packets=%u, total_sent_src_packets=%u, total_sent_fec_packets=%u\n", total_sent_packets, total_sent_src_packets, total_sent_fec_packets);
		// 清理资源
		if (!media_packets.empty()) {
			media_packets.clear();
		}
	}
}

