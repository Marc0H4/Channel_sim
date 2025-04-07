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

#include <string.h>

#include <cstdint>
#include <memory>
#include <utility>

#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "modules/rtp_rtcp/source/forward_error_correction.h"
#include "modules/rtp_rtcp/source/forward_error_correction_internal.h"
#include "rtc_base/checks.h"
#include "rtc_base/synchronization/mutex.h"

namespace webrtc {

namespace {
//FEC 头部长度常量
constexpr size_t kRedForFecHeaderLength = 1;

// This controls the maximum amount of excess overhead (actual - target)
// allowed in order to trigger EncodeFec(), before `params_.max_fec_frames`
// is reached. Overhead here is defined as relative to number of media packets.
// 控制触发 EncodeFec() 的最大多余开销
constexpr int kMaxExcessOverhead = 50;  // Q8.

// This is the minimum number of media packets required (above some protection
// level) in order to trigger EncodeFec(), before `params_.max_fec_frames` is
// reached.
// 触发 EncodeFec() 所需的最小媒体包数量
constexpr size_t kMinMediaPackets = 4;

// Threshold on the received FEC protection level, above which we enforce at
// least `kMinMediaPackets` packets for the FEC code. Below this
// threshold `kMinMediaPackets` is set to default value of 1.
//
// The range is between 0 and 255, where 255 corresponds to 100% overhead
// (relative to the number of protected media packets).

//FEC 保护级别的阈值
//当 FEC 保护级别超过这个阈值时，会强制至少使用 kMinMediaPackets 个包进行 FEC 编码。
//在保护级别低于这个阈值时，kMinMediaPackets 可以设置为默认值 1
constexpr uint8_t kHighProtectionThreshold = 80;

// This threshold is used to adapt the `kMinMediaPackets` threshold, based
// on the average number of packets per frame seen so far. When there are few
// packets per frame (as given by this threshold), at least
// `kMinMediaPackets` + 1 packets are sent to the FEC code.

// 根据每帧的平均包数来调整 kMinMediaPackets 阈值
//如果每帧的平均包数低于这个阈值，生成 FEC 包时使用的最小媒体包数量会保持较低值。
//如果平均包数高于这个阈值，最小媒体包数量可能增加，以提供更好的保护。
constexpr float kMinMediaPacketsAdaptationThreshold = 2.0f;

// At construction time, we don't know the SSRC that is used for the generated
// FEC packets, but we still need to give it to the ForwardErrorCorrection ctor
// to be used in the decoding.
// TODO(brandtr): Get rid of this awkwardness by splitting
// ForwardErrorCorrection in two objects -- one encoder and one decoder.
// 在构造时，我们不知道生成的 FEC 包使用的 SSRC
constexpr uint32_t kUnknownSsrc = 0;

}  // namespace
// 参数构造函数
UlpfecGenerator::Params::Params() = default;
UlpfecGenerator::Params::Params(FecProtectionParams delta_params,
                                FecProtectionParams keyframe_params)
    : delta_params(delta_params), keyframe_params(keyframe_params) {}
// ULPFEC 生成器构造函数
UlpfecGenerator::UlpfecGenerator(const Environment& env,
                                 int red_payload_type,
                                 int ulpfec_payload_type)
    : env_(env),
      red_payload_type_(red_payload_type),
      ulpfec_payload_type_(ulpfec_payload_type),
      fec_(ForwardErrorCorrection::CreateUlpfec(kUnknownSsrc)),
      num_protected_frames_(0),
      min_num_media_packets_(1),
      media_contains_keyframe_(false),
      fec_bitrate_(/*max_window_size=*/TimeDelta::Seconds(1)) {}

// FlexFecSender 使用的构造函数，不使用负载类型
UlpfecGenerator::UlpfecGenerator(const Environment& env,
                                 std::unique_ptr<ForwardErrorCorrection> fec)
    : env_(env),
      red_payload_type_(0),
      ulpfec_payload_type_(0),
      fec_(std::move(fec)),
      num_protected_frames_(0),
      min_num_media_packets_(1),
      media_contains_keyframe_(false),
      fec_bitrate_(/*max_window_size=*/TimeDelta::Seconds(1)) {}
// 析构函数
UlpfecGenerator::~UlpfecGenerator() = default;
// 设置保护参数
void UlpfecGenerator::SetProtectionParameters(
    const FecProtectionParams& delta_params,
    const FecProtectionParams& key_params) {
  RTC_DCHECK_GE(delta_params.fec_rate, 0);
  RTC_DCHECK_LE(delta_params.fec_rate, 255); //确保在0-255之间
  RTC_DCHECK_GE(key_params.fec_rate, 0);
  RTC_DCHECK_LE(key_params.fec_rate, 255);
  // 存储新参数并应用于下一个 FEC 包
  MutexLock lock(&mutex_);
  pending_params_.emplace(delta_params, key_params);
}
// 添加包并生成 FEC
void UlpfecGenerator::AddPacketAndGenerateFec(const RtpPacketToSend& packet) {
  RTC_DCHECK_RUNS_SERIALIZED(&race_checker_); //确保代码在单一线程中运行
  RTC_DCHECK(generated_fec_packets_.empty()); //确保当前没有未处理的 FEC 包

  {
    MutexLock lock(&mutex_);
    if (pending_params_) {
      current_params_ = *pending_params_;
      pending_params_.reset();

      if (CurrentParams().fec_rate > kHighProtectionThreshold) {
        min_num_media_packets_ = kMinMediaPackets;
      } else {
        min_num_media_packets_ = 1;
      }
    }
  }

  if (packet.is_key_frame()) {
    media_contains_keyframe_ = true; //关键帧检查
  }
  const bool complete_frame = packet.Marker(); //完整帧标记,识别帧的结尾
  if (media_packets_.size() < kUlpfecMaxMediaPackets) {
    //将用来计算 FEC 报文的原始报文缓存起来，ulpfec 的 mask 最多记录 48 个报文
    auto fec_packet = std::make_unique<ForwardErrorCorrection::Packet>(); //创建一个新的 FEC 包
    fec_packet->data = packet.Buffer(); //将当前包的数据复制到 FEC 包中
    media_packets_.push_back(std::move(fec_packet)); //将 FEC 包添加到 media_packets_ 列表中

    // 保存最后一个 RTP 包的副本，以便在创建新的 ULPFEC+RED 包时复制 RTP 头部
    RTC_DCHECK_GE(packet.headers_size(), kRtpHeaderSize);
    last_media_packet_ = packet;
  }

  if (complete_frame) {
    ++num_protected_frames_;
  }

  auto params = CurrentParams();

  // 在最多 `params_.max_fec_frames` 帧上生成 FEC，或当以下条件满足时生成：
  // (1) 多余开销（实际开销 - 目标开销）小于 `kMaxExcessOverhead`
  // (2) 至少有 `min_num_media_packets_` 个媒体包
  if (complete_frame &&
      (num_protected_frames_ >= params.max_fec_frames ||
       (ExcessOverheadBelowMax() && MinimumMediaPacketsReached()))) {
    // 不使用不等保护特性
    //编码fec包
    constexpr int kNumImportantPackets = 0;
    constexpr bool kUseUnequalProtection = false;
    fec_->EncodeFec(media_packets_, params.fec_rate, kNumImportantPackets,
                    kUseUnequalProtection, params.fec_mask_type,
                    &generated_fec_packets_);
    if (generated_fec_packets_.empty()) {
      ResetState(); //状态重置
    }
  }
}
// 检查多余开销是否低于最大值
bool UlpfecGenerator::ExcessOverheadBelowMax() const {
  RTC_DCHECK_RUNS_SERIALIZED(&race_checker_);

  return ((Overhead() - CurrentParams().fec_rate) < kMaxExcessOverhead); //实际开销 目标开销 
}
// 检查是否达到最小媒体包数量
// 根据每帧的平均包数，动态调整所需的最小包数，以适应不同的传输速率
// 如果每帧的平均包数小于该阈值，只需达到 min_num_media_packets_。
// 如果大于或等于阈值，则要求多一个包（min_num_media_packets_ + 1）。
bool UlpfecGenerator::MinimumMediaPacketsReached() const {
  RTC_DCHECK_RUNS_SERIALIZED(&race_checker_);
  float average_num_packets_per_frame =
      static_cast<float>(media_packets_.size()) / num_protected_frames_;
  int num_media_packets = static_cast<int>(media_packets_.size());
  if (average_num_packets_per_frame < kMinMediaPacketsAdaptationThreshold) {
    return num_media_packets >= min_num_media_packets_;
  } else {
    // 对于较大的速率（更多的包/帧），增加阈值
    return num_media_packets >= min_num_media_packets_ + 1;
  }
}
// 获取当前的保护参数
const FecProtectionParams& UlpfecGenerator::CurrentParams() const {
  RTC_DCHECK_RUNS_SERIALIZED(&race_checker_);
  return media_contains_keyframe_ ? current_params_.keyframe_params //关键帧参数
                                  : current_params_.delta_params; //普通帧参数
}
// 计算最大包开销
size_t UlpfecGenerator::MaxPacketOverhead() const {
  RTC_DCHECK_RUNS_SERIALIZED(&race_checker_);
  return fec_->MaxPacketOverhead();
}
// 获取生成的 FEC 包
std::vector<std::unique_ptr<RtpPacketToSend>> UlpfecGenerator::GetFecPackets() {
  RTC_DCHECK_RUNS_SERIALIZED(&race_checker_);
  if (generated_fec_packets_.empty()) {
    return std::vector<std::unique_ptr<RtpPacketToSend>>();
  }

  // 将 FEC 包（包括 FEC 头部）封装在 RED 包中
  //在使用 RED 封装时，last_media_packet_ 的头部被复制到新的 RED 包中。然后，负载大小被设置为 0，以便为 RED 负载腾出空间。
  
  RTC_CHECK(last_media_packet_.has_value());
  last_media_packet_->SetPayloadSize(0); //清空负载大小，以便用作 RED 包的基础
  // 创建一个向量来存储 FEC 包，并为其分配足够的空间
  std::vector<std::unique_ptr<RtpPacketToSend>> fec_packets;
  fec_packets.reserve(generated_fec_packets_.size());
  // 初始化一个变量，用于跟踪总 FEC 数据大小
  size_t total_fec_size_bytes = 0;
  // 遍历每个生成的 FEC 包,为每个 FEC 包创建一个新的 RtpPacketToSend 实例，基于 last_media_packet_
  for (const auto* fec_packet : generated_fec_packets_) {
    std::unique_ptr<RtpPacketToSend> red_packet =
        std::make_unique<RtpPacketToSend>(*last_media_packet_);
    red_packet->SetPayloadType(red_payload_type_); // 设置 RED 包的负载类型
    red_packet->SetMarker(false); // 将标记位设置为 false
    uint8_t* payload_buffer = red_packet->SetPayloadSize(
        kRedForFecHeaderLength + fec_packet->data.size());// 设置 RED 包的负载大小，包括 RED 头部和 FEC 数据
    //将 FEC 负载类型写入 RED 头部,将 FEC 数据复制到 RED 包的负载中
    payload_buffer[0] = ulpfec_payload_type_;  // RED 头部,指示red包为fec包
    memcpy(&payload_buffer[1], fec_packet->data.data(),
           fec_packet->data.size());
    total_fec_size_bytes += red_packet->size();  //累加当前 RED 包的大小到总 FEC 数据大小
    red_packet->set_packet_type(RtpPacketMediaType::kForwardErrorCorrection); //设置包类型为 FEC
    red_packet->set_allow_retransmission(false); //禁止重传
    red_packet->set_is_red(true); //标记包为 RED 包
    red_packet->set_fec_protect_packet(false); //指示不需要进一步 FEC 保护
    fec_packets.push_back(std::move(red_packet));//将封装好的 RED 包加入 fec_packets 向量
  }
  // 重置状态以准备处理下一个 FEC 生成周期
  ResetState();
  // 更新 FEC 比特率统计
  MutexLock lock(&mutex_);
  fec_bitrate_.Update(total_fec_size_bytes, env_.clock().CurrentTime()); //更新比特率统计

  return fec_packets;//返回封装好的fec包
}
// 获取当前 FEC 传输速率
DataRate UlpfecGenerator::CurrentFecRate() const {
  MutexLock lock(&mutex_);
  return fec_bitrate_.Rate(env_.clock().CurrentTime())
      .value_or(DataRate::Zero());
}
// 计算当前的开销
int UlpfecGenerator::Overhead() const {
  RTC_DCHECK_RUNS_SERIALIZED(&race_checker_);
  RTC_DCHECK(!media_packets_.empty());
  int num_fec_packets =
      fec_->NumFecPackets(media_packets_.size(), CurrentParams().fec_rate);

  // 返回 Q8 格式的开销
  return (num_fec_packets << 8) / media_packets_.size(); //fec数量/媒体包数量*256，范围在0-256之间，表示总开销
}
// 重置生成器状态
void UlpfecGenerator::ResetState() {
  RTC_DCHECK_RUNS_SERIALIZED(&race_checker_);
  media_packets_.clear();
  last_media_packet_.reset();
  generated_fec_packets_.clear();
  num_protected_frames_ = 0;
  media_contains_keyframe_ = false;
}

}  // namespace webrtc
