#pragma once
#ifdef MY_FEC_API	
#else
#define MY_FEC_API _declspec(dllimport)
#endif // MY_CAL_API

#include <winsock2.h>
#include "modules/rtp_rtcp/include/simple_client_server.h"
#include <fstream>
#include "modules/rtp_rtcp/source/forward_error_correction.h" 
#include "modules/rtp_rtcp/source/forward_error_correction_internal.h"
#include "modules/rtp_rtcp/source/fec_private_tables_bursty.h"
#include "modules/rtp_rtcp/source/fec_private_tables_random.h"
#include "udp_with_ulpfec.h"
MY_FEC_API	void sendto_fec(SOCKET so, const char* buf, int len, int flags, const sockaddr* to, int tolen, int k, int r, int bitrate);

MY_FEC_API  int  recvfrom_fec(SOCKET so, char* buf, int len, int flags, sockaddr* from, int* fromlen);