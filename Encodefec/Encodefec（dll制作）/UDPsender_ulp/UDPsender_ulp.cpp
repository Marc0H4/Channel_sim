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

// �޸ĺ�ķ��ͺ���
void sendto_fec(SOCKET so, const char* buf, int len, int flags, const sockaddr* to, int tolen, int k, int r, int bitrate) {
	static ForwardErrorCorrection fec_instance;
	fec_instance.SendByUlpfec(so, buf, len, flags, to, tolen, k, r, bitrate);
}

// ��Encode��������Ϊdll
int Encode_FEC(const ForwardErrorCorrection::PacketList& media_packets,
	UINT32 r,
	int num_important_packets,
	bool use_unequal_protection,
	FecMaskType fec_mask_type,
	std::list<ForwardErrorCorrection::Packet*>* fec_packets) {
	// ����EncodeFec����
	static ForwardErrorCorrection fec_instance;
	int ret = fec_instance.EncodeFec(media_packets, r, num_important_packets, use_unequal_protection, fec_mask_type, fec_packets);
	return ret;
}

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

int main(int argc, char* argv[])
{
	UINT32		k;					//���е�Դ��������
	UINT32      r;                  //���е��޸���������
	UINT32		n;					//���еı��������������Դ + �޸���
	UINT32		i;
	SOCKET		so = INVALID_SOCKET;	//���ڷ����� => �ͻ���ͨ�ŵ� UDP �׽���
	char* pkt_with_fpi = NULL;			//�����̶���С���ݰ��ͽ��� FPI ��ɵ�ͷ�Ļ�����
	SOCKADDR_IN	dst_host;
	UINT32		ret = -1;
	std::ifstream tsFile;
	videoStruct m_videodata;
	const int packetsize = SYMBOL_SIZE - 15; // ����ÿ�����ݰ���СΪSYMBOL_SIZE - 15�ֽ�
	std::unique_ptr<char[]> readBuffer(new char[packetsize]); //����̬������ڴ�
	int bitrate = 20000000;
	if (argc == 1)
	{
		//��� k ֵʡ�ԣ���ʹ��Ĭ��ֵ
		k = DEFAULT_K;
	}
	else
	{
		k = atoi(argv[1]);
	}
	// ��ʼ�� FEC ����
	r = 2;

	// ��ʼ�� UDP �׽���
	if ((so = init_socket(&dst_host)) == INVALID_SOCKET)
	{
		OF_PRINT_ERROR(("Error initializing socket!\n"))
			ret = -1;
		goto end;
	}
	// ����Ƶ�ļ�
	tsFile.open("C:\\Users\\11299\\Desktop\\output.ts", std::ios::binary);
	if (!tsFile) {
		printf("Cannot open video file!");
		goto end;
	}
	//������ʼֵ
	m_videodata.sysWord = 1;  //4�ֽ�
	m_videodata.idWord = 1;   //1�ֽ�
	m_videodata.nowTime = 1;  //4�ֽ�
	m_videodata.versionNumber = 1;  //1�ֽ�
	m_videodata.totalChannel = 6;  //1�ֽ�
	m_videodata.whichChannel = 1;  //1�ֽ�
	m_videodata.videoFormat = 1;  //1�ֽ�
	m_videodata.packetSize = SYMBOL_SIZE - 15;  //2�ֽ�
	// ��ȡ��Ƶ�ļ�ֱ���ļ�����
	while (tsFile.read(readBuffer.get(), packetsize) || tsFile.gcount() > 0) {
		int bytesToSend = static_cast<int>(tsFile.gcount()); //ʵ�ʶ�ȡ���ֽ���
		m_videodata.packetSize = bytesToSend;
		// ���ļ���ȡ���ݲ����
		memcpy(m_videodata.videoData, readBuffer.get(), bytesToSend);
		sendto_fec(so, reinterpret_cast<char*>(&m_videodata), sizeof(m_videodata.sysWord) + sizeof(m_videodata.idWord) +
			sizeof(m_videodata.nowTime) + sizeof(m_videodata.versionNumber) + sizeof(m_videodata.totalChannel) +
			sizeof(m_videodata.whichChannel) + sizeof(m_videodata.videoFormat) + sizeof(m_videodata.packetSize) + bytesToSend,
			0, (SOCKADDR*)&dst_host, sizeof(dst_host), k, r, bitrate);
	}

	ret = 1;

end:
	// ����������Դ...
	if (so != INVALID_SOCKET)
	{
		closesocket(so);
	}
	if (pkt_with_fpi)
	{
		free(pkt_with_fpi);
	}
	// �ر���Ƶ�ļ�
	if (tsFile.is_open())
	{
		tsFile.close();
	}
	return ret;
}


//��ʼ�����ǵ� UDP �׽���
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

	// ��������Ч��
	RTC_DCHECK_GT(num_media_packets, 0); // ȷ����ý���
	RTC_DCHECK_GE(num_important_packets, 0); // ȷ����Ҫ��������Ϊ��
	RTC_DCHECK_LE(num_important_packets, num_media_packets); // ȷ����Ҫ�������������ܰ���
	RTC_DCHECK(fec_packets->empty()); // ȷ��FEC���б�Ϊ��
	RTC_DCHECK_LE(num_media_packets, 16); // ����������ݰ�����Ϊ16

	// ׼�����ɵ�FEC��
	int num_fec_packets = r;
	RTC_DCHECK_LE(num_fec_packets, num_media_packets);
	if (num_fec_packets == 0) {
		return 0;
	}

	// FEC���ڴ����룬���
	for (int i = 0; i < num_fec_packets; ++i) {
		auto fec_packet = new Packet();
		memset(fec_packet->data, 0, Packet::kMaxDataSize);
		fec_packets->push_back(fec_packet);
	}

	// �����������
	int num_media_packets_int = static_cast<int>(num_media_packets);
	PacketMaskTable mask_table(fec_mask_type, num_media_packets_int);
	packet_mask_size_ = PacketMaskSize(num_media_packets);
	memset(packet_masks_, 0, num_fec_packets * packet_mask_size_);
	GeneratePacketMasks(num_media_packets, num_fec_packets,
		num_important_packets, use_unequal_protection,
		&mask_table, packet_masks_);

	// ����FEC��
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
		// ���FEC��ͷ
		memcpy(&fec_packet->packet_mask, &packet_masks_[(sequence_number - num_media_packets) * packet_mask_size_], packet_mask_size_);
		fec_packet->group_number = group_number;
		fec_packet->sequence_number = sequence_number;
		fec_packet->k = num_media_packets;
		fec_packet->r = num_fec_packets;
		
		// ����sequence_number
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
// ���ɰ�����
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

	const int num_mask_bytes = PacketMaskSize(num_media_packets); // ���������������ֽ���

	// �����ǵȱ��������
	if (!use_unequal_protection || num_imp_packets == 0) {
		// ֱ�Ӵ�������л�ȡ��Ӧ�����룺���ڵȱ��������
		// ���� = (k, n-k)���������� = (n-k)/k��
		// ���� k = ý���������n = �ܰ�����(n-k) = FEC��������
		rtc::ArrayView<const uint8_t> mask =
			mask_table->LookUp(num_media_packets, num_fec_packets);
		memcpy(packet_mask, &mask[0], mask.size());
	}
}  // ���� GetPacketMasks

rtc::ArrayView<const uint8_t> PacketMaskTable::LookUp(int num_media_packets,
	int num_fec_packets) {
	RTC_DCHECK_GT(num_media_packets, 0);
	RTC_DCHECK_GT(num_fec_packets, 0);
	RTC_DCHECK_LE(num_media_packets, kUlpfecMaxMediaPackets);
	RTC_DCHECK_LE(num_fec_packets, num_media_packets);
	// ���ý�������С�ڵ���12��ֱ�Ӵ�Ԥ�����FEC���в�������
	if (num_media_packets <= 12) {
		return LookUpInFecTable(table_, num_media_packets - 1, num_fec_packets - 1);
	}
	// �������볤��
	int mask_length =
		static_cast<int>(PacketMaskSize(static_cast<size_t>(num_media_packets)));

	// ����FEC�����룬���ڱ���ý�����
	// �������У�ÿ��FEC��ռһ�У�ÿ��λ/�д���һ��ý�����
	// ���磬�����A����/λB������Ϊ1����ʾFEC��A������ý���B��

	// ����ÿ��FEC����
	for (int row = 0; row < num_fec_packets; row++) {
		// ���������е�ÿ���룬ÿ������8λ��
		// ���ý���XӦ�ɵ�ǰFEC����������λX����Ϊ1��
		// �ڴ�ʵ���У������ǽ���ģ����ý���X����FEC��(X % N)������
		// �������ķ�ʽȷ����ÿ��ý����ı����Ƿֲ����ȵ�
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
	packet_mask = 0; // ��ʼ����������
	group_number = 0; // ��ʼ�������Ϣ
	sequence_number = 0; // ��ʼ�����������Ϣ
	k = 0; // ��ʼ�����ݰ�������Ϣ
	r = 0; // ��ʼ�������������Ϣ
	memset(data, 0, kMaxDataSize); // ��ʼ�������ֶ�����
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

	// ��Ϊ��һ�ε��ã��ȼ�¼һ�°��Ĵ�С��Ϣ���Ա����һ���������
	if (fec_first_use) {
		packet_size = len;
		fec_first_use = false;
	}

	//�ж�len�����Ƿ����Ҫ��
	if (len < packet_size) {
		memset(const_cast<char*>(buf) + len, 0, packet_size - len);
		//Ĭ�����һ�����ݳ��Ȳ���
		fec_last_use = true;
	}

	// ���� ForwardErrorCorrection::Packet ��������ݰ�
	auto packet = std::make_unique<ForwardErrorCorrection::Packet>();
	packet->packet_mask = 0; // ���ݰ������룬ֱ������Ϊ0
	packet->group_number = group_number;
	packet->sequence_number = sequence_number;
	packet->k = k;
	packet->r = r;
	memcpy(packet->data, buf, packet_size);
	// ����sequence_number
	sequence_number++;

	// ���÷��ͻ��������������
	char send_buffer[2006]; // 2006 = 2000 + 6
	memcpy(send_buffer, &packet->packet_mask, sizeof(packet->packet_mask));
	memcpy(send_buffer + sizeof(packet->packet_mask), &packet->group_number, sizeof(packet->group_number));
	memcpy(send_buffer + sizeof(packet->packet_mask) + sizeof(packet->group_number), &packet->sequence_number, sizeof(packet->sequence_number));
	memcpy(send_buffer + sizeof(packet->packet_mask) + sizeof(packet->group_number) + sizeof(packet->sequence_number), &packet->k, sizeof(packet->k));
	memcpy(send_buffer + sizeof(packet->packet_mask) + sizeof(packet->group_number) + sizeof(packet->sequence_number) + sizeof(packet->k), &packet->r, sizeof(packet->r));
	memcpy(send_buffer + sizeof(packet->packet_mask) + sizeof(packet->group_number) + sizeof(packet->sequence_number) + sizeof(packet->k) + sizeof(packet->r), packet->data, packet_size);
	// ��ӡ���ݰ���Ϣ
	printf("sending SRC symbol: group_number=%u, sequence_number=%u, packet_size=%u\n", packet->group_number, packet->sequence_number, packet_size);

	// �����ݰ��������б���
	media_packets.push_back(std::move(packet));

	// �������ݰ�
	auto start = std::chrono::high_resolution_clock::now(); //��¼��ǰʱ��
	if ((ret = sendto(so, send_buffer, packet_size + 6, 0, to, tolen)) == SOCKET_ERROR) {
		OF_PRINT_ERROR(("sendto() failed!\n"))
			ret = -1;
		return;
	}
	auto end = std::chrono::high_resolution_clock::now(); //��¼��ǰʱ��
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
	double timeTaken = duration.count() / 1000.0; // ʵ��ʱ��
	double desiredTime = (packet_size + 6) * 8 / static_cast<double>(bitrate); // ����ʱ��
	total_sent_packets++;
	total_sent_src_packets++;
	double sleepTime = desiredTime - timeTaken;
	if (sleepTime > 0) {
		std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(sleepTime));
	}

	// �����ݰ��б�����k�����ݰ�ʱ��ִ��һ�α��뺯��
	if (media_packets.size() == k) {
		EncodeFec(media_packets, r, kNumImportantPackets, kUseUnequalProtection, fec_mask_type, &fec_packets);

		// ��������������ʿ��ƣ�
		for (ForwardErrorCorrection::Packet* fec_packet : fec_packets) {
			// ���÷��ͻ��������������
			char send_buffer[2006]; // 2006 = 2000 + 6
			memcpy(send_buffer, &fec_packet->packet_mask, sizeof(fec_packet->packet_mask));
			memcpy(send_buffer + sizeof(fec_packet->packet_mask), &fec_packet->group_number, sizeof(fec_packet->group_number));
			memcpy(send_buffer + sizeof(fec_packet->packet_mask) + sizeof(fec_packet->group_number), &fec_packet->sequence_number, sizeof(fec_packet->sequence_number));
			memcpy(send_buffer + sizeof(fec_packet->packet_mask) + sizeof(fec_packet->group_number) + sizeof(fec_packet->sequence_number), &fec_packet->k, sizeof(fec_packet->k));
			memcpy(send_buffer + sizeof(fec_packet->packet_mask) + sizeof(fec_packet->group_number) + sizeof(fec_packet->sequence_number) + sizeof(fec_packet->k), &fec_packet->r, sizeof(fec_packet->r));
			memcpy(send_buffer + sizeof(fec_packet->packet_mask) + sizeof(fec_packet->group_number) + sizeof(fec_packet->sequence_number) + sizeof(fec_packet->k) + sizeof(fec_packet->r), fec_packet->data, packet_size);

			// ���������
			printf("sending FEC symbol: group_number=%u, sequence_number=%u, packet_size=%u\n", fec_packet->group_number, fec_packet->sequence_number, packet_size);
			auto start = std::chrono::high_resolution_clock::now(); //��¼��ǰʱ��
			if ((ret = sendto(so, send_buffer, packet_size + 6, 0, to, tolen)) == SOCKET_ERROR) {
				OF_PRINT_ERROR(("sendto() failed!\n"))
					ret = -1;
				return;
			}
			auto end = std::chrono::high_resolution_clock::now(); //��¼��ǰʱ��
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
			double timeTaken = duration.count() / 1000.0; // ʵ��ʱ��
			double desiredTime = (packet_size + 6) * 8 / static_cast<double>(bitrate); // ����ʱ��
			total_sent_packets++;
			total_sent_fec_packets++;
			double sleepTime = desiredTime - timeTaken;
			if (sleepTime > 0) {
				std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(sleepTime));
			}
		}
		// ������Դ
		media_packets.clear();
		for (auto fec_packet : fec_packets) {
			delete fec_packet;
		}
		fec_packets.clear();

		// ����group_number��sequence_number
		group_number++;
		sequence_number = 0;
	}

	// ���һ�����ݷ��ͣ���ӡ��Ϣ
	if (fec_last_use) {
		printf("\ntotal_sent_packets=%u, total_sent_src_packets=%u, total_sent_fec_packets=%u\n", total_sent_packets, total_sent_src_packets, total_sent_fec_packets);
		// ������Դ
		if (!media_packets.empty()) {
			media_packets.clear();
		}
	}
}

