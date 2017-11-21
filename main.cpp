#include "Core/BitArray.h"
#include "Core/UniquePtr.h"
#include "Core/Vector.h"
#include "Math/Color.h"
#include "Math/Rect.h"
#include "Math/Utils.h"
#include "Math/Vec.h"
#include "OS/AsyncQueue.h"

#include <complex>
#include <experimental/coroutine>
#include <initializer_list>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL.h>
#include <stdio.h>

namespace stdx = std::experimental;

using CoroutineHandle = stdx::coroutine_handle<>;
using CoroutineQueue = AsyncQueue<CoroutineHandle>;

UniquePtr<CoroutineQueue> globalQueue;
UniquePtr<CoroutineQueue> mainThreadQueue;

struct Awaiter {
	Awaiter() = default;
	Awaiter(CoroutineHandle coro, std::atomic<int> *count = nullptr): coro(coro), count(count) {}

	void push_or_destroy() {
		if (globalQueue)
			globalQueue->push(coro);
		else
			coro.destroy();
	}

	void notify() {
		if (coro == nullptr)
			return;

		if (count) {
			if (count->fetch_add(-1) == 1)
				push_or_destroy();
		} else {
			push_or_destroy();
		}
	}

	bool await_ready() { return false; }
	void await_resume() {}
	void await_suspend(CoroutineHandle) { notify(); }

private:
	CoroutineHandle coro;
	std::atomic<int> *count = nullptr;
};

template <typename T>
struct Task {
	struct promise_type {
		T result = {};
		Awaiter awaiter;

		auto get_return_object() { return Task{stdx::coroutine_handle<promise_type>::from_promise(*this)}; }
		auto initial_suspend() { return stdx::suspend_always{}; }
		auto final_suspend() { return awaiter; }
		void unhandled_exception() {} // do nothing, exceptions are disabled
		void return_value(T value) {
			result = std::move(value);
		}
	};

	Task(stdx::coroutine_handle<promise_type> h): coro(h) {}
	Task(Task &&r): coro(r.coro) { r.coro = nullptr; }
	~Task() { if (coro) coro.destroy(); }

	NG_DELETE_COPY(Task);

	stdx::coroutine_handle<promise_type> coro;

	// We're never ready, don't await on Task twice! Awaiting on task triggers "await_suspend"
	bool await_ready() { return false; }
	T await_resume() { return coro.promise().result; }

	// When somebody asks for our value we do a suspend and that's when Task is actually scheduled for execution.
	void await_suspend(CoroutineHandle c) {
		coro.promise().awaiter = Awaiter(c);
		globalQueue->push(coro);
	}
};

template <>
struct Task<void> {
	struct promise_type {
		Awaiter awaiter;

		auto get_return_object() { return Task{stdx::coroutine_handle<promise_type>::from_promise(*this)}; }
		auto initial_suspend() { return stdx::suspend_always{}; }
		auto final_suspend() { return stdx::suspend_never{}; }
		void unhandled_exception() {} // do nothing, exceptions are disabled
		void return_void() { awaiter.notify(); }
	};

	Task(stdx::coroutine_handle<promise_type> h): coro(h) {}
	Task(Task &&r): coro(r.coro) { r.coro = nullptr; }

	NG_DELETE_COPY(Task);

	stdx::coroutine_handle<promise_type> coro;
};

// wait for all tasks in the list (execution on global queue)
template <typename T>
struct MultiTaskAwaiter {
	MultiTaskAwaiter(Vector<Task<T>> tasks): tasks(std::move(tasks)), count(this->tasks.length()) {}
	NG_DELETE_COPY_AND_MOVE(MultiTaskAwaiter);

	bool await_ready() { return false; }
	Vector<T> await_resume() {
		Vector<T> result;
		result.reserve(tasks.length());
		for (auto &v : tasks) {
			result.append(std::move(v.coro.promise().result));
		}
		return result;
	}

	void await_suspend(CoroutineHandle c) {
		for (auto &t : tasks) {
			t.coro.promise().awaiter = Awaiter(c, &count);
			globalQueue->push(t.coro);
		}
	}

private:
	Vector<Task<T>> tasks;
	std::atomic<int> count;
};

template <typename T, typename ...Args>
auto co_all(Task<T> &&t, Args &&...args) {
	Vector<Task<T>> v;
	v.reserve(sizeof...(args)+1);
	v.append(std::move(t));
	(v.append(std::move(args)), ...);
	return MultiTaskAwaiter(std::move(v));
}

// wait for a task (execution on main thread queue)
template <typename T>
struct MainThreadAwaiter {
	MainThreadAwaiter(Task<T> task): task(std::move(task)) {}
	NG_DELETE_COPY_AND_MOVE(MainThreadAwaiter);

	bool await_ready() { return false; }
	T await_resume() { return task.coro.promise().result; }
	void await_suspend(CoroutineHandle c) {
		task.coro.promise().awaiter = Awaiter(c);
		mainThreadQueue->push(task.coro);
	}

private:
	Task<T> task;
};

template <>
struct MainThreadAwaiter<void> {
	MainThreadAwaiter(Task<void> task): task(std::move(task)) {}
	NG_DELETE_COPY_AND_MOVE(MainThreadAwaiter);

	bool await_ready() { return false; }
	void await_resume() {}
	void await_suspend(CoroutineHandle c) {
		task.coro.promise().awaiter = Awaiter(c);
		mainThreadQueue->push(task.coro);
	}

private:
	Task<void> task;
};

template <typename T>
auto co_main(Task<T> task) {
	return MainThreadAwaiter(std::move(task));
}

int worker_thread(void*) {
	while (true) {
		auto next = globalQueue->pop();
		if (next == nullptr) {
			return 0;
		}

		if (!next.done()) {
			next.resume();
		}
	}
}

Vector<SDL_Thread*> workers;
int numCPUs = 0;

void terminate_workers() {
	for (int i = 0; i < workers.length(); i++) {
		globalQueue->push(nullptr);
	}
}

void init_workers() {
	globalQueue = make_unique<CoroutineQueue>();
	mainThreadQueue = make_unique<CoroutineQueue>();
	numCPUs = SDL_GetCPUCount();//min(8, SDL_GetCPUCount());
	for (int i = 0; i < numCPUs; i++) {
		workers.append(SDL_CreateThread(worker_thread, "worker", (void*)(int64_t)(i+1)));
	}
}

void wait_for_workers() {
	for (int i = 0; i < numCPUs; i++) {
		SDL_WaitThread(workers[i], nullptr);
	}
	workers.clear();
	globalQueue.reset();

	Vector<CoroutineHandle> buf;
	mainThreadQueue->try_pop_all(&buf);
	for (auto c : buf)
		c.resume();
	mainThreadQueue.reset();
}

//==============================================================================================================
//==============================================================================================================
//==============================================================================================================

Vec2d pixel_size(const RectD &rf, const Rect &r) {
	return rf.size() / ToVec2d(r.size());
}

RectD rect_to_rectd(const Rect &r, const Vec2d &scale, const Vec2d &offset) {
	return RectD(ToVec2d(r.min) * scale + offset, ToVec2d(r.max) * scale + offset);
}

void draw_selection(const Vec2i &a, const Vec2i &b) {
	const auto min = Vec2i(::min(a.x, b.x), ::min(a.y, b.y));
	const auto max = Vec2i(::max(a.x, b.x), ::max(a.y, b.y));
	glColor3ub(255, 0, 0);
	glBegin(GL_LINES);
		glVertex2i(min.x, min.y);
		glVertex2i(max.x, min.y);

		glVertex2i(min.x, min.y);
		glVertex2i(min.x, max.y);

		glVertex2i(max.x, max.y);
		glVertex2i(max.x, min.y);

		glVertex2i(max.x, max.y);
		glVertex2i(min.x, max.y);
	glEnd();
	glColor3ub(255,255,255);
}

void draw_quad(const Vec2i &pos, const Vec2i &size, float u, float v, float u2, float v2) {
	glBegin(GL_QUADS);

	glTexCoord2f(u, v);
	glVertex2i(pos.x, pos.y);

	glTexCoord2f(u2, v);
	glVertex2i(pos.x+size.x, pos.y);

	glTexCoord2f(u2, v2);
	glVertex2i(pos.x+size.x, pos.y+size.y);

	glTexCoord2f(u, v2);
	glVertex2i(pos.x, pos.y+size.y);

	glEnd();
}


struct ColorRange {
	RGBA8 from;
	RGBA8 to;
	float range;
};

static inline constexpr int ITERATIONS = 1024;
static inline constexpr RGBA8 DARK_YELLOW(0xEE, 0xEE, 0x9E, 0xFF);
static inline constexpr RGBA8 DARK_GREEN(0x44, 0x88, 0x44, 0xFF);
static inline constexpr RGBA8 PALE_GREY_BLUE(0x49, 0x93, 0xDD, 0xFF);
static inline constexpr RGBA8 CYAN(0x00, 0xFF, 0xFF, 0xFF);
static inline constexpr RGBA8 RED(0xFF, 0x00, 0x00, 0xFF);
static inline constexpr RGBA8 WHITE(0xFF, 0xFF, 0xFF, 0xFF);
static inline constexpr RGBA8 BLACK(0x00, 0x00, 0x00, 0xFF);
static inline constexpr ColorRange COLOR_SCALE[] = {
	{DARK_YELLOW, DARK_GREEN, 0.25f},
	{DARK_GREEN, CYAN, 0.25f},
	{CYAN, RED, 0.25f},
	{RED, WHITE, 0.125f},
	{WHITE, PALE_GREY_BLUE, 0.125f},
};

struct Palette {
	RGBA8 palette[ITERATIONS+1];
	constexpr Palette(): palette{} {
		int p = 0;
		for (int i = 0; i < int(sizeof(COLOR_SCALE)/sizeof(*COLOR_SCALE)); i++) {
			auto r = COLOR_SCALE[i];
			int n = r.range * ITERATIONS + 0.5;
			for (int j = 0; j < n && j < ITERATIONS; j++) {
				auto c = lerp(r.from, r.to, (float)j/n);
				palette[p] = c;
				p++;
			}
		}
		palette[ITERATIONS] = BLACK;
	}

	constexpr RGBA8 operator[](int i) const { return palette[i]; }
};

static inline constexpr Palette palette;

RGBA8 mandelbrot_at(std::complex<double> c) {
	auto z = std::complex<double>(0, 0);
	for (int i = 0; i < ITERATIONS; i++) {
		z = z * z + c;
		if (z.real() * z.real() + z.imag() * z.imag() > 4.0) {
			return palette[i];
		}
	}
	return palette[ITERATIONS];
}

Vector<uint8_t> mandelbrot(const RectD &rf, const Vec2i &size) {
	Vector<uint8_t> data(area(size)*4);
	const double px = (rf.max.x - rf.min.x) / (double)size.x; // pixel width
	const double py = (rf.max.y - rf.min.y) / (double)size.y; // pixel height
	const double dx = px / 4.0f; // 1/4 of a pixel
	const double dy = py / 4.0f;
	const double offx = px / 2.0f; // 1/2 of a pixel
	const double offy = py / 2.0f;
	for (int y = 0; y < size.y; y++) {
		const double i = (double)y * py + rf.min.y + offy;
		for (int x = 0; x < size.x; x++) {
			const double r = (double)x * px + rf.min.x + offx;

			// some form of supersampling AA, probably not the best one
			const RGBA8 c0 = mandelbrot_at(std::complex<double>(r-dx, i-dy));
			const RGBA8 c1 = mandelbrot_at(std::complex<double>(r+dx, i-dy));
			const RGBA8 c2 = mandelbrot_at(std::complex<double>(r-dx, i+dy));
			const RGBA8 c3 = mandelbrot_at(std::complex<double>(r+dx, i+dy));
			const RGBA8 color = lerp( lerp(c0, c1, 0.5f), lerp(c2, c3, 0.5f), 0.5f );

			// NOTE: comment out lines above and uncomment line below to turn off AA
			// const RGBA8 color = mandelbrot_at(std::complex<double>(r, i));

			const int offset = y * size.x * 4 + x * 4;
			data[offset+0] = color.r;
			data[offset+1] = color.g;
			data[offset+2] = color.b;
			data[offset+3] = color.a;
		}
	}
	return data;
}

struct Tile {
	bool wip = false;
	RGBA8 color;
	GLuint texture[2] = { 0, 0 }; // two lods
	bool released = false;
	int current_lod = {-1}; // -1 if no texture available

	const Vec2i pos;
	Tile(const Vec2i &pos, const Vec2i &tile_size, const Vec2d &scale, const Vec2d &offset): pos(pos) {
		const auto r = Rect_WH(pos, tile_size);
		const auto rf = rect_to_rectd(r, scale, offset);
		const auto center = rf.center();
		color = mandelbrot_at(std::complex<double>(center.x, center.y));
	}
	~Tile() {
		for (int i = 0; i < current_lod+1; i++) {
			glDeleteTextures(1, &texture[i]);
		}
	}

	void draw(const Vec2i &tile_size, const Vec2i &offset) {
		switch (current_lod) {
		case -1:
			glBindTexture(GL_TEXTURE_2D, 0);
			glColor3ub(color.r, color.g, color.b);
			draw_quad(pos - offset, tile_size, 0, 0, 1, 1);
			glColor3ub(255, 255, 255);
			break;
		case 0:
			glBindTexture(GL_TEXTURE_2D, texture[0]);
			draw_quad(pos - offset, tile_size, 0, 0, 1, 1);
			break;
		case 1:
			glBindTexture(GL_TEXTURE_2D, texture[1]);
			draw_quad(pos - offset, tile_size, 0, 0, 1, 1);
			break;
		}
	}
};

void release_tile(Tile *t) {
	if (t->wip) {
		// somebody's working on the tile, just mark it as released
		t->released = true;
	} else {
		del_obj(t);
	}
}

// returns true if object is still alive
Task<bool> upload_texture(Tile *t, Vector<uint8_t> data, const Vec2i &size, bool finalize = false) {
	GLuint id;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.x, size.y, 0, GL_RGBA,
		      GL_UNSIGNED_BYTE, data.data());

	if (glGetError() != GL_NO_ERROR) {
		printf("failed uploading texture\n");
	}

	t->current_lod++;
	t->texture[t->current_lod] = id;
	if (finalize) {
		t->wip = false;
	}
	if (t->released) {
		del_obj(t);
		co_return false;
	}
	co_return true;
}

Task<void> build_tile(Tile *t, const Vec2i &tile_size, Vec2d scale, Vec2d offset) {
	// LOD 0
	const Rect r = Rect_WH(t->pos, tile_size);
	const RectD rf = rect_to_rectd(r, scale, offset);
	const auto data0 = mandelbrot(rf, tile_size/Vec2i(4));
	if (!co_await co_main(upload_texture(t, std::move(data0), tile_size/Vec2i(4))))
		co_return;
	// LOD 1
	const auto data1 = mandelbrot(rf, tile_size);
	(void)co_await co_main(upload_texture(t, std::move(data1), tile_size, true));
}

struct TileManager {
	Vec2i screen_offset = Vec2i(0);
	Vec2d offset = Vec2d(-1.5, -1.0);
	Vec2d scale = Vec2d(0.00235);

	// in pixels
	const Vec2i tile_size;

	Vector<Tile*> tiles;
	BitArray tile_bits;

	TileManager(const Vec2i &ts): tile_size(ts) {
	}
	~TileManager() {
		for (auto t : tiles)
			release_tile(t);
	}

	void reset(Rect *s) {
		offset = Vec2d(-1.5, -1.0);
		scale = Vec2d(0.00235);
		*s = Rect_WH(Vec2i(0), s->size());
		for (auto t : tiles)
			release_tile(t);
		tiles.clear();
		update(*s);
	}

	void zoom(Rect *s, const Vec2i &a, const Vec2i &b) {
		const auto sr = Rect(
			Vec2i(::min(a.x, b.x), ::min(a.y, b.y)),
			Vec2i(::max(a.x, b.x), ::max(a.y, b.y)));

		const auto origin = ToVec2d(s->top_left() + sr.top_left()) * scale + offset;
		const auto ratio = (float)sr.width() / s->width();
		scale *= Vec2d(ratio);
		offset = origin;
		*s = Rect_WH(Vec2i(0), s->size());

		for (auto t : tiles)
			release_tile(t);
		tiles.clear();
		update(*s);
	}

	void update(const Rect &s) {
		screen_offset = s.top_left();

		// visible size in tiles WxH
		const Vec2i vis = s.size() / tile_size + Vec2i(2);
		const int vis_area = area(vis);
		if (tile_bits.length() != vis_area)
			tile_bits = BitArray(vis_area);
		else
			tile_bits.clear();

		// base - the offset of screen in tiles, floor aligned to tile size
		const Vec2i base = floor_div(s.top_left(), tile_size);
		const Rect visrect = Rect_WH(Vec2i(0), vis);

		// go over existing tiles, release out of bounds ones, mark others in a bit array
		for (int i = 0; i < tiles.length(); i++) {
			const auto t = tiles[i];
			const Vec2i index = t->pos / tile_size - base;
			if (!contains(visrect, index)) {
				tiles.quick_remove(i--);
				release_tile(t);
			} else {
				tile_bits.set_bit(index.y * vis.x + index.x);
			}
		}

		// go over all visible tiles, add missing ones
		for (int y = 0; y < vis.y; y++) {
			for (int x = 0; x < vis.x; x++) {
				const int offset = y*vis.x+x;
				if (tile_bits.test_bit(offset))
					continue;


				// ok, we have a new tile here
				const Vec2i pos = (base + Vec2i(x, y)) * tile_size;

				const auto tile = new_obj<Tile>(pos, tile_size, this->scale, this->offset);
				tiles.append(tile);
				tile->wip = true;
				globalQueue->push(build_tile(tile, tile_size, this->scale, this->offset).coro);
			}
		}
	}

	void draw() {
		for (int i = 0; i < tiles.length(); i++) {
			tiles[i]->draw(tile_size, screen_offset);
		}
	}
};

void main_loop(SDL_Window *sdl_window, Rect &screen) {
	TileManager tm(Vec2i(128));
	tm.update(screen);

	glClearColor(0, 0, 0, 1);

	auto pan = false;
	auto select = false;
	auto selectA = Vec2i(0);
	auto selectB = Vec2i(0);
	auto panOrigin = Vec2i(0);

	auto done = false;

	Vector<CoroutineHandle> mainThreadBuf;
	while (!done) {
		if (mainThreadQueue->try_pop_all(&mainThreadBuf)) {
			for (auto c : mainThreadBuf)
				c.resume();
		}

		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			switch (e.type) {
			case SDL_WINDOWEVENT:
				if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
					screen.set_size(Vec2i(e.window.data1, e.window.data2));
					tm.update(screen);
					glViewport(0, 0, screen.width(), screen.height());
					glLoadIdentity();
					glOrtho(0, screen.width(), screen.height(), 0, -1, 1);
				}
				break;
			case SDL_KEYDOWN:
				if (e.key.keysym.sym == SDLK_ESCAPE)
					done = true;
				break;
			case SDL_MOUSEBUTTONDOWN:
				if (e.button.button == 1) {
					panOrigin = Vec2i(e.button.x, e.button.y);
					pan = true;
				} else if (e.button.button == 2) {
					tm.reset(&screen);
				} else if (e.button.button == 3) {
					selectB = selectA = Vec2i(e.button.x, e.button.y);
					select = true;
				}
				break;
			case SDL_MOUSEBUTTONUP:
				if (pan) {
					pan = false;
				}
				if (select) {
					select = false;
					tm.zoom(&screen, selectA, selectB);
				}
				break;
			case SDL_QUIT:
				done = true;
				break;
			case SDL_MOUSEMOTION:
				if (pan) {
					const Vec2i delta = Vec2i(e.motion.x, e.motion.y) - panOrigin;
					panOrigin += delta;
					screen.move(-delta);
					tm.update(screen);
				} else if (select) {
					selectB = Vec2i(e.motion.x, e.motion.y);
				}
				break;
			}
		}

		glClear(GL_COLOR_BUFFER_BIT);
		tm.draw();
		glBindTexture(GL_TEXTURE_2D, 0);
		if (select)
			draw_selection(selectA, selectB);
		SDL_GL_SwapWindow(sdl_window);
	}

	terminate_workers();
	wait_for_workers();
}

int main() {
	SDL_Init(SDL_INIT_VIDEO);

	auto screen = Rect_WH(0, 0, 1280, 720);
	auto sdl_window = SDL_CreateWindow("cppmandel",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen.width(), screen.height(), SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (!sdl_window) {
		printf("failed to create an SDL window: %s\n", SDL_GetError());
		return 1;
	}

	auto context = SDL_GL_CreateContext(sdl_window);
	if (!context) {
		printf("failed to create a GL context: %s\n", SDL_GetError());
		return 1;
	}

	SDL_GL_SetSwapInterval(1);

	glEnable(GL_TEXTURE_2D);
	glViewport(0, 0, screen.width(), screen.height());
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, screen.width(), screen.height(), 0, -1, 1);

	init_workers();

	main_loop(sdl_window, screen);

	SDL_GL_DeleteContext(context);
	SDL_DestroyWindow(sdl_window);
	SDL_Quit();
	return 0;
}
