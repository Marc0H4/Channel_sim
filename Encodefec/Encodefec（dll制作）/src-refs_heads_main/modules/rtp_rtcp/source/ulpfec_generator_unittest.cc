/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/ulpfec_generator.h"

#include <list>
#include <memory>
#include <utility>
#include <vector>

#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "modules/rtp_rtcp/source/fec_test_helper.h"
#include "modules/rtp_rtcp/source/forward_error_correction.h"
#include "test/gtest.h"

namespace webrtc {
// 定义匿名命名空间用于内部使用
namespace {
using test::fec::AugmentedPacket;
using test::fec::AugmentedPacketGenerator;
// 定义常量 FEC 和 RED 的负载类型
constexpr int kFecPayloadType = 96;
constexpr int kRedPayloadType = 97;
constexpr uint32_t kMediaSsrc = 835424;
}  // namespace
//用于验证 RTP 包头的正确性，检查序列号、时间戳和负载类型是否符合预期。
void VerifyHeader(uint16_t seq_num,
                  uint32_t timestamp,
                  int red_payload_type,
                  int fec_payload_type,
                  bool marker_bit,
                  const rtc::CopyOnWriteBuffer& data) {
  // 不设置标记位
  EXPECT_EQ(marker_bit ? 0x80 : 0, data[1] & 0x80);
  EXPECT_EQ(red_payload_type, data[1] & 0x7F);
  EXPECT_EQ(seq_num, (data[2] << 8) + data[3]);
  uint32_t parsed_timestamp =
      (data[4] << 24) + (data[5] << 16) + (data[6] << 8) + data[7];
  EXPECT_EQ(timestamp, parsed_timestamp);
  EXPECT_EQ(static_cast<uint8_t>(fec_payload_type), data[kRtpHeaderSize]);
}

class UlpfecGeneratorTest : public ::testing::Test {
 protected:
  UlpfecGeneratorTest()
      : env_(CreateEnvironment(std::make_unique<SimulatedClock>(1))),
        ulpfec_generator_(env_, kRedPayloadType, kFecPayloadType),
        packet_generator_(kMediaSsrc) {}

  const Environment env_;
  UlpfecGenerator ulpfec_generator_;
  AugmentedPacketGenerator packet_generator_;
};

// 验证在通过模糊测试找到的一个bug，
// 即一个数据包序列中的间隙导致我们在当前 FEC 阻塞字节之外不移动到下一个字节。
// 如果该字节不保护分组，我们将生成空的 FEC。
TEST_F(UlpfecGeneratorTest, NoEmptyFecWithSeqNumGaps) {
  struct Packet {
    size_t header_size;
    size_t payload_size;
    uint16_t seq_num;
    bool marker_bit;
  };
  std::vector<Packet> protected_packets;
   // 插入一些测试数据包
  protected_packets.push_back({15, 3, 41, 0});
  protected_packets.push_back({14, 1, 43, 0});
  protected_packets.push_back({19, 0, 48, 0});
  protected_packets.push_back({19, 0, 50, 0});
  protected_packets.push_back({14, 3, 51, 0});
  protected_packets.push_back({13, 8, 52, 0});
  protected_packets.push_back({19, 2, 53, 0});
  protected_packets.push_back({12, 3, 54, 0});
  protected_packets.push_back({21, 0, 55, 0});
  protected_packets.push_back({13, 3, 57, 1});
  // 设置FEC保护参数
  FecProtectionParams params = {117, 3, kFecMaskBursty};
  ulpfec_generator_.SetProtectionParameters(params, params);
  // 检查每个保护包
  for (Packet p : protected_packets) {
    RtpPacketToSend packet(nullptr);
    packet.SetMarker(p.marker_bit);
    packet.AllocateExtension(RTPExtensionType::kRtpExtensionMid,
                             p.header_size - packet.headers_size());
    packet.SetSequenceNumber(p.seq_num);
    packet.AllocatePayload(p.payload_size);
    // 生成 FEC
    ulpfec_generator_.AddPacketAndGenerateFec(packet);
    // 获取生成的 FEC 包
    std::vector<std::unique_ptr<RtpPacketToSend>> fec_packets =
        ulpfec_generator_.GetFecPackets();
    // 如果没有设置标记位，期望没有生成 FEC 包
    if (!p.marker_bit) {
      EXPECT_TRUE(fec_packets.empty());
    } else {
      EXPECT_FALSE(fec_packets.empty());
    }
  }
}
//测试用例 OneFrameFec:检查在特定条件下（如足够的保护因子和媒体包数量），能否正确生成一个 FEC 包。
TEST_F(UlpfecGeneratorTest, OneFrameFec) {
  // The number of media packets (`kNumPackets`), number of frames (one for
  // this test), and the protection factor (|params->fec_rate|) are set to make
  // sure the conditions for generating FEC are satisfied. This means:
  // (1) protection factor is high enough so that actual overhead over 1 frame
  // of packets is within `kMaxExcessOverhead`, and (2) the total number of
  // media packets for 1 frame is at least `minimum_media_packets_fec_`.
  // 设置媒体包数量、帧数和保护因子，以确保满足生成 FEC 的条件。
  constexpr size_t kNumPackets = 4;
  FecProtectionParams params = {15, 3, kFecMaskRandom};
  packet_generator_.NewFrame(kNumPackets);
   // 期望生成一个 FEC 包
  ulpfec_generator_.SetProtectionParameters(params, params);
  uint32_t last_timestamp = 0;
  // 生成每个包并添加到 FEC 生成器
  for (size_t i = 0; i < kNumPackets; ++i) {
    std::unique_ptr<AugmentedPacket> packet =
        packet_generator_.NextPacket(i, 10);
    RtpPacketToSend rtp_packet(nullptr);
    EXPECT_TRUE(rtp_packet.Parse(packet->data.data(), packet->data.size()));
    ulpfec_generator_.AddPacketAndGenerateFec(rtp_packet);
    last_timestamp = packet->header.timestamp;
  }
  // 获取生成的 FEC 包并验证其数量
  std::vector<std::unique_ptr<RtpPacketToSend>> fec_packets =
      ulpfec_generator_.GetFecPackets();
  EXPECT_EQ(fec_packets.size(), 1u);
  uint16_t seq_num = packet_generator_.NextPacketSeqNum();
  fec_packets[0]->SetSequenceNumber(seq_num);
  EXPECT_TRUE(ulpfec_generator_.GetFecPackets().empty());
  // 验证 FEC 包头
  EXPECT_EQ(fec_packets[0]->headers_size(), kRtpHeaderSize);

  VerifyHeader(seq_num, last_timestamp, kRedPayloadType, kFecPayloadType, false,
               fec_packets[0]->Buffer());
}
//测试用例 TwoFrameFec:验证在多帧情况下，FEC 包的生成是否符合预期。
TEST_F(UlpfecGeneratorTest, TwoFrameFec) {
  // The number of media packets/frame (`kNumPackets`), the number of frames
  // (`kNumFrames`), and the protection factor (|params->fec_rate|) are set to
  // make sure the conditions for generating FEC are satisfied. This means:
  // (1) protection factor is high enough so that actual overhead over
  // `kNumFrames` is within `kMaxExcessOverhead`, and (2) the total number of
  // media packets for `kNumFrames` frames is at least
  // `minimum_media_packets_fec_`.
  // 设置每帧的媒体包数、帧数和保护因子，以确保满足生成 FEC 的条件。
  constexpr size_t kNumPackets = 2;
  constexpr size_t kNumFrames = 2;

  FecProtectionParams params = {15, 3, kFecMaskRandom};
  // Expecting one FEC packet.
  ulpfec_generator_.SetProtectionParameters(params, params);
  uint32_t last_timestamp = 0;
  // 生成每个帧的包并添加到 FEC 生成器
  for (size_t i = 0; i < kNumFrames; ++i) {
    packet_generator_.NewFrame(kNumPackets);
    for (size_t j = 0; j < kNumPackets; ++j) {
      std::unique_ptr<AugmentedPacket> packet =
          packet_generator_.NextPacket(i * kNumPackets + j, 10);
      RtpPacketToSend rtp_packet(nullptr);
      EXPECT_TRUE(rtp_packet.Parse(packet->data.data(), packet->data.size()));
      ulpfec_generator_.AddPacketAndGenerateFec(rtp_packet);
      last_timestamp = packet->header.timestamp;
    }
  }
  // 获取生成的 FEC 包并验证其数量
  std::vector<std::unique_ptr<RtpPacketToSend>> fec_packets =
      ulpfec_generator_.GetFecPackets();
  EXPECT_EQ(fec_packets.size(), 1u);
  const uint16_t seq_num = packet_generator_.NextPacketSeqNum();
  fec_packets[0]->SetSequenceNumber(seq_num);
  VerifyHeader(seq_num, last_timestamp, kRedPayloadType, kFecPayloadType, false,
               fec_packets[0]->Buffer());
}
//测试用例 MixedMediaRtpHeaderLengths，测试不同 RTP 头长度的处理，确保 FEC 包生成时使用了正确的头长度。
TEST_F(UlpfecGeneratorTest, MixedMediaRtpHeaderLengths) {
  constexpr size_t kShortRtpHeaderLength = 12;
  constexpr size_t kLongRtpHeaderLength = 16;

  // 只有一个帧需要生成 FEC
  FecProtectionParams params = {127, 1, kFecMaskRandom};
  ulpfec_generator_.SetProtectionParameters(params, params);

  // 使用短 RTP 头长度填充内部缓冲区
  packet_generator_.NewFrame(kUlpfecMaxMediaPackets + 1);
  for (size_t i = 0; i < kUlpfecMaxMediaPackets; ++i) {
    std::unique_ptr<AugmentedPacket> packet =
        packet_generator_.NextPacket(i, 10);
    RtpPacketToSend rtp_packet(nullptr);
    EXPECT_TRUE(rtp_packet.Parse(packet->data.data(), packet->data.size()));
    EXPECT_EQ(rtp_packet.headers_size(), kShortRtpHeaderLength);
    ulpfec_generator_.AddPacketAndGenerateFec(rtp_packet);
    EXPECT_TRUE(ulpfec_generator_.GetFecPackets().empty());
  }

  // 使用长 RTP 头长度的数据包触发 FEC 生成。
  // 由于内部缓冲区已满，此数据包不会被保护。
  std::unique_ptr<AugmentedPacket> packet =
      packet_generator_.NextPacket(kUlpfecMaxMediaPackets, 10);
  RtpPacketToSend rtp_packet(nullptr);
  EXPECT_TRUE(rtp_packet.Parse(packet->data.data(), packet->data.size()));
  EXPECT_TRUE(rtp_packet.SetPayloadSize(0) != nullptr);
  const uint32_t csrcs[]{1};
  rtp_packet.SetCsrcs(csrcs);

  EXPECT_EQ(rtp_packet.headers_size(), kLongRtpHeaderLength);

  ulpfec_generator_.AddPacketAndGenerateFec(rtp_packet);
  std::vector<std::unique_ptr<RtpPacketToSend>> fec_packets =
      ulpfec_generator_.GetFecPackets();
  EXPECT_FALSE(fec_packets.empty());

  // 确保 RED 头正确放置，即在 RED 包创建中使用了正确的 RTP 头长度。
  uint16_t seq_num = packet_generator_.NextPacketSeqNum();
  for (const auto& fec_packet : fec_packets) {
    fec_packet->SetSequenceNumber(seq_num++);
    EXPECT_EQ(kFecPayloadType, fec_packet->data()[kShortRtpHeaderLength]);
  }
}
//测试用例 UpdatesProtectionParameters，验证保护参数的更新逻辑，确保在关键帧和普通帧之间切换时，参数能够正确应用。
TEST_F(UlpfecGeneratorTest, UpdatesProtectionParameters) {
  const FecProtectionParams kKeyFrameParams = {25, /*max_fec_frames=*/2,
                                               kFecMaskRandom};
  const FecProtectionParams kDeltaFrameParams = {25, /*max_fec_frames=*/5,
                                                 kFecMaskRandom};

  ulpfec_generator_.SetProtectionParameters(kDeltaFrameParams, kKeyFrameParams);

  // 尚未应用任何参数。
  EXPECT_EQ(ulpfec_generator_.CurrentParams().max_fec_frames, 0);

  // 辅助函数，用于添加标记为关键帧或普通帧的单包帧。
  auto add_frame = [&](bool is_keyframe) {
    packet_generator_.NewFrame(1);
    std::unique_ptr<AugmentedPacket> packet =
        packet_generator_.NextPacket(0, 10);
    RtpPacketToSend rtp_packet(nullptr);
    EXPECT_TRUE(rtp_packet.Parse(packet->data.data(), packet->data.size()));
    rtp_packet.set_is_key_frame(is_keyframe);
    ulpfec_generator_.AddPacketAndGenerateFec(rtp_packet);
  };

  // 添加关键帧，应用关键帧参数，尚未生成 FEC。
  add_frame(true);
  EXPECT_EQ(ulpfec_generator_.CurrentParams().max_fec_frames, 2);
  EXPECT_TRUE(ulpfec_generator_.GetFecPackets().empty());

  // 添加普通帧，生成 FEC 包。参数将在下一个添加的包后更新。
  add_frame(false);
  EXPECT_EQ(ulpfec_generator_.CurrentParams().max_fec_frames, 2);
  EXPECT_FALSE(ulpfec_generator_.GetFecPackets().empty());

  // 添加普通帧，现在参数更新。
  add_frame(false);
  EXPECT_EQ(ulpfec_generator_.CurrentParams().max_fec_frames, 5);
  EXPECT_TRUE(ulpfec_generator_.GetFecPackets().empty());

  // 再添加一个普通帧。
  add_frame(false);
  EXPECT_EQ(ulpfec_generator_.CurrentParams().max_fec_frames, 5);
  EXPECT_TRUE(ulpfec_generator_.GetFecPackets().empty());

  // 添加关键帧，参数立即切换到关键帧参数。
  // 两个缓冲帧加上关键帧被保护并发出 FEC，即使帧数超过关键帧的帧数阈值。
  add_frame(true);
  EXPECT_EQ(ulpfec_generator_.CurrentParams().max_fec_frames, 2);
  EXPECT_FALSE(ulpfec_generator_.GetFecPackets().empty());
}

}  // namespace webrtc
