#pragma once
#ifdef MY_FEC_API	
#else
#define MY_FEC_API _declspec(dllimport)
#endif // MY_CAL_API

#include <winsock2.h>
#include "simple_client_server.h"
#include "forward_error_correction.h"

MY_FEC_API	void sendto_fec(SOCKET so, const char* buf, int len, int flags, const sockaddr* to, int tolen, int k, int r, int bitrate);

MY_FEC_API  int  recvfrom_fec(SOCKET so, char* buf, int len, int flags, sockaddr* from, int* fromlen);

MY_FEC_API  int Encode_FEC(const ForwardErrorCorrection::PacketList& media_packets,
	                       UINT32 r,
	                       int num_important_packets,
	                       bool use_unequal_protection,
	                       FecMaskType fec_mask_type,
	                       std::list<ForwardErrorCorrection::Packet*>* fec_packets);
