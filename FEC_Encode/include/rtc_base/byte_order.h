#ifndef RTC_BASE_BYTE_ORDER_H_
#define RTC_BASE_BYTE_ORDER_H_

#include <stdint.h>
#include <winsock2.h>  // 需要包含用于字节序转换函数

// 为了使用 Windows socket 函数，可能需要链接到 Ws2_32.lib
// 可以在项目的链接器设置中添加 Ws2_32.lib
// 或者使用 #pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Ws2_32.lib")

namespace rtc {

// 设置和获取8位值
inline void Set8(void* memory, size_t offset, uint8_t v) {
  static_cast<uint8_t*>(memory)[offset] = v;
}
inline uint8_t Get8(const void* memory, size_t offset) {
  return static_cast<const uint8_t*>(memory)[offset];
}

// 设置和获取大端字节序的16、32、64位值
inline void SetBE16(void* memory, uint16_t v) {
  uint16_t val = htons(v);
  memcpy(memory, &val, sizeof(val));
}
inline void SetBE32(void* memory, uint32_t v) {
  uint32_t val = htonl(v);
  memcpy(memory, &val, sizeof(val));
}
inline void SetBE64(void* memory, uint64_t v) {
  uint64_t val = htonll(v);
  memcpy(memory, &val, sizeof(val));
}
inline uint16_t GetBE16(const void* memory) {
  uint16_t val;
  memcpy(&val, memory, sizeof(val));
  return ntohs(val);
}
inline uint32_t GetBE32(const void* memory) {
  uint32_t val;
  memcpy(&val, memory, sizeof(val));
  return ntohl(val);
}
inline uint64_t GetBE64(const void* memory) {
  uint64_t val;
  memcpy(&val, memory, sizeof(val));
  return ntohll(val);
}

// 64位字节序转换函数的实现
inline uint64_t htonll(uint64_t value) {
  const uint32_t high_part = htonl(static_cast<uint32_t>(value >> 32));
  const uint32_t low_part = htonl(static_cast<uint32_t>(value & 0xFFFFFFFFLL));
  return (static_cast<uint64_t>(high_part) << 32) | low_part;
}
inline uint64_t ntohll(uint64_t value) {
  return htonll(value);  // htonll 和 ntohll 在逻辑上是相同的
}

}  // namespace rtc

#endif  // RTC_BASE_BYTE_ORDER_H_