/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/forward_error_correction.h"

#include <string.h>

#include <algorithm>
#include <utility>

#include "absl/algorithm/container.h"
#include "modules/include/module_common_types_public.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "modules/rtp_rtcp/source/flexfec_03_header_reader_writer.h"
#include "modules/rtp_rtcp/source/forward_error_correction_internal.h"
#include "modules/rtp_rtcp/source/ulpfec_header_reader_writer.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/mod_ops.h"

namespace webrtc {

namespace {
// Transport header size in bytes. Assume UDP/IPv4 as a reasonable minimum.
constexpr size_t kTransportOverhead = 28;

constexpr uint16_t kOldSequenceThreshold = 0x3fff;
}  // namespace

ForwardErrorCorrection::Packet::Packet() : data(0), ref_count_(0) {}
ForwardErrorCorrection::Packet::~Packet() = default;

int32_t ForwardErrorCorrection::Packet::AddRef() {
  return ++ref_count_;
}

int32_t ForwardErrorCorrection::Packet::Release() {
  int32_t ref_count;
  ref_count = --ref_count_;
  if (ref_count == 0)
    delete this;
  return ref_count;
}

// This comparator is used to compare std::unique_ptr's pointing to
// subclasses of SortablePackets. It needs to be parametric since
// the std::unique_ptr's are not covariant w.r.t. the types that
// they are pointing to.
template <typename S, typename T>
bool ForwardErrorCorrection::SortablePacket::LessThan::operator()(
    const S& first,
    const T& second) {
  RTC_DCHECK_EQ(first->ssrc, second->ssrc);
  return IsNewerSequenceNumber(second->seq_num, first->seq_num);
}

ForwardErrorCorrection::ReceivedPacket::ReceivedPacket() = default;
ForwardErrorCorrection::ReceivedPacket::~ReceivedPacket() = default;

ForwardErrorCorrection::RecoveredPacket::RecoveredPacket() = default;
ForwardErrorCorrection::RecoveredPacket::~RecoveredPacket() = default;

ForwardErrorCorrection::ProtectedPacket::ProtectedPacket() = default;
ForwardErrorCorrection::ProtectedPacket::~ProtectedPacket() = default;

ForwardErrorCorrection::ReceivedFecPacket::ReceivedFecPacket() = default;
ForwardErrorCorrection::ReceivedFecPacket::~ReceivedFecPacket() = default;

ForwardErrorCorrection::ForwardErrorCorrection(
    std::unique_ptr<FecHeaderReader> fec_header_reader,
    std::unique_ptr<FecHeaderWriter> fec_header_writer,
    uint32_t ssrc,
    uint32_t protected_media_ssrc)
    : ssrc_(ssrc),
      protected_media_ssrc_(protected_media_ssrc),
      fec_header_reader_(std::move(fec_header_reader)),
      fec_header_writer_(std::move(fec_header_writer)),
      generated_fec_packets_(fec_header_writer_->MaxFecPackets()),
      packet_mask_size_(0) {}

ForwardErrorCorrection::~ForwardErrorCorrection() = default;

std::unique_ptr<ForwardErrorCorrection> ForwardErrorCorrection::CreateUlpfec(
    uint32_t ssrc) {
  std::unique_ptr<FecHeaderReader> fec_header_reader(new UlpfecHeaderReader());
  std::unique_ptr<FecHeaderWriter> fec_header_writer(new UlpfecHeaderWriter());
  return std::unique_ptr<ForwardErrorCorrection>(new ForwardErrorCorrection(
      std::move(fec_header_reader), std::move(fec_header_writer), ssrc, ssrc));
}

std::unique_ptr<ForwardErrorCorrection> ForwardErrorCorrection::CreateFlexfec(
    uint32_t ssrc,
    uint32_t protected_media_ssrc) {
  std::unique_ptr<FecHeaderReader> fec_header_reader(
      new Flexfec03HeaderReader());
  std::unique_ptr<FecHeaderWriter> fec_header_writer(
      new Flexfec03HeaderWriter());
  return std::unique_ptr<ForwardErrorCorrection>(new ForwardErrorCorrection(
      std::move(fec_header_reader), std::move(fec_header_writer), ssrc,
      protected_media_ssrc));
}

int ForwardErrorCorrection::EncodeFec(const PacketList& media_packets,
                                      uint8_t protection_factor,
                                      int num_important_packets,
                                      bool use_unequal_protection,
                                      FecMaskType fec_mask_type,
                                      std::list<Packet*>* fec_packets) {
  const size_t num_media_packets = media_packets.size();

  // 检查参数有效性
  RTC_DCHECK_GT(num_media_packets, 0); //确保有媒体包
  RTC_DCHECK_GE(num_important_packets, 0); //确保重要包数量不为负
  RTC_DCHECK_LE(num_important_packets, num_media_packets); //确保重要包数量不超过总包数
  RTC_DCHECK(fec_packets->empty()); //确保FEC包列表为空
  // 最多支持多少个媒体包？
  // FEC算法本身没有限制媒体包个数上限，这个最大包个数限制是FlexFEC的限制
  // 因为RTP的长度有限、延迟有限制等，因此支持的最大FEC长度也有限制。
  const size_t max_media_packets = fec_header_writer_->MaxMediaPackets(); 
  if (num_media_packets > max_media_packets) {
    // 如果媒体包数量超过最大值，记录警告日志
    RTC_LOG(LS_WARNING) << "Can't protect " << num_media_packets
                        << " media packets per frame. Max is "
                        << max_media_packets << ".";
    return -1;
  }

   // 检查媒体包的有效性
  for (const auto& media_packet : media_packets) {
    RTC_DCHECK(media_packet); // 确保媒体包不为空
    if (media_packet->data.size() < kRtpHeaderSize) {
      // 如果媒体包小于RTP头大小，记录警告日志
      RTC_LOG(LS_WARNING) << "Media packet " << media_packet->data.size()
                          << " bytes "
                             "is smaller than RTP header.";
      return -1;
    }
    // 确保FEC包能适应典型的MTU
    if (media_packet->data.size() + MaxPacketOverhead() + kTransportOverhead >
        IP_PACKET_SIZE) {
      // 如果媒体包加上开销大于IP包大小，记录警告日志
      RTC_LOG(LS_WARNING) << "Media packet " << media_packet->data.size()
                          << " bytes "
                             "with overhead is larger than "
                          << IP_PACKET_SIZE << " bytes.";
    }
  }

  // 准备生成的FEC包
  // FEC包个数 = 媒体包个数 * protection_factor
  // protection_factor这里是从[0, 1]映射到[0, 255]
  int num_fec_packets = NumFecPackets(num_media_packets, protection_factor);
  if (num_fec_packets == 0) {
    return 0;
  }
  // FEC包内存申请，清空
  for (int i = 0; i < num_fec_packets; ++i) {
    generated_fec_packets_[i].data.EnsureCapacity(IP_PACKET_SIZE);
    memset(generated_fec_packets_[i].data.MutableData(), 0, IP_PACKET_SIZE);
    // 使用此作为未触及包的标记
    generated_fec_packets_[i].data.SetSize(0);
    fec_packets->push_back(&generated_fec_packets_[i]);
  }
  // 创建包掩码表
  // 根据 <媒体包个数,FEC包个数> 确定编码的mask（FEC生成矩阵）
  // 关于mask，仔细阅读下前两篇文章，如何生成mask的代码，我们在后面介绍
  internal::PacketMaskTable mask_table(fec_mask_type, num_media_packets);
  packet_mask_size_ = internal::PacketMaskSize(num_media_packets);
  memset(packet_masks_, 0, num_fec_packets * packet_mask_size_);
  internal::GeneratePacketMasks(num_media_packets, num_fec_packets,
                                num_important_packets, use_unequal_protection,
                                &mask_table, packet_masks_);

  // 根据缺失的媒体包调整包掩码
  // 一般我们认为输入的包序号是连续的，万一不连续的？下面逻辑是对这种情况做调整。
  // 那我们的mask也需要调整，对于不存在的sequence number，mask中直接插入0即可
  int num_mask_bits = InsertZerosInPacketMasks(media_packets, num_fec_packets);
  if (num_mask_bits < 0) {
    // 如果由于序列号间隙导致无法保护媒体包，记录信息日志
    RTC_LOG(LS_INFO) << "Due to sequence number gaps, cannot protect media "
                        "packets with a single block of FEC packets.";
    fec_packets->clear();
    return -1;
  }
  packet_mask_size_ = internal::PacketMaskSize(num_mask_bits);

  // 根据上面的mask生成FEC，并存到`generated_fec_packets_`.
  GenerateFecPayloads(media_packets, num_fec_packets);
  // 获取媒体包的SSRC，保护多条不同的流时需要
  // FEC group的起始sequence base，seq base在RTP打包时需要
  const uint32_t media_ssrc = ParseSsrc(media_packets.front()->data.data());
  const uint16_t seq_num_base =
      ParseSequenceNumber(media_packets.front()->data.data());
  // 对生成的所有FEC，处理FEC头部，比如填充seqbase，ssrc，mask等等
  FinalizeFecHeaders(num_fec_packets, media_ssrc, seq_num_base);

  return 0;
}

int ForwardErrorCorrection::NumFecPackets(int num_media_packets,
                                          int protection_factor) {
  // Result in Q0 with an unsigned round.
  int num_fec_packets = (num_media_packets * protection_factor + (1 << 7)) >> 8;
  // Generate at least one FEC packet if we need protection.
  if (protection_factor > 0 && num_fec_packets == 0) {
    num_fec_packets = 1;
  }
  RTC_DCHECK_LE(num_fec_packets, num_media_packets);
  return num_fec_packets;
}

void ForwardErrorCorrection::GenerateFecPayloads(
    const PacketList& media_packets,
    size_t num_fec_packets) {
  // 回忆下，我们第一篇文章中介绍，对于M个媒体包生成F个FEC包
  // 在生成矩阵里，除了上面的单位矩阵，还有F行的矩阵，即F行mask
  // 这里遍历所有行mask，生成FEC
  RTC_DCHECK(!media_packets.empty());
  for (size_t i = 0; i < num_fec_packets; ++i) {
    Packet* const fec_packet = &generated_fec_packets_[i];
    size_t pkt_mask_idx = i * packet_mask_size_;
    // 我们知道，FlexFEC的FEC头部是变长的，根据mask长度来的
    const size_t min_packet_mask_size = fec_header_writer_->MinPacketMaskSize(
        &packet_masks_[pkt_mask_idx], packet_mask_size_);
    const size_t fec_header_size =
        fec_header_writer_->FecHeaderSize(min_packet_mask_size);
    // 这个index用于在mask中索引生成矩阵系数，因为seq之间可能有gap，因此需要这种方法
    size_t media_pkt_idx = 0;
    auto media_packets_it = media_packets.cbegin();
    uint16_t prev_seq_num =
        ParseSequenceNumber((*media_packets_it)->data.data());
    while (media_packets_it != media_packets.end()) {
      Packet* const media_packet = media_packets_it->get();
      // Should `media_packet` be protected by `fec_packet`?
      // mask中对应的bit位如果为1（生成矩阵系数），则表示参与XOR FEC运算
      if (packet_masks_[pkt_mask_idx] & (1 << (7 - media_pkt_idx))) {
        size_t media_payload_length =
            media_packet->data.size() - kRtpHeaderSize;
        // FEC包长度 这里会自动调整，最终会扩展到最长的媒体包
        size_t fec_packet_length = fec_header_size + media_payload_length;
        if (fec_packet_length > fec_packet->data.size()) {
          size_t old_size = fec_packet->data.size();
          fec_packet->data.SetSize(fec_packet_length);
          memset(fec_packet->data.MutableData() + old_size, 0,
                 fec_packet_length - old_size);
        }
        // 对头部做XOR FEC处理，生成FEC头部
        XorHeaders(*media_packet, fec_packet);
        // 对payload做XOR FEC处理
        XorPayloads(*media_packet, media_payload_length, fec_header_size,
                    fec_packet);
      }
      media_packets_it++;
      if (media_packets_it != media_packets.end()) {
        uint16_t seq_num =
            ParseSequenceNumber((*media_packets_it)->data.data());
        media_pkt_idx += static_cast<uint16_t>(seq_num - prev_seq_num);
        prev_seq_num = seq_num;
      }
      pkt_mask_idx += media_pkt_idx / 8;
      media_pkt_idx %= 8;
    }
    RTC_DCHECK_GT(fec_packet->data.size(), 0)
        << "Packet mask is wrong or poorly designed.";
  }
}

int ForwardErrorCorrection::InsertZerosInPacketMasks(
    const PacketList& media_packets,
    size_t num_fec_packets) {
  size_t num_media_packets = media_packets.size();
  if (num_media_packets <= 1) {
    return num_media_packets;
  }
  uint16_t last_seq_num =
      ParseSequenceNumber(media_packets.back()->data.data());
  uint16_t first_seq_num =
      ParseSequenceNumber(media_packets.front()->data.data());
  size_t total_missing_seq_nums =
      static_cast<uint16_t>(last_seq_num - first_seq_num) - num_media_packets +
      1;
  if (total_missing_seq_nums == 0) {
    // All sequence numbers are covered by the packet mask.
    // No zero insertion required.
    return num_media_packets;
  }
  const size_t max_media_packets = fec_header_writer_->MaxMediaPackets();
  if (total_missing_seq_nums + num_media_packets > max_media_packets) {
    return -1;
  }
  // Allocate the new mask.
  size_t tmp_packet_mask_size =
      internal::PacketMaskSize(total_missing_seq_nums + num_media_packets);
  memset(tmp_packet_masks_, 0, num_fec_packets * tmp_packet_mask_size);

  auto media_packets_it = media_packets.cbegin();
  uint16_t prev_seq_num = first_seq_num;
  ++media_packets_it;

  // Insert the first column.
  internal::CopyColumn(tmp_packet_masks_, tmp_packet_mask_size, packet_masks_,
                       packet_mask_size_, num_fec_packets, 0, 0);
  size_t new_bit_index = 1;
  size_t old_bit_index = 1;
  // Insert zeros in the bit mask for every hole in the sequence.
  while (media_packets_it != media_packets.end()) {
    if (new_bit_index == max_media_packets) {
      // We can only cover up to 48 packets.
      break;
    }
    uint16_t seq_num = ParseSequenceNumber((*media_packets_it)->data.data());
    const int num_zeros_to_insert =
        static_cast<uint16_t>(seq_num - prev_seq_num - 1);
    if (num_zeros_to_insert > 0) {
      internal::InsertZeroColumns(num_zeros_to_insert, tmp_packet_masks_,
                                  tmp_packet_mask_size, num_fec_packets,
                                  new_bit_index);
    }
    new_bit_index += num_zeros_to_insert;
    internal::CopyColumn(tmp_packet_masks_, tmp_packet_mask_size, packet_masks_,
                         packet_mask_size_, num_fec_packets, new_bit_index,
                         old_bit_index);
    ++new_bit_index;
    ++old_bit_index;
    prev_seq_num = seq_num;
    ++media_packets_it;
  }
  if (new_bit_index % 8 != 0) {
    // We didn't fill the last byte. Shift bits to correct position.
    for (uint16_t row = 0; row < num_fec_packets; ++row) {
      int new_byte_index = row * tmp_packet_mask_size + new_bit_index / 8;
      tmp_packet_masks_[new_byte_index] <<= (7 - (new_bit_index % 8));
    }
  }
  // Replace the old mask with the new.
  memcpy(packet_masks_, tmp_packet_masks_,
         num_fec_packets * tmp_packet_mask_size);
  return new_bit_index;
}

void ForwardErrorCorrection::FinalizeFecHeaders(size_t num_fec_packets,
                                                uint32_t media_ssrc,
                                                uint16_t seq_num_base) {
  for (size_t i = 0; i < num_fec_packets; ++i) {
    const FecHeaderWriter::ProtectedStream protected_streams[] = {
        {.ssrc = media_ssrc,
         .seq_num_base = seq_num_base,
         .packet_mask = {&packet_masks_[i * packet_mask_size_],
                         packet_mask_size_}}};
    fec_header_writer_->FinalizeFecHeader(protected_streams,
                                          generated_fec_packets_[i]);
  }
}

void ForwardErrorCorrection::ResetState(
    RecoveredPacketList* recovered_packets) {
  // Free the memory for any existing recovered packets, if the caller hasn't.
  recovered_packets->clear();
  received_fec_packets_.clear();
}

void ForwardErrorCorrection::InsertMediaPacket(
    RecoveredPacketList* recovered_packets,
    const ReceivedPacket& received_packet) {
  RTC_DCHECK_EQ(received_packet.ssrc, protected_media_ssrc_);

  // Search for duplicate packets.
  for (const auto& recovered_packet : *recovered_packets) {
    RTC_DCHECK_EQ(recovered_packet->ssrc, received_packet.ssrc);
    if (recovered_packet->seq_num == received_packet.seq_num) {
      // Duplicate packet, no need to add to list.
      return;
    }
  }

  std::unique_ptr<RecoveredPacket> recovered_packet(new RecoveredPacket());
  // This "recovered packet" was not recovered using parity packets.
  recovered_packet->was_recovered = false;
  // This media packet has already been passed on.
  recovered_packet->returned = true;
  recovered_packet->ssrc = received_packet.ssrc;
  recovered_packet->seq_num = received_packet.seq_num;
  recovered_packet->pkt = received_packet.pkt;
  // TODO(holmer): Consider replacing this with a binary search for the right
  // position, and then just insert the new packet. Would get rid of the sort.
  RecoveredPacket* recovered_packet_ptr = recovered_packet.get();
  recovered_packets->push_back(std::move(recovered_packet));
  recovered_packets->sort(SortablePacket::LessThan());
  UpdateCoveringFecPackets(*recovered_packet_ptr);
}

void ForwardErrorCorrection::UpdateCoveringFecPackets(
    const RecoveredPacket& packet) {
  for (auto& fec_packet : received_fec_packets_) {
    // Is this FEC packet protecting the media packet `packet`?
    auto protected_it = absl::c_lower_bound(
        fec_packet->protected_packets, &packet, SortablePacket::LessThan());
    if (protected_it != fec_packet->protected_packets.end() &&
        (*protected_it)->seq_num == packet.seq_num) {
      // Found an FEC packet which is protecting `packet`.
      (*protected_it)->pkt = packet.pkt;
    }
  }
}

void ForwardErrorCorrection::InsertFecPacket(
    const RecoveredPacketList& recovered_packets,
    const ReceivedPacket& received_packet) {
  RTC_DCHECK_EQ(received_packet.ssrc, ssrc_);

  // Check for duplicate.
  for (const auto& existing_fec_packet : received_fec_packets_) {
    RTC_DCHECK_EQ(existing_fec_packet->ssrc, received_packet.ssrc);
    if (existing_fec_packet->seq_num == received_packet.seq_num) {
      // Drop duplicate FEC packet data.
      return;
    }
  }

  std::unique_ptr<ReceivedFecPacket> fec_packet(new ReceivedFecPacket());
  fec_packet->pkt = received_packet.pkt;
  fec_packet->ssrc = received_packet.ssrc;
  fec_packet->seq_num = received_packet.seq_num;
  // Parse ULPFEC/FlexFEC header specific info.
  bool ret = fec_header_reader_->ReadFecHeader(fec_packet.get());
  if (!ret) {
    return;
  }

  RTC_CHECK_EQ(fec_packet->protected_streams.size(), 1);

  if (fec_packet->protected_streams[0].ssrc != protected_media_ssrc_) {
    RTC_LOG(LS_INFO)
        << "Received FEC packet is protecting an unknown media SSRC; dropping.";
    return;
  }

  if (fec_packet->protected_streams[0].packet_mask_offset +
          fec_packet->protected_streams[0].packet_mask_size >
      fec_packet->pkt->data.size()) {
    RTC_LOG(LS_INFO) << "Received corrupted FEC packet; dropping.";
    return;
  }

  // Parse packet mask from header and represent as protected packets.
  for (uint16_t byte_idx = 0;
       byte_idx < fec_packet->protected_streams[0].packet_mask_size;
       ++byte_idx) {
    uint8_t packet_mask =
        fec_packet->pkt
            ->data[fec_packet->protected_streams[0].packet_mask_offset +
                   byte_idx];
    for (uint16_t bit_idx = 0; bit_idx < 8; ++bit_idx) {
      if (packet_mask & (1 << (7 - bit_idx))) {
        std::unique_ptr<ProtectedPacket> protected_packet(
            new ProtectedPacket());
        // This wraps naturally with the sequence number.
        protected_packet->ssrc = protected_media_ssrc_;
        protected_packet->seq_num = static_cast<uint16_t>(
            fec_packet->protected_streams[0].seq_num_base + (byte_idx << 3) +
            bit_idx);
        protected_packet->pkt = nullptr;
        fec_packet->protected_packets.push_back(std::move(protected_packet));
      }
    }
  }

  if (fec_packet->protected_packets.empty()) {
    // All-zero packet mask; we can discard this FEC packet.
    RTC_LOG(LS_WARNING) << "Received FEC packet has an all-zero packet mask.";
  } else {
    AssignRecoveredPackets(recovered_packets, fec_packet.get());
    // TODO(holmer): Consider replacing this with a binary search for the right
    // position, and then just insert the new packet. Would get rid of the sort.
    received_fec_packets_.push_back(std::move(fec_packet));
    received_fec_packets_.sort(SortablePacket::LessThan());
    const size_t max_fec_packets = fec_header_reader_->MaxFecPackets();
    if (received_fec_packets_.size() > max_fec_packets) {
      received_fec_packets_.pop_front();
    }
    RTC_DCHECK_LE(received_fec_packets_.size(), max_fec_packets);
  }
}

void ForwardErrorCorrection::AssignRecoveredPackets(
    const RecoveredPacketList& recovered_packets,
    ReceivedFecPacket* fec_packet) {
  ProtectedPacketList* protected_packets = &fec_packet->protected_packets;
  std::vector<RecoveredPacket*> recovered_protected_packets;

  // Find intersection between the (sorted) containers `protected_packets`
  // and `recovered_packets`, i.e. all protected packets that have already
  // been recovered. Update the corresponding protected packets to point to
  // the recovered packets.
  auto it_p = protected_packets->cbegin();
  auto it_r = recovered_packets.cbegin();
  SortablePacket::LessThan less_than;
  while (it_p != protected_packets->end() && it_r != recovered_packets.end()) {
    if (less_than(*it_p, *it_r)) {
      ++it_p;
    } else if (less_than(*it_r, *it_p)) {
      ++it_r;
    } else {  // *it_p == *it_r.
      // This protected packet has already been recovered.
      (*it_p)->pkt = (*it_r)->pkt;
      ++it_p;
      ++it_r;
    }
  }
}

void ForwardErrorCorrection::InsertPacket(
    const ReceivedPacket& received_packet,
    RecoveredPacketList* recovered_packets) {
  // 丢弃旧的FEC包，以确保`received_fec_packets_`中的序列号跨度最多为序列号空间的1/2。
  // 这对于保持`received_fec_packets_`排序很重要，并且可能减少由于序列号回绕导致的错误解码的可能性。
  if (!received_fec_packets_.empty() &&
      received_packet.ssrc == received_fec_packets_.front()->ssrc) {
    // 当`received_packet`和`front_received_fec_packet`属于同一序列号空间时，
    // 即相同的SSRC，检测回绕才有意义。这种情况发生在`received_packet`是FEC包，
    // 或者`received_packet`是媒体包并且使用了RED+ULPFEC时。
    auto it = received_fec_packets_.begin();
    while (it != received_fec_packets_.end()) {
      uint16_t seq_num_diff = MinDiff(received_packet.seq_num, (*it)->seq_num);
      if (seq_num_diff > kOldSequenceThreshold) {
        it = received_fec_packets_.erase(it);
      } else {
        // 由于`received_fec_packets_`是排序的，没有必要继续迭代。
        break;
      }
    }
  }

  if (received_packet.is_fec) {
    //处理接收到的FEC包，通过解析包掩码来确定哪些媒体数据包被保护，并尝试使用FEC包来恢复任何丢失的媒体数据包。
    //同时，它负责维护一个有序的FEC包列表，确保处理效率和资源使用的平衡。
    InsertFecPacket(*recovered_packets, received_packet);
  } else {
    //处理接收到的媒体数据包，并将其插入到恢复包列表中
    InsertMediaPacket(recovered_packets, received_packet);
  }
  // 丢弃旧的恢复包
  DiscardOldRecoveredPackets(recovered_packets);
}

bool ForwardErrorCorrection::StartPacketRecovery(
    const ReceivedFecPacket& fec_packet,
    RecoveredPacket* recovered_packet) {
  // Ensure pkt is initialized.
  recovered_packet->pkt = new Packet();
  // Sanity check packet length.
  if (fec_packet.pkt->data.size() <
      fec_packet.fec_header_size + fec_packet.protection_length) {
    RTC_LOG(LS_WARNING)
        << "The FEC packet is truncated: it does not contain enough room "
           "for its own header.";
    return false;
  }
  if (fec_packet.protection_length >
      std::min(size_t{IP_PACKET_SIZE - kRtpHeaderSize},
               IP_PACKET_SIZE - fec_packet.fec_header_size)) {
    RTC_LOG(LS_WARNING) << "Incorrect protection length, dropping FEC packet.";
    return false;
  }
  // Initialize recovered packet data.
  recovered_packet->pkt->data.EnsureCapacity(IP_PACKET_SIZE);
  recovered_packet->pkt->data.SetSize(fec_packet.protection_length +
                                      kRtpHeaderSize);
  recovered_packet->returned = false;
  recovered_packet->was_recovered = true;
  // Copy bytes corresponding to minimum RTP header size.
  // Note that the sequence number and SSRC fields will be overwritten
  // at the end of packet recovery.
  memcpy(recovered_packet->pkt->data.MutableData(),
         fec_packet.pkt->data.cdata(), kRtpHeaderSize);
  // Copy remaining FEC payload.
  if (fec_packet.protection_length > 0) {
    memcpy(recovered_packet->pkt->data.MutableData() + kRtpHeaderSize,
           fec_packet.pkt->data.cdata() + fec_packet.fec_header_size,
           fec_packet.protection_length);
  }
  return true;
}

bool ForwardErrorCorrection::FinishPacketRecovery(
    const ReceivedFecPacket& /* fec_packet */,
    RecoveredPacket* recovered_packet) {
  uint8_t* data = recovered_packet->pkt->data.MutableData();
  // Set the RTP version to 2.
  data[0] |= 0x80;  // Set the 1st bit.
  data[0] &= 0xbf;  // Clear the 2nd bit.
  // Recover the packet length, from temporary location.
  const size_t new_size =
      ByteReader<uint16_t>::ReadBigEndian(&data[2]) + kRtpHeaderSize;
  if (new_size > size_t{IP_PACKET_SIZE - kRtpHeaderSize}) {
    RTC_LOG(LS_WARNING) << "The recovered packet had a length larger than a "
                           "typical IP packet, and is thus dropped.";
    return false;
  }
  size_t old_size = recovered_packet->pkt->data.size();
  recovered_packet->pkt->data.SetSize(new_size);
  data = recovered_packet->pkt->data.MutableData();
  if (new_size > old_size) {
    memset(data + old_size, 0, new_size - old_size);
  }

  // Set the SN field.
  ByteWriter<uint16_t>::WriteBigEndian(&data[2], recovered_packet->seq_num);
  // Set the SSRC field.
  ByteWriter<uint32_t>::WriteBigEndian(&data[8], recovered_packet->ssrc);
  return true;
}

void ForwardErrorCorrection::XorHeaders(const Packet& src, Packet* dst) {
  uint8_t* dst_data = dst->data.MutableData();
  const uint8_t* src_data = src.data.cdata();
  // XOR the first 2 bytes of the header: V, P, X, CC, M, PT fields.
  dst_data[0] ^= src_data[0];
  dst_data[1] ^= src_data[1];

  // XOR the length recovery field.
  uint8_t src_payload_length_network_order[2];
  ByteWriter<uint16_t>::WriteBigEndian(src_payload_length_network_order,
                                       src.data.size() - kRtpHeaderSize);
  dst_data[2] ^= src_payload_length_network_order[0];
  dst_data[3] ^= src_payload_length_network_order[1];

  // XOR the 5th to 8th bytes of the header: the timestamp field.
  dst_data[4] ^= src_data[4];
  dst_data[5] ^= src_data[5];
  dst_data[6] ^= src_data[6];
  dst_data[7] ^= src_data[7];

  // Skip the 9th to 12th bytes of the header.
}

void ForwardErrorCorrection::XorPayloads(const Packet& src,
                                         size_t payload_length,
                                         size_t dst_offset,
                                         Packet* dst) {
  // XOR the payload.
  RTC_DCHECK_LE(kRtpHeaderSize + payload_length, src.data.size());
  RTC_DCHECK_LE(dst_offset + payload_length, dst->data.capacity());
  if (dst_offset + payload_length > dst->data.size()) {
    size_t old_size = dst->data.size();
    size_t new_size = dst_offset + payload_length;
    dst->data.SetSize(new_size);
    memset(dst->data.MutableData() + old_size, 0, new_size - old_size);
  }
  uint8_t* dst_data = dst->data.MutableData();
  const uint8_t* src_data = src.data.cdata();
  for (size_t i = 0; i < payload_length; ++i) {
    dst_data[dst_offset + i] ^= src_data[kRtpHeaderSize + i];
  }
}

bool ForwardErrorCorrection::RecoverPacket(const ReceivedFecPacket& fec_packet,
                                           RecoveredPacket* recovered_packet) {
  if (!StartPacketRecovery(fec_packet, recovered_packet)) {
    return false;
  }
  for (const auto& protected_packet : fec_packet.protected_packets) {
    if (protected_packet->pkt == nullptr) {
      // This is the packet we're recovering.
      recovered_packet->seq_num = protected_packet->seq_num;
      recovered_packet->ssrc = protected_packet->ssrc;
    } else {
      XorHeaders(*protected_packet->pkt, recovered_packet->pkt.get());
      XorPayloads(*protected_packet->pkt,
                  protected_packet->pkt->data.size() - kRtpHeaderSize,
                  kRtpHeaderSize, recovered_packet->pkt.get());
    }
  }
  if (!FinishPacketRecovery(fec_packet, recovered_packet)) {
    return false;
  }
  return true;
}
//解码恢复丢失的包，每次恢复一个，恢复一个即删除掉该FEC，再从头开始，直至所有包都恢复，和我们学过的高斯消元法比较类似
size_t ForwardErrorCorrection::AttemptRecovery(
    RecoveredPacketList* recovered_packets) {
  size_t num_recovered_packets = 0;

  auto fec_packet_it = received_fec_packets_.begin();
  while (fec_packet_it != received_fec_packets_.end()) {
    // FEC包带有mask，可以知道保护了哪些媒体包
    // 根据seq base和mask，以及收到的媒体包，确定还需要几个媒体包才能恢复
    int packets_missing = NumCoveredPacketsMissing(**fec_packet_it);

    // 一次只能恢复一个媒体包，丢包超过2个不能恢复，就先判断其他的FEC
    if (packets_missing == 1) {
      // Recovery possible.
      std::unique_ptr<RecoveredPacket> recovered_packet(new RecoveredPacket());
      recovered_packet->pkt = nullptr;
      // 恢复XOR是逆运算，可以参考编码
      // 需要注意填充RTP的一些固定字段，如version
      if (!RecoverPacket(**fec_packet_it, recovered_packet.get())) {
        // Can't recover using this packet, drop it.
        fec_packet_it = received_fec_packets_.erase(fec_packet_it);
        continue;
      }

      ++num_recovered_packets;

      auto* recovered_packet_ptr = recovered_packet.get();
      // Add recovered packet to the list of recovered packets and update any
      // FEC packets covering this packet with a pointer to the data.
      // TODO(holmer): Consider replacing this with a binary search for the
      // right position, and then just insert the new packet. Would get rid of
      // the sort.
      recovered_packets->push_back(std::move(recovered_packet));
      recovered_packets->sort(SortablePacket::LessThan());
      // 恢复了媒体包，在所有用到此媒体包的FEC中置标志位
      UpdateCoveringFecPackets(*recovered_packet_ptr);
      DiscardOldRecoveredPackets(recovered_packets);
      fec_packet_it = received_fec_packets_.erase(fec_packet_it);

      // 一旦FEC恢复了包后，缺少2个包不能恢复，现在可能可以恢复
      // 因此从头开始再判断一次
      fec_packet_it = received_fec_packets_.begin();
    } else if (packets_missing == 0 ||
               IsOldFecPacket(**fec_packet_it, recovered_packets)) {
      // 当前mask下，所有的媒体包都收到或者已经恢复，则删除掉这个FEC（mask）
      // 处理下一个FEC（mask）
      fec_packet_it = received_fec_packets_.erase(fec_packet_it);
    } else {
      // 丢包超过一个，暂时还不能恢复，先恢复其他的
      fec_packet_it++;
    }
  }

  return num_recovered_packets;
}

int ForwardErrorCorrection::NumCoveredPacketsMissing(
    const ReceivedFecPacket& fec_packet) {
  int packets_missing = 0;
  for (const auto& protected_packet : fec_packet.protected_packets) {
    if (protected_packet->pkt == nullptr) {
      ++packets_missing;
      if (packets_missing > 1) {
        break;  // We can't recover more than one packet.
      }
    }
  }
  return packets_missing;
}

void ForwardErrorCorrection::DiscardOldRecoveredPackets(
    RecoveredPacketList* recovered_packets) {
  const size_t max_media_packets = fec_header_reader_->MaxMediaPackets();
  while (recovered_packets->size() > max_media_packets) {
    recovered_packets->pop_front();
  }
  RTC_DCHECK_LE(recovered_packets->size(), max_media_packets);
}

bool ForwardErrorCorrection::IsOldFecPacket(
    const ReceivedFecPacket& fec_packet,
    const RecoveredPacketList* recovered_packets) {
  if (recovered_packets->empty()) {
    return false;
  }

  const uint16_t back_recovered_seq_num = recovered_packets->back()->seq_num;
  const uint16_t last_protected_seq_num =
      fec_packet.protected_packets.back()->seq_num;

  // FEC packet is old if its last protected sequence number is much
  // older than the latest protected sequence number received.
  return (MinDiff(back_recovered_seq_num, last_protected_seq_num) >
          kOldSequenceThreshold);
}

uint16_t ForwardErrorCorrection::ParseSequenceNumber(const uint8_t* packet) {
  return (packet[2] << 8) + packet[3];
}

uint32_t ForwardErrorCorrection::ParseSsrc(const uint8_t* packet) {
  return (packet[8] << 24) + (packet[9] << 16) + (packet[10] << 8) + packet[11];
}

ForwardErrorCorrection::DecodeFecResult ForwardErrorCorrection::DecodeFec(
    const ReceivedPacket& received_packet,
    RecoveredPacketList* recovered_packets) {
  RTC_DCHECK(recovered_packets);

  const size_t max_media_packets = fec_header_reader_->MaxMediaPackets();
  // 接收到的包已经到FEC保护最大长度，如果seq gap超过了这个长度
  // 此时需要reset FEC解码
  if (recovered_packets->size() == max_media_packets) {
    const RecoveredPacket* back_recovered_packet =
        recovered_packets->back().get();

    if (received_packet.ssrc == back_recovered_packet->ssrc) {
      const unsigned int seq_num_diff =
          MinDiff(received_packet.seq_num, back_recovered_packet->seq_num);
      if (seq_num_diff > max_media_packets) {
        // A big gap in sequence numbers. The old recovered packets
        // are now useless, so it's safe to do a reset.
        RTC_LOG(LS_INFO) << "Big gap in media/ULPFEC sequence numbers. No need "
                            "to keep the old packets in the FEC buffers, thus "
                            "resetting them.";
        ResetState(recovered_packets);
      }
    }
  }
  // 插入数据，media包/FEC包
  InsertPacket(received_packet, recovered_packets);

  DecodeFecResult decode_result;
  // 尝试恢复包
  decode_result.num_recovered_packets = AttemptRecovery(recovered_packets);
  return decode_result;
}

size_t ForwardErrorCorrection::MaxPacketOverhead() const {
  return fec_header_writer_->MaxPacketOverhead();
}

FecHeaderReader::FecHeaderReader(size_t max_media_packets,
                                 size_t max_fec_packets)
    : max_media_packets_(max_media_packets),
      max_fec_packets_(max_fec_packets) {}

FecHeaderReader::~FecHeaderReader() = default;

size_t FecHeaderReader::MaxMediaPackets() const {
  return max_media_packets_;
}

size_t FecHeaderReader::MaxFecPackets() const {
  return max_fec_packets_;
}

FecHeaderWriter::FecHeaderWriter(size_t max_media_packets,
                                 size_t max_fec_packets,
                                 size_t max_packet_overhead)
    : max_media_packets_(max_media_packets),
      max_fec_packets_(max_fec_packets),
      max_packet_overhead_(max_packet_overhead) {}

FecHeaderWriter::~FecHeaderWriter() = default;

size_t FecHeaderWriter::MaxMediaPackets() const {
  return max_media_packets_;
}

size_t FecHeaderWriter::MaxFecPackets() const {
  return max_fec_packets_;
}

size_t FecHeaderWriter::MaxPacketOverhead() const {
  return max_packet_overhead_;
}

}  // namespace webrtc
