// Minimal environment so MAME's mdec.cpp (BSD-3, smf) compiles as a reference in tests only.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstddef>
#define ATTR_COLD
#define WORD_XOR_LE(a) (a)
typedef uint32_t offs_t;
struct machine_config {};
struct device_type_t {};
typedef const device_type_t &device_type;
#define DECLARE_DEVICE_TYPE(T, C) extern const device_type_t T##_t;
#define DEFINE_DEVICE_TYPE(T, C, S, N) const device_type_t T##_t = device_type_t();
#define PSX_MDEC PSX_MDEC_t
class device_t {
public:
	device_t(const machine_config &, device_type, const char *, device_t *, uint32_t) {}
	virtual ~device_t() {}
	virtual void device_start() {}
	virtual void device_reset() {}
	virtual void device_post_load() {}
};
#define save_item(...)
#define NAME(x) x
#define LOGMASKED(...)
#define LOG(...)
#define logerror(...)
#define osd_printf_debug(...)
namespace util { template <typename T> inline T sext(T x, unsigned b) { return T((int64_t(x) << (64 - b)) >> (64 - b)); } }
