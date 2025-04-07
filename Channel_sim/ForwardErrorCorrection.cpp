#include "ForwardErrorCorrection.h"
void ForwardErrorCorrection::PacketByFEC(const char* buf, int len, int k, int r) {

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

	// 将数据包储存在列表中
	media_packets.push_back(std::move(packet));
	buffer_packets.push_back(std::make_unique<ForwardErrorCorrection::Packet>(*media_packets.back()));


	// 当数据包列表中有k个数据包时，执行一次编码函数
	if (media_packets.size() == k) {
		EncodeFec(media_packets, r, kNumImportantPackets, kUseUnequalProtection, fec_mask_type, &fec_packets);

		// 将 fec_packets 列表中的包按顺序存放在 buffer_packets 列表中
		for (auto& fec_packet : fec_packets) {
			buffer_packets.push_back(std::make_unique<ForwardErrorCorrection::Packet>(*fec_packet));
		}

		// 更新group_number和sequence_number
		group_number++;
		sequence_number = 0;
	}
}