#include "Math/Color.h"
#include "Math/Mat.h"

Vec3f hsv_to_rgb(const Vec3f &hsv)
{
	const float h = hsv.x;
	const float s = hsv.y;
	const float v = hsv.z;

	const int h_i = h * 6.0f;
	const float f = h * 6.0f - h_i;
	const float p = v * (1.0f - s);
	const float q = v * (1.0f - f * s);
	const float t = v * (1.0f - (1.0f - f) * s);
	switch (h_i) {
	case 0:
		return Vec3f(v, t, p);
	case 1:
		return Vec3f(q, v, p);
	case 2:
		return Vec3f(p, v, t);
	case 3:
		return Vec3f(p, q, v);
	case 4:
		return Vec3f(t, p, v);
	case 5:
	default:
		return Vec3f(v, p, q);
	}
}

constexpr float GOLDEN_RATIO_CONJUGATE = 0.61803398874989484;

void generate_random_colors(Slice<Vec3f> colors)
{
	float h = 0;
	const float s = 0.7f;
	const float v = 0.95f;

	for (Vec3f &c : colors) {
		c = hsv_to_rgb(Vec3f(h, s, v));
		h += GOLDEN_RATIO_CONJUGATE;
		h = remainderf(h, 1.0f);
	}
}

Vec3f rgb_to_xyz(const Vec3f&)
{
	die("not implemented");
	return Vec3f(0);
}

Vec3f xyz_to_rgb(const Vec3f &xyz)
{
	return Mat3(
		 3.2404542, -0.9692660,  0.0556434,
		-1.5371385,  1.8760108, -0.2040259,
		-0.4985314,  0.0415560,  1.0572252
	) * xyz;
}

Vec3f xyz_to_yxy(const Vec3f &xyz)
{
	return Vec3f(
		xyz.y,
		xyz.x / (xyz.x + xyz.y + xyz.z),
		xyz.y / (xyz.x + xyz.y + xyz.z)
	);
}

Vec3f yxy_to_xyz(const Vec3f &yxy)
{
	Vec3f xyz;
	float ratio = yxy.x / yxy.z;
	xyz.x = yxy.y * ratio;
	xyz.y = yxy.x;
	xyz.z = ratio - xyz.x - xyz.y;
	return xyz;
}
