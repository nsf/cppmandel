#pragma once

#include "Math/Vec.h"

template <typename VT>
struct GenericRect {
	using T = decltype(VT().x);

	VT min, max;

	GenericRect() = default;
	GenericRect(const VT &min, const VT &max): min(min), max(max) {}
	GenericRect(T x0, T y0, T x1, T y1): min(x0, y0), max(x1, y1) {}

	T width() const { return max.x - min.x + 1; }
	T height() const { return max.y - min.y + 1; }
	VT size() const { return VT(width(), height()); }

	void move(const VT &p) { min += p; max += p; }

	T top() const { return min.y; }
	T bottom() const { return max.y; }
	T left() const { return min.x; }
	T right() const { return max.x; }

	VT top_left() const { return VT(min.x, min.y); }
	VT top_right() const { return VT(max.x, min.y); }
	VT bottom_left() const { return VT(min.x, max.y); }
	VT bottom_right() const { return VT(max.x, max.y); }
	VT center() const { return min+(max-min)/VT(2); }

	void set_top(T v) { min.y = v; }
	void set_bottom(T v) { max.y = v; }
	void set_left(T v) { min.x = v; }
	void set_right(T v) { max.x = v; }

	void set_top_left(const VT &v) { min.x = v.x; min.y = v.y; }
	void set_top_right(const VT &v) { max.x = v.x; min.y = v.y; }
	void set_bottom_left(const VT &v) { min.x = v.x; max.y = v.y; }
	void set_bottom_right(const VT &v) { max.x = v.x; max.y = v.y; }

	void set_size(const VT &v) { max = min + v - VT(1); }
	void set_width(T v) { max.x = min.x + v - 1; }
	void set_height(T v) { max.y = min.y + v - 1; }

	bool valid() const { return min <= max; }
};

using Rect = GenericRect<Vec2i>;
using RectF = GenericRect<Vec2f>;
using RectD = GenericRect<Vec2d>;

template <typename VT>
static inline bool operator==(const GenericRect<VT> &l, const GenericRect<VT> &r) { return l.min == r.min && l.max == r.max; }
template <typename VT>
static inline bool operator!=(const GenericRect<VT> &l, const GenericRect<VT> &r) { return l.min != r.min || l.max != r.max; }

static inline Rect Rect_WH(const Vec2i &p, const Vec2i &size) { return Rect(p, p+size-Vec2i(1)); }
static inline Rect Rect_WH(int x, int y, int w, int h) { return Rect_WH(Vec2i(x, y), Vec2i(w, h)); }
static inline Rect Rect_Intersection(const Rect &r1, const Rect &r2) { return Rect(max(r1.min, r2.min), min(r1.max, r2.max)); }
static inline Rect Rect_Valid(const Rect &r)
{
	if (!r.valid())
		return Rect(r.max, r.min);
	return r;
}
static inline Rect Rect_CenteredIn(const Vec2i &size, const Rect &r)
{
	const Vec2i rsize = r.size();
	const Vec2i offset = (rsize - size) / Vec2i(2);
	return Rect_WH(r.top_left() + offset, size);
}

template <typename VT>
static inline bool contains(const GenericRect<VT> &r, const VT &p) { return r.min <= p && p <= r.max; }
template <typename VT>
static inline bool intersects(const GenericRect<VT> &r1, const GenericRect<VT> &r2)
{
	return !(
		r1.max.x < r2.min.x ||
		r1.max.y < r2.min.y ||
		r1.min.x > r2.max.x ||
		r1.min.y > r2.max.y
	);
}
template <typename VT>
static inline bool contains(const GenericRect<VT> &r1, const GenericRect<VT> &r2)
{
	return GenericRect<VT>(max(r1.min, r2.min), min(r1.max, r2.max)) == r2;
}

#define RECT(r) (r).min.x, (r).min.y, (r).max.x, (r).max.y
