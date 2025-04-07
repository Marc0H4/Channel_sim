/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_RTP_RTCP_SOURCE_FORWARD_ERROR_CORRECTION_H_
#define MODULES_RTP_RTCP_SOURCE_FORWARD_ERROR_CORRECTION_H_

#include <stddef.h>
#include <stdint.h>
#include <list>
#include <memory>
#include <vector>
#include <string.h>
#include <basetsd.h>
#include <winsock2.h>
#include "simple_client_server.h"

class ForwardErrorCorrection {

public:

    class Packet {
    public:
        Packet();
        virtual ~Packet();
        uint16_t packet_mask;            // 2字节的掩码内容
        uint8_t  group_number;           // 1字节的组号信息
        uint8_t  sequence_number;        // 1字节的组内序号信息
        uint8_t  k;                      // 1字节的数据包数量信息
        uint8_t  r;                      // 1字节的冗余包数量信息

        static constexpr size_t kMaxDataSize = 2000;
        uint8_t data[kMaxDataSize];  // 数据字段内容（最大长度为2000字节）

    };

    using PacketList = std::list<std::unique_ptr<Packet>>;

    ~ForwardErrorCorrection();

    int EncodeFec(const PacketList& media_packets,
        UINT32 r,
        int num_important_packets,
        bool use_unequal_protection,
        FecMaskType fec_mask_type,
        std::list<Packet*>* fec_packets);

    // 改写后的sendto_fec函数
    void SendByUlpfec(SOCKET so,
        const char* buf,
        int len,
        int flags,
        const sockaddr* to,
        int tolen,
        int k,
        int r,
        int bitrate);

    void PacketByFEC(const char* buf, int len, int k, int r);

    UINT32 total_sent_packets = 0;

    UINT32 total_sent_src_packets = 0;

    UINT32 total_sent_fec_packets = 0;

    PacketList media_packets;


    PacketList buffer_packets;

private:

    static void XorPayloads(const uint8_t* src, uint8_t* dst, size_t length);

    uint8_t packet_masks_[48 * 6];

    size_t packet_mask_size_;

    int kNumImportantPackets = 0;

    bool kUseUnequalProtection = false;

    bool fec_first_use = true;

    bool fec_last_use = false;

    std::list<ForwardErrorCorrection::Packet*> fec_packets;

    int group_number = 0;

    int sequence_number = 0;

	int packet_size = 0;
};

#endif  // MODULES_RTP_RTCP_SOURCE_FORWARD_ERROR_CORRECTION_H_