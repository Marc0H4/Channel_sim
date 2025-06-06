/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/forward_error_correction_internal.h"

#include <string.h>

#include <algorithm>

#include "modules/rtp_rtcp/source/fec_private_tables_bursty.h"
#include "modules/rtp_rtcp/source/fec_private_tables_random.h"
#include "rtc_base/checks.h"

namespace {
// Allow for different modes of protection for packets in UEP case.
enum ProtectionMode {
  kModeNoOverlap,
  kModeOverlap,
  kModeBiasFirstPacket,
};

// Fits an input mask (sub_mask) to an output mask.
// The mask is a matrix where the rows are the FEC packets,
// and the columns are the source packets the FEC is applied to.
// Each row of the mask is represented by a number of mask bytes.
//
// \param[in]  num_mask_bytes     The number of mask bytes of output mask.
// \param[in]  num_sub_mask_bytes The number of mask bytes of input mask.
// \param[in]  num_rows           The number of rows of the input mask.
// \param[in]  sub_mask           A pointer to hold the input mask, of size
//                                [0, num_rows * num_sub_mask_bytes]
// \param[out] packet_mask        A pointer to hold the output mask, of size
//                                [0, x * num_mask_bytes], where x >= num_rows.
void FitSubMask(int num_mask_bytes,
                int num_sub_mask_bytes,
                int num_rows,
                const uint8_t* sub_mask,
                uint8_t* packet_mask) {
  if (num_mask_bytes == num_sub_mask_bytes) {
    memcpy(packet_mask, sub_mask, num_rows * num_sub_mask_bytes);
  } else {
    for (int i = 0; i < num_rows; ++i) {
      int pkt_mask_idx = i * num_mask_bytes;
      int pkt_mask_idx2 = i * num_sub_mask_bytes;
      for (int j = 0; j < num_sub_mask_bytes; ++j) {
        packet_mask[pkt_mask_idx] = sub_mask[pkt_mask_idx2];
        pkt_mask_idx++;
        pkt_mask_idx2++;
      }
    }
  }
}

// Shifts a mask by number of columns (bits), and fits it to an output mask.
// The mask is a matrix where the rows are the FEC packets,
// and the columns are the source packets the FEC is applied to.
// Each row of the mask is represented by a number of mask bytes.
//
// \param[in]  num_mask_bytes     The number of mask bytes of output mask.
// \param[in]  num_sub_mask_bytes The number of mask bytes of input mask.
// \param[in]  num_column_shift   The number columns to be shifted, and
//                                the starting row for the output mask.
// \param[in]  end_row            The ending row for the output mask.
// \param[in]  sub_mask           A pointer to hold the input mask, of size
//                                [0, (end_row_fec - start_row_fec) *
//                                    num_sub_mask_bytes]
// \param[out] packet_mask        A pointer to hold the output mask, of size
//                                [0, x * num_mask_bytes],
//                                where x >= end_row_fec.
// TODO(marpan): This function is doing three things at the same time:
// shift within a byte, byte shift and resizing.
// Split up into subroutines.
void ShiftFitSubMask(int num_mask_bytes,
                     int res_mask_bytes,
                     int num_column_shift,
                     int end_row,
                     const uint8_t* sub_mask,
                     uint8_t* packet_mask) {
  // Number of bit shifts within a byte
  const int num_bit_shifts = (num_column_shift % 8);
  const int num_byte_shifts = num_column_shift >> 3;

  // Modify new mask with sub-mask21.

  // Loop over the remaining FEC packets.
  for (int i = num_column_shift; i < end_row; ++i) {
    // Byte index of new mask, for row i and column res_mask_bytes,
    // offset by the number of bytes shifts
    int pkt_mask_idx =
        i * num_mask_bytes + res_mask_bytes - 1 + num_byte_shifts;
    // Byte index of sub_mask, for row i and column res_mask_bytes
    int pkt_mask_idx2 =
        (i - num_column_shift) * res_mask_bytes + res_mask_bytes - 1;

    uint8_t shift_right_curr_byte = 0;
    uint8_t shift_left_prev_byte = 0;
    uint8_t comb_new_byte = 0;

    // Handle case of num_mask_bytes > res_mask_bytes:
    // For a given row, copy the rightmost "numBitShifts" bits
    // of the last byte of sub_mask into output mask.
    if (num_mask_bytes > res_mask_bytes) {
      shift_left_prev_byte = (sub_mask[pkt_mask_idx2] << (8 - num_bit_shifts));
      packet_mask[pkt_mask_idx + 1] = shift_left_prev_byte;
    }

    // For each row i (FEC packet), shift the bit-mask of the sub_mask.
    // Each row of the mask contains "resMaskBytes" of bytes.
    // We start from the last byte of the sub_mask and move to first one.
    for (int j = res_mask_bytes - 1; j > 0; j--) {
      // Shift current byte of sub21 to the right by "numBitShifts".
      shift_right_curr_byte = sub_mask[pkt_mask_idx2] >> num_bit_shifts;

      // Fill in shifted bits with bits from the previous (left) byte:
      // First shift the previous byte to the left by "8-numBitShifts".
      shift_left_prev_byte =
          (sub_mask[pkt_mask_idx2 - 1] << (8 - num_bit_shifts));

      // Then combine both shifted bytes into new mask byte.
      comb_new_byte = shift_right_curr_byte | shift_left_prev_byte;

      // Assign to new mask.
      packet_mask[pkt_mask_idx] = comb_new_byte;
      pkt_mask_idx--;
      pkt_mask_idx2--;
    }
    // For the first byte in the row (j=0 case).
    shift_right_curr_byte = sub_mask[pkt_mask_idx2] >> num_bit_shifts;
    packet_mask[pkt_mask_idx] = shift_right_curr_byte;
  }
}

}  // namespace

namespace webrtc {
namespace internal {

PacketMaskTable::PacketMaskTable(FecMaskType fec_mask_type,
                                 int num_media_packets)
    : table_(PickTable(fec_mask_type, num_media_packets)) {}

PacketMaskTable::~PacketMaskTable() = default;

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
  return {&fec_packet_mask_[0],
          static_cast<size_t>(num_fec_packets * mask_length)};
}

// If `num_media_packets` is larger than the maximum allowed by `fec_mask_type`
// for the bursty type, or the random table is explicitly asked for, then the
// random type is selected. Otherwise the bursty table callback is returned.
const uint8_t* PacketMaskTable::PickTable(FecMaskType fec_mask_type,
                                          int num_media_packets) {
  RTC_DCHECK_GE(num_media_packets, 0);
  RTC_DCHECK_LE(static_cast<size_t>(num_media_packets), kUlpfecMaxMediaPackets);

  if (fec_mask_type != kFecMaskRandom &&
      num_media_packets <=
          static_cast<int>(fec_private_tables::kPacketMaskBurstyTbl[0])) {
    return &fec_private_tables::kPacketMaskBurstyTbl[0];
  }

  return &fec_private_tables::kPacketMaskRandomTbl[0];
}

// Remaining protection after important (first partition) packet protection
void RemainingPacketProtection(int num_media_packets,
                               int num_fec_remaining,
                               int num_fec_for_imp_packets,
                               int num_mask_bytes,
                               ProtectionMode mode,
                               uint8_t* packet_mask,
                               PacketMaskTable* mask_table) {
  if (mode == kModeNoOverlap) {
    // sub_mask21

    const int res_mask_bytes =
        PacketMaskSize(num_media_packets - num_fec_for_imp_packets);

    auto end_row = (num_fec_for_imp_packets + num_fec_remaining);
    rtc::ArrayView<const uint8_t> packet_mask_sub_21 = mask_table->LookUp(
        num_media_packets - num_fec_for_imp_packets, num_fec_remaining);

    ShiftFitSubMask(num_mask_bytes, res_mask_bytes, num_fec_for_imp_packets,
                    end_row, &packet_mask_sub_21[0], packet_mask);

  } else if (mode == kModeOverlap || mode == kModeBiasFirstPacket) {
    // sub_mask22
    rtc::ArrayView<const uint8_t> packet_mask_sub_22 =
        mask_table->LookUp(num_media_packets, num_fec_remaining);

    FitSubMask(num_mask_bytes, num_mask_bytes, num_fec_remaining,
               &packet_mask_sub_22[0],
               &packet_mask[num_fec_for_imp_packets * num_mask_bytes]);

    if (mode == kModeBiasFirstPacket) {
      for (int i = 0; i < num_fec_remaining; ++i) {
        int pkt_mask_idx = i * num_mask_bytes;
        packet_mask[pkt_mask_idx] = packet_mask[pkt_mask_idx] | (1 << 7);
      }
    }
  } else {
    RTC_DCHECK_NOTREACHED();
  }
}

// Protection for important (first partition) packets
void ImportantPacketProtection(int num_fec_for_imp_packets,
                               int num_imp_packets,
                               int num_mask_bytes,
                               uint8_t* packet_mask,
                               PacketMaskTable* mask_table) {
  const int num_imp_mask_bytes = PacketMaskSize(num_imp_packets);

  // Get sub_mask1 from table
  rtc::ArrayView<const uint8_t> packet_mask_sub_1 =
      mask_table->LookUp(num_imp_packets, num_fec_for_imp_packets);

  FitSubMask(num_mask_bytes, num_imp_mask_bytes, num_fec_for_imp_packets,
             &packet_mask_sub_1[0], packet_mask);
}

// This function sets the protection allocation: i.e., how many FEC packets
// to use for num_imp (1st partition) packets, given the: number of media
// packets, number of FEC packets, and number of 1st partition packets.
int SetProtectionAllocation(int num_media_packets,
                            int num_fec_packets,
                            int num_imp_packets) {
  // TODO(marpan): test different cases for protection allocation:

  // Use at most (alloc_par * num_fec_packets) for important packets.
  float alloc_par = 0.5;
  int max_num_fec_for_imp = alloc_par * num_fec_packets;

  int num_fec_for_imp_packets = (num_imp_packets < max_num_fec_for_imp)
                                    ? num_imp_packets
                                    : max_num_fec_for_imp;

  // Fall back to equal protection in this case
  if (num_fec_packets == 1 && (num_media_packets > 2 * num_imp_packets)) {
    num_fec_for_imp_packets = 0;
  }

  return num_fec_for_imp_packets;
}

// UEP（不等保护）的修改：重用离线表中的包掩码。
// 注意：这些掩码是为等保护情况设计的，假设随机包丢失。

// 当前版本有3种模式（选项）用于从现有掩码构建UEP掩码。
// 将来版本可能会添加各种其他组合。
// 长期来看，我们可能会为UEP情况专门添加一组表。
// TODO(marpan): 还需考虑针对突发丢包情况修改掩码。

// 掩码的特征为（要保护的包数，保护用的FEC数）。
// 保护因子定义为：（保护用的FEC数 / 要保护的包数）。

// 设 k=媒体包数，n=总包数，（n-k）=FEC包数，m=重要包数。

// 对于保护模式0和1：
// 一个掩码（sub_mask1）用于第一部分的包，
// 另一个掩码（sub_mask21/22，分别用于模式0/1）用于剩余的FEC包。

// 在模式0和1中，第一部分的包（重要包数）被视为同等重要，
// 并获得比剩余部分包更多的保护。

// 对于重要包数：
// sub_mask1 = (m, t)：保护 = t/(m)，其中 t=F(k,n-k,m)。
// t=F(k,n-k,m) 是用于保护第一部分的包数，
// 由函数 SetProtectionAllocation() 确定。

// 对于剩余的保护：
// 模式0：sub_mask21 = (k-m,n-k-t)：保护 = (n-k-t)/(k-m)
// 模式0中，两部分之间没有保护重叠。
// 对于模式0，我们通常设置 t = min(m, n-k)。

// 模式1：sub_mask22 = (k, n-k-t)，保护为 (n-k-t)/(k)
// 模式1中，两部分之间有保护重叠（首选）。

// 对于保护模式2：
// 这使得列表中的第一个包（即第一部分的第一个包）获得更多保护。
// 在模式2中，等保护掩码（从模式1中获得，t=0）被修改
// （在包掩码的第一列添加更多“1”）以偏向于对第一个源包的更高保护。

// 保护模式2可以扩展为一种滑动保护
// （即在各列之间变化“1”的数量/密度）来保护包。


void UnequalProtectionMask(int num_media_packets,
                           int num_fec_packets,
                           int num_imp_packets,
                           int num_mask_bytes,
                           uint8_t* packet_mask,
                           PacketMaskTable* mask_table) {
  // Set Protection type and allocation
  // TODO(marpan): test/update for best mode and some combinations thereof.

  ProtectionMode mode = kModeOverlap;
  int num_fec_for_imp_packets = 0;

  if (mode != kModeBiasFirstPacket) {
    num_fec_for_imp_packets = SetProtectionAllocation(
        num_media_packets, num_fec_packets, num_imp_packets);
  }

  int num_fec_remaining = num_fec_packets - num_fec_for_imp_packets;
  // Done with setting protection type and allocation

  //
  // Generate sub_mask1
  //
  if (num_fec_for_imp_packets > 0) {
    ImportantPacketProtection(num_fec_for_imp_packets, num_imp_packets,
                              num_mask_bytes, packet_mask, mask_table);
  }

  //
  // Generate sub_mask2
  //
  if (num_fec_remaining > 0) {
    RemainingPacketProtection(num_media_packets, num_fec_remaining,
                              num_fec_for_imp_packets, num_mask_bytes, mode,
                              packet_mask, mask_table);
  }
}

// This algorithm is tailored to look up data in the `kPacketMaskRandomTbl` and
// `kPacketMaskBurstyTbl` tables. These tables only cover fec code for up to 12
// media packets. Starting from 13 media packets, the fec code will be generated
// at runtime. The format of those arrays is that they're essentially a 3
// dimensional array with the following dimensions: * media packet
//   * Size for kPacketMaskRandomTbl: 12
//   * Size for kPacketMaskBurstyTbl: 12
// * fec index
//   * Size for both random and bursty table increases from 1 to number of rows.
//     (i.e. 1-48, or 1-12 respectively).
// * Fec data (what actually gets returned)
//   * Size for kPacketMaskRandomTbl: 2 bytes.
//     * For all entries: 2 * fec index (1 based)
//   * Size for kPacketMaskBurstyTbl: 2 bytes.
//     * For all entries: 2 * fec index (1 based)
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
  return {&entry[0], size};
}

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
  } else {  // 不等保护情况
    UnequalProtectionMask(num_media_packets, num_fec_packets, num_imp_packets,
                          num_mask_bytes, packet_mask, mask_table);
  }  // 结束不等保护修改
}  // 结束 GetPacketMasks

size_t PacketMaskSize(size_t num_sequence_numbers) {
  RTC_DCHECK_LE(num_sequence_numbers, 8 * kUlpfecPacketMaskSizeLBitSet);
  if (num_sequence_numbers > 8 * kUlpfecPacketMaskSizeLBitClear) {
    return kUlpfecPacketMaskSizeLBitSet;
  }
  return kUlpfecPacketMaskSizeLBitClear;
}

void InsertZeroColumns(int num_zeros,
                       uint8_t* new_mask,
                       int new_mask_bytes,
                       int num_fec_packets,
                       int new_bit_index) {
  for (uint16_t row = 0; row < num_fec_packets; ++row) {
    const int new_byte_index = row * new_mask_bytes + new_bit_index / 8;
    const int max_shifts = (7 - (new_bit_index % 8));
    new_mask[new_byte_index] <<= std::min(num_zeros, max_shifts);
  }
}

void CopyColumn(uint8_t* new_mask,
                int new_mask_bytes,
                uint8_t* old_mask,
                int old_mask_bytes,
                int num_fec_packets,
                int new_bit_index,
                int old_bit_index) {
  RTC_CHECK_LT(new_bit_index, 8 * new_mask_bytes);

  // Copy column from the old mask to the beginning of the new mask and shift it
  // out from the old mask.
  for (uint16_t row = 0; row < num_fec_packets; ++row) {
    int new_byte_index = row * new_mask_bytes + new_bit_index / 8;
    int old_byte_index = row * old_mask_bytes + old_bit_index / 8;
    new_mask[new_byte_index] |= ((old_mask[old_byte_index] & 0x80) >> 7);
    if (new_bit_index % 8 != 7) {
      new_mask[new_byte_index] <<= 1;
    }
    old_mask[old_byte_index] <<= 1;
  }
}

}  // namespace internal
}  // namespace webrtc
