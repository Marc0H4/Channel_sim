#include "ForwardErrorCorrection.h"
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