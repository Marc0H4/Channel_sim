/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/ulpfec_receiver.h"

#include <memory>
#include <utility>

#include "api/scoped_refptr.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"
#include "system_wrappers/include/metrics.h"

namespace webrtc {
// UlpfecReceiver类的构造函数
UlpfecReceiver::UlpfecReceiver(uint32_t ssrc,
                               int ulpfec_payload_type,
                               RecoveredPacketReceiver* callback,
                               Clock* clock)
    : ssrc_(ssrc),
      ulpfec_payload_type_(ulpfec_payload_type),
      clock_(clock),
      recovered_packet_callback_(callback),
      fec_(ForwardErrorCorrection::CreateUlpfec(ssrc_)) {
  // 确保ulpfec_payload_type_的值合法
  RTC_DCHECK_GE(ulpfec_payload_type_, -1);
}
// UlpfecReceiver类的析构函数
UlpfecReceiver::~UlpfecReceiver() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  // 如果有接收到的包，记录统计信息
  if (packet_counter_.first_packet_time != Timestamp::MinusInfinity()) {
    const Timestamp now = clock_->CurrentTime();
    TimeDelta elapsed = (now - packet_counter_.first_packet_time);
    if (elapsed.seconds() >= metrics::kMinRunTimeInSeconds) {
      if (packet_counter_.num_packets > 0) {
        RTC_HISTOGRAM_PERCENTAGE(
            "WebRTC.Video.ReceivedFecPacketsInPercent",
            static_cast<int>(packet_counter_.num_fec_packets * 100 /
                             packet_counter_.num_packets)); //计算并记录接收到的FEC包占所有接收包的百分比
      }
      if (packet_counter_.num_fec_packets > 0) {
        RTC_HISTOGRAM_PERCENTAGE(
            "WebRTC.Video.RecoveredMediaPacketsInPercentOfFec",
            static_cast<int>(packet_counter_.num_recovered_packets * 100 /
                             packet_counter_.num_fec_packets)); //计算并记录恢复的媒体包占FEC包的百分比
      }
      if (ulpfec_payload_type_ != -1) {
        RTC_HISTOGRAM_COUNTS_10000(
            "WebRTC.Video.FecBitrateReceivedInKbps",
            static_cast<int>(packet_counter_.num_bytes * 8 / elapsed.seconds() /
                             1000)); //计算并记录接收的FEC比特率,单位为Kbps
      }
    }
  }
  // 清除接收到的包和重置FEC状态
  received_packets_.clear();
  fec_->ResetState(&recovered_packets_);
}
// 获取包计数器的状态
FecPacketCounter UlpfecReceiver::GetPacketCounter() const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return packet_counter_;
}

//     0                   1                    2                   3
//     0 1 2 3 4 5 6 7 8 9 0 1 2 3  4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |F|   block PT  |  timestamp offset         |   block length    |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//
// RFC 2198          RTP Payload for Redundant Audio Data    September 1997
//
//    The bits in the header are specified as follows:
//
//    F: 1 bit First bit in header indicates whether another header block
//        follows.  If 1 further header blocks follow, if 0 this is the
//        last header block.
//        If 0 there is only 1 byte RED header
//
//    block PT: 7 bits RTP payload type for this block.
//
//    timestamp offset:  14 bits Unsigned offset of timestamp of this block
//        relative to timestamp given in RTP header.  The use of an unsigned
//        offset implies that redundant data must be sent after the primary
//        data, and is hence a time to be subtracted from the current
//        timestamp to determine the timestamp of the data for which this
//        block is the redundancy.
//
//    block length:  10 bits Length in bytes of the corresponding data
//        block excluding header.
// 添加接收到的RED包
bool UlpfecReceiver::AddReceivedRedPacket(const RtpPacketReceived& rtp_packet) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  // TODO(bugs.webrtc.org/11993): We get here via Call::DeliverRtp, so should be
  // moved to the network thread.
  // 检查SSRC是否匹配，SSRC（同步源标识符）是一个32位的标识符，用于区分不同的媒体流
  if (rtp_packet.Ssrc() != ssrc_) {
    RTC_LOG(LS_WARNING)
        << "Received RED packet with different SSRC than expected; dropping.";
    return false;
  }
  // 检查包大小是否超过最大IP包大小
  if (rtp_packet.size() > IP_PACKET_SIZE) {
    RTC_LOG(LS_WARNING) << "Received RED packet with length exceeds maximum IP "
                           "packet size; dropping.";
    return false;
  }

  static constexpr uint8_t kRedHeaderLength = 1;
  // 检查有效载荷大小
  if (rtp_packet.payload_size() == 0) {
    RTC_LOG(LS_WARNING) << "Corrupt/truncated FEC packet.";
    return false;
  }

  // 移除RED头部并存储为虚拟RTP包
  auto received_packet =
      std::make_unique<ForwardErrorCorrection::ReceivedPacket>();
  received_packet->pkt = new ForwardErrorCorrection::Packet();

  // 从RED头部获取有效载荷类型和序列号
  uint8_t payload_type = rtp_packet.payload()[0] & 0x7f;
  received_packet->is_fec = payload_type == ulpfec_payload_type_;
  received_packet->is_recovered = rtp_packet.recovered();
  received_packet->ssrc = rtp_packet.Ssrc();
  received_packet->seq_num = rtp_packet.SequenceNumber();
  received_packet->extensions = rtp_packet.extension_manager();
  // 检查RED头部的f位是否被设置
  if (rtp_packet.payload()[0] & 0x80) {
    // 如果f位被设置，表示有多个RED头部块
    // WebRTC不支持在一个RED包中生成多个块
    RTC_LOG(LS_WARNING) << "More than 1 block in RED packet is not supported.";
    return false;
  }
  // 更新包计数器
  ++packet_counter_.num_packets;
  packet_counter_.num_bytes += rtp_packet.size();
  if (packet_counter_.first_packet_time == Timestamp::MinusInfinity()) {
    packet_counter_.first_packet_time = clock_->CurrentTime();
  }
  // 如果是FEC包，处理其数据
  if (received_packet->is_fec) {
    ++packet_counter_.num_fec_packets;
    // 提取RED头部后面的数据
    received_packet->pkt->data =
        rtp_packet.Buffer().Slice(rtp_packet.headers_size() + kRedHeaderLength,
                                  rtp_packet.payload_size() - kRedHeaderLength);
  } else {
    // 不是FEC包，复制RTP头部
    received_packet->pkt->data.EnsureCapacity(rtp_packet.size() -
                                              kRedHeaderLength);
    received_packet->pkt->data.SetData(rtp_packet.data(),
                                       rtp_packet.headers_size());
    // 设置有效载荷类型
    uint8_t& payload_type_byte = received_packet->pkt->data.MutableData()[1];
    payload_type_byte &= 0x80;          // 重置RED有效载荷类型
    payload_type_byte += payload_type;  // 设置媒体有效载荷类型
    // 复制有效载荷和填充数据
    received_packet->pkt->data.AppendData(
        rtp_packet.data() + rtp_packet.headers_size() + kRedHeaderLength,
        rtp_packet.size() - rtp_packet.headers_size() - kRedHeaderLength);
  }
  // 如果数据包有内容，将其加入接收包列表
  if (received_packet->pkt->data.size() > 0) {
    received_packets_.push_back(std::move(received_packet));
  }
  return true;
}
// 处理接收到的FEC包
void UlpfecReceiver::ProcessReceivedFec() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);

  // 如果我们遍历 `received_packets_` 并且其中包含导致递归回到该函数的包（例如RED包封装RED包），
  // 那么将会无限递归。为避免这种情况，我们将 `received_packets_` 与一个空向量交换，
  // 这样下次递归调用时不会再次遍历相同的包。这也解决了我们在迭代过程中修改向量的问题（包在 AddReceivedRedPacket 中添加）。
  // 交换 received_packets_ 以避免递归处理相同包
  std::vector<std::unique_ptr<ForwardErrorCorrection::ReceivedPacket>>
      received_packets;
  received_packets.swap(received_packets_);
  RtpHeaderExtensionMap* last_recovered_extension_map = nullptr;
  size_t num_recovered_packets = 0;
  // 遍历接收到的包
  for (const auto& received_packet : received_packets) {
    // 发送接收到的媒体包到VCM
    if (!received_packet->is_fec) {
      ForwardErrorCorrection::Packet* packet = received_packet->pkt.get();

      RtpPacketReceived rtp_packet(&received_packet->extensions);
      if (!rtp_packet.Parse(std::move(packet->data))) {
        // 不要将恢复的包传递给FEC，避免FEC计算损坏
        RTC_LOG(LS_WARNING) << "Corrupted media packet";
        continue;
      }
      recovered_packet_callback_->OnRecoveredPacket(rtp_packet);
      // 某些头部扩展需要在 `received_packet` 中清零，因为它们是在FEC编码后写入包中的。
      // 我们尝试在不复制底层写时复制缓冲区的情况下执行此操作，但如果 `recovered_packet_callback_->OnRecoveredPacket` 持有引用，
      // 则在 'rtp_packet.ZeroMutableExtensions()' 中仍会进行复制。
      rtp_packet.ZeroMutableExtensions();
      packet->data = rtp_packet.Buffer();
    }
    if (!received_packet->is_recovered) {
      // 不要将恢复的包传递给FEC。恢复的包可能具有与原始包不同的RTP头部扩展集，从而导致字节表示不同，这会破坏FEC计算。
      ForwardErrorCorrection::DecodeFecResult decode_result =
          fec_->DecodeFec(*received_packet, &recovered_packets_);
      last_recovered_extension_map = &received_packet->extensions;
      num_recovered_packets += decode_result.num_recovered_packets;
    }
  }

  if (num_recovered_packets == 0) {
    return;
  }

  // 发送任何恢复的媒体包到VCM
  for (const auto& recovered_packet : recovered_packets_) {
    if (recovered_packet->returned) {
      // 已经发送过
      continue;
    }
    ForwardErrorCorrection::Packet* packet = recovered_packet->pkt.get();
    ++packet_counter_.num_recovered_packets;
    // 先设置此标志；如果恢复的包携带RED头部，OnRecoveredPacket 将递归回到这里
    recovered_packet->returned = true;
    RtpPacketReceived parsed_packet(last_recovered_extension_map);
    if (!parsed_packet.Parse(packet->data)) {
      continue;
    }
    parsed_packet.set_recovered(true);
    recovered_packet_callback_->OnRecoveredPacket(parsed_packet);
  }
}

}  // namespace webrtc
