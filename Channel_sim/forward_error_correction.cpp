#include <thread>
#include "modules/rtp_rtcp/include/simple_client_server.h"
#include "modules/rtp_rtcp/source/forward_error_correction.h" 
#include "modules/rtp_rtcp/source/fec_private_tables_bursty.h"
#include "modules/rtp_rtcp/source/fec_private_tables_random.h"

ForwardErrorCorrection::Packet::Packet() {
	packet_mask = 0; // ��ʼ����������
	group_number = 0; // ��ʼ�������Ϣ
	sequence_number = 0; // ��ʼ�����������Ϣ
	k = 0; // ��ʼ�����ݰ�������Ϣ
	r = 0; // ��ʼ�������������Ϣ
	memset(data, 0, kMaxDataSize); // ��ʼ�������ֶ�����
}

ForwardErrorCorrection::Packet::~Packet() = default;

ForwardErrorCorrection::~ForwardErrorCorrection() = default;

PacketMaskTable::PacketMaskTable(FecMaskType fec_mask_type,
	int num_media_packets)
	: table_(PickTable(fec_mask_type, num_media_packets)) {
}

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

void ForwardErrorCorrection::XorPayloads(const uint8_t* src, uint8_t* dst, size_t length) {
	for (size_t i = 0; i < length; ++i) {
		dst[i] ^= src[i];
	}
}

size_t PacketMaskSize(size_t num_sequence_numbers) {
	RTC_DCHECK_LE(num_sequence_numbers, 8 * kUlpfecPacketMaskSizeLBitSet);
	if (num_sequence_numbers > 8 * kUlpfecPacketMaskSizeLBitClear) {
		return kUlpfecPacketMaskSizeLBitSet;
	}
	return kUlpfecPacketMaskSizeLBitClear;
}

size_t ForwardErrorCorrection::Packet::Serialize(char* buffer, size_t buffer_size, size_t payload_size_to_write) const {
	// Define the size of the fixed header part
	const size_t header_size = sizeof(this->packet_mask) +
		sizeof(this->group_number) +
		sizeof(this->sequence_number) +
		sizeof(this->k) +
		sizeof(this->r); // Should be 2 + 1 + 1 + 1 + 1 = 6 bytes

	// Calculate the total required size for serialization
	const size_t required_total_size = header_size + payload_size_to_write;

	// Check if the provided buffer is large enough
	if (buffer_size < required_total_size) {
		// fprintf(stderr, "Error: Buffer too small for serialization. Needed %zu, got %zu\n", required_total_size, buffer_size);
		return 0; // Indicate error (buffer too small)
	}

	// Check if the requested payload size exceeds the internal data capacity
	if (payload_size_to_write > sizeof(this->data)) {
		// fprintf(stderr, "Error: Requested payload size %zu exceeds internal data capacity %zu\n", payload_size_to_write, sizeof(this->data));
		return 0; // Indicate error (invalid payload size request)
	}

	// Proceed with serialization
	char* current_ptr = buffer;

	// Copy header fields sequentially
	memcpy(current_ptr, &this->packet_mask, sizeof(this->packet_mask));
	current_ptr += sizeof(this->packet_mask);

	memcpy(current_ptr, &this->group_number, sizeof(this->group_number));
	current_ptr += sizeof(this->group_number);

	memcpy(current_ptr, &this->sequence_number, sizeof(this->sequence_number));
	current_ptr += sizeof(this->sequence_number);

	memcpy(current_ptr, &this->k, sizeof(this->k));
	current_ptr += sizeof(this->k);

	memcpy(current_ptr, &this->r, sizeof(this->r));
	current_ptr += sizeof(this->r);

	// Copy the specified amount of payload data
	memcpy(current_ptr, this->data, payload_size_to_write);

	// Return the total number of bytes written
	return required_total_size;
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

void ForwardErrorCorrection::PacketByFEC(const char* buf, int len, int k, int r) {

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

	// �����ݰ��������б���
	media_packets.push_back(std::move(packet));
	buffer_packets.push_back(std::make_unique<ForwardErrorCorrection::Packet>(*media_packets.back()));


	// �����ݰ��б�����k�����ݰ�ʱ��ִ��һ�α��뺯��
	if (media_packets.size() == k) {
		EncodeFec(media_packets, r, kNumImportantPackets, kUseUnequalProtection, fec_mask_type, &fec_packets);

		// �� fec_packets �б��еİ���˳������ buffer_packets �б���
		for (auto& fec_packet : fec_packets) {
			buffer_packets.push_back(std::make_unique<ForwardErrorCorrection::Packet>(*fec_packet));
		}

		// ����group_number��sequence_number
		group_number++;
		sequence_number = 0;
	}
}