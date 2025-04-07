/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/ulpfec_header_reader_writer.h"

#include <string.h>

#include "api/scoped_refptr.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "modules/rtp_rtcp/source/forward_error_correction_internal.h"
#include "rtc_base/checks.h"

namespace webrtc {

namespace {

// 一批中最多可以保护的媒体包数量
constexpr size_t kMaxMediaPackets = 48;

// FEC解码器跟踪的最大媒体包数量。
// 保持比`kMaxMediaPackets`更大的跟踪窗口，以应对网络中的包重排序
constexpr size_t kMaxTrackedMediaPackets = 4 * kMaxMediaPackets;

// ForwardErrorCorrection中存储的最大FEC包数量
constexpr size_t kMaxFecPackets = kMaxMediaPackets;

// FEC Level 0头部大小（字节）
constexpr size_t kFecLevel0HeaderSize = 10;

// FEC Level 1（ULP）头部大小（字节），L位被设置
constexpr size_t kFecLevel1HeaderSizeLBitSet = 2 + kUlpfecPacketMaskSizeLBitSet;

// FEC Level 1（ULP）头部大小（字节），L位被清除
constexpr size_t kFecLevel1HeaderSizeLBitClear =
    2 + kUlpfecPacketMaskSizeLBitClear;
// 包掩码的偏移量
constexpr size_t kPacketMaskOffset = kFecLevel0HeaderSize + 2;
// 根据包掩码大小计算ULPFEC头部大小
size_t UlpfecHeaderSize(size_t packet_mask_size) {
  RTC_DCHECK_LE(packet_mask_size, kUlpfecPacketMaskSizeLBitSet);
  if (packet_mask_size <= kUlpfecPacketMaskSizeLBitClear) {
    return kFecLevel0HeaderSize + kFecLevel1HeaderSizeLBitClear;
  } else {
    return kFecLevel0HeaderSize + kFecLevel1HeaderSizeLBitSet;
  }
}

}  // namespace
// ULPFEC头部读取器构造函数
UlpfecHeaderReader::UlpfecHeaderReader()
    : FecHeaderReader(kMaxTrackedMediaPackets, kMaxFecPackets) {}
// ULPFEC头部读取器析构函数
UlpfecHeaderReader::~UlpfecHeaderReader() = default;
// 读取FEC头部
bool UlpfecHeaderReader::ReadFecHeader(
    ForwardErrorCorrection::ReceivedFecPacket* fec_packet) const {
  uint8_t* data = fec_packet->pkt->data.MutableData();
   // 检查数据大小是否足够
  if (fec_packet->pkt->data.size() < kPacketMaskOffset) {
    return false;  // 包被截断
  }
   // 检查L位是否被设置
  bool l_bit = (data[0] & 0x40) != 0u;
  // 根据L位确定包掩码大小
  size_t packet_mask_size =
      l_bit ? kUlpfecPacketMaskSizeLBitSet : kUlpfecPacketMaskSizeLBitClear;
  // 计算FEC头部大小
  fec_packet->fec_header_size = UlpfecHeaderSize(packet_mask_size);
  // 读取序列号基数
  uint16_t seq_num_base = ByteReader<uint16_t>::ReadBigEndian(&data[2]);
  // 设置受保护流的信息
  fec_packet->protected_streams = {{.ssrc = fec_packet->ssrc,  // Due to RED.
                                    .seq_num_base = seq_num_base,
                                    .packet_mask_offset = kPacketMaskOffset,
                                    .packet_mask_size = packet_mask_size}};
  // 读取保护长度
  fec_packet->protection_length =
      ByteReader<uint16_t>::ReadBigEndian(&data[10]);

  // 将长度恢复字段存储在头部的临时位置。
  // 这使得头部与相应的FlexFEC位置兼容，从而简化XOR操作。
  memcpy(&data[2], &data[8], 2);

  return true;
}
// ULPFEC头部写入器构造函数，初始化基类
UlpfecHeaderWriter::UlpfecHeaderWriter()
    : FecHeaderWriter(kMaxMediaPackets,
                      kMaxFecPackets,
                      kFecLevel0HeaderSize + kFecLevel1HeaderSizeLBitSet) {}
// ULPFEC头部写入器析构函数
UlpfecHeaderWriter::~UlpfecHeaderWriter() = default;

// 返回包掩码的最小大小
size_t UlpfecHeaderWriter::MinPacketMaskSize(const uint8_t* /* packet_mask */,
                                             size_t packet_mask_size) const {
  return packet_mask_size;
}
// 根据包掩码大小返回FEC头部大小
size_t UlpfecHeaderWriter::FecHeaderSize(size_t packet_mask_size) const {
  return UlpfecHeaderSize(packet_mask_size);
}
// 完成FEC头部的最终设置
void UlpfecHeaderWriter::FinalizeFecHeader(
    rtc::ArrayView<const ProtectedStream> protected_streams,
    ForwardErrorCorrection::Packet& fec_packet) const {
  RTC_CHECK_EQ(protected_streams.size(), 1); // 确保只有一个受保护流
  uint16_t seq_num_base = protected_streams[0].seq_num_base;
  const uint8_t* packet_mask = protected_streams[0].packet_mask.data();
  size_t packet_mask_size = protected_streams[0].packet_mask.size();

  uint8_t* data = fec_packet.data.MutableData();
  // 清除E位
  data[0] &= 0x7f;
  // 根据包掩码大小设置L位
  bool l_bit = (packet_mask_size == kUlpfecPacketMaskSizeLBitSet);
  if (l_bit) {
    data[0] |= 0x40;  // 设置L位
  } else {
    RTC_DCHECK_EQ(packet_mask_size, kUlpfecPacketMaskSizeLBitClear);
    data[0] &= 0xbf;  // 清除L位
  }
  // 从临时位置复制长度恢复字段
  memcpy(&data[8], &data[2], 2);
  // 写入序列号基数
  ByteWriter<uint16_t>::WriteBigEndian(&data[2], seq_num_base);
  // 设置保护长度为整个包的长度
  const size_t fec_header_size = FecHeaderSize(packet_mask_size);
  ByteWriter<uint16_t>::WriteBigEndian(
      &data[10], fec_packet.data.size() - fec_header_size);
  // 复制包掩码到FEC数据
  memcpy(&data[12], packet_mask, packet_mask_size);
}

}  // namespace webrtc
