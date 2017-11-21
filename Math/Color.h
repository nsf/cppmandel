#pragma once

#include "Math/Vec.h"
#include "Core/Slice.h"

struct RGBA8 {
	uint8_t r, g, b, a;
	RGBA8() = default;
	constexpr RGBA8(const RGBA8&) = default;
	constexpr explicit RGBA8(uint32_t c): r((c >> 0) & 0xFF), g((c >> 8) & 0xFF), b((c >> 16) & 0xFF), a((c >> 24) & 0xFF) {}
	constexpr RGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255): r(r), g(g), b(b), a(a) {}
	explicit RGBA8(const Vec3f &v): r(clamp(v.x * 255.0f, 0.0f, 255.0f)), g(clamp(v.y * 255.0f, 0.0f, 255.0f)), b(clamp(v.z * 255.0f, 0.0f, 255.0f)), a(255) {}
	RGBA8 &operator=(const RGBA8&) = default;

	constexpr bool operator==(const RGBA8 &rhs) const { return (r == rhs.r && g == rhs.g && b == rhs.b && a == rhs.a); }
	constexpr bool operator!=(const RGBA8 &rhs) const { return (r != rhs.r || g != rhs.g || b != rhs.b || a != rhs.a); }

	void invert(bool invert_alpha = false) { r = 255-r; g = 255-g; b = 255-b; if (invert_alpha) a = 255-a; }
	constexpr uint32_t source() const { return ((a << 24) | (b << 16) | (g << 8) | (r << 0)); }

	uint8_t &operator[](int i) { return *(&r + i); }
	constexpr uint8_t operator[](int i) const { return *(&r + i); }
};
static inline constexpr RGBA8 RGBA8_R(uint8_t v = 255) { return RGBA8(v, 0, 0); }
static inline constexpr RGBA8 RGBA8_G(uint8_t v = 255) { return RGBA8(0, v, 0); }
static inline constexpr RGBA8 RGBA8_B(uint8_t v = 255) { return RGBA8(0, 0, v); }
static inline constexpr RGBA8 RGBA8_A(uint8_t v = 255) { return RGBA8(255, 255, 255, v); }
static inline constexpr RGBA8 RGBA8_AF(float v) { return RGBA8(255, 255, 255, clamp((int)(v * 255), 0, 255)); }
static inline constexpr RGBA8 RGBA8_Black() { return RGBA8(0, 0, 0); }
static inline constexpr RGBA8 RGBA8_White() { return RGBA8(255, 255, 255); }
static inline constexpr RGBA8 RGBA8_Red()   { return RGBA8(255, 0, 0); }
static inline constexpr RGBA8 RGBA8_Green() { return RGBA8(0, 255, 0); }
static inline constexpr RGBA8 RGBA8_Blue()  { return RGBA8(0, 0, 255); }
static inline constexpr RGBA8 RGBA8_Empty() { return RGBA8(0, 0, 0, 0); }

static inline constexpr RGBA8 lerp(const RGBA8 &c1, const RGBA8 &c2, float val) {
	const float ival = 1.0f - val;
	return RGBA8(
		static_cast<uint8_t>(c1.r * ival + c2.r * val),
		static_cast<uint8_t>(c1.g * ival + c2.g * val),
		static_cast<uint8_t>(c1.b * ival + c2.b * val),
		static_cast<uint8_t>(c1.a * ival + c2.a * val));
}

Vec3f hsv_to_rgb(const Vec3f &hsv);
Vec3f rgb_to_xyz(const Vec3f &rgb);
Vec3f xyz_to_rgb(const Vec3f &xyz);
Vec3f xyz_to_yxy(const Vec3f &xyz);
Vec3f yxy_to_xyz(const Vec3f &yxy);
void generate_random_colors(Slice<Vec3f> colors);

static inline Vec3f srgb_to_linear(const Vec3f &rgb) { return pow(rgb, Vec3f(2.2)); }
