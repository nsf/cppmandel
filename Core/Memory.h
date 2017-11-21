#pragma once

#include <new>
#include <utility>
#include <type_traits>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include "Core/Utils.h"

void *xmalloc(int n);
void xfree(void *ptr);
int xcopy(void *dst, const void *src, int n);
void xclear(void *dst, int n);
void xtrack_add();
void xtrack_del();
void xtrack_report();
int64_t xtrack_get_add();

inline int align(int n, int a)
{
	return (n + a - 1) - (n + a - 1) % a;
}

template <typename T, typename ...Args>
T *new_obj(Args &&...args)
{
	T *ptr = (T*)xmalloc(sizeof(T));
	return new (ptr) T(std::forward<Args>(args)...);
}

template <typename T>
void del_obj(T *ptr)
{
	ptr->~T();
	xfree(ptr);
}

template <typename T>
T *new_obj_array(int n)
{
	T *ptr = (T*)xmalloc(sizeof(T) * n);
	for (int i = 0; i < n; i++)
		new (ptr + i) T;
	return ptr;
}

template <typename T>
void del_obj_array(T *arr, int n)
{
	for (int i = 0; i < n; i++)
		arr[i].~T();
	xfree(arr);
}

template <typename T>
T *allocate_memory(int n = 1)
{
	return (T*)xmalloc(sizeof(T) * n);
}

template <typename T>
T &allocate_memory(T *&ptr)
{
	ptr = (T*)xmalloc(sizeof(T));
	return *ptr;
}

template <typename T>
void free_memory(T *ptr)
{
	if (ptr) xfree(ptr);
}

template <typename T>
int copy_memory(T *dst, const T *src, int n = 1)
{
	return xcopy(dst, src, sizeof(T) * n);
}

template <typename T>
void copy_memory_fast(T *dst, const T *src, int n = 1)
{
	std::memcpy(dst, src, sizeof(T) * n);
}

template <typename T>
void clear_memory(T *dst, int n = 1)
{
	xclear(dst, sizeof(T)*n);
}

struct Allocator {
	virtual void *allocate_bytes(int n) = 0;
	virtual void free_bytes(void *mem) = 0;

	template <typename T>
	T *allocate_memory(int n = 1)
	{
		return (T*)allocate_bytes(sizeof(T) * n);
	}

	template <typename T>
	T &allocate_memory(T *&ptr)
	{
		ptr = (T*)allocate_bytes(sizeof(T));
		return *ptr;
	}

	template <typename T>
	void free_memory(T *ptr)
	{
		if (ptr) free_bytes(ptr);
	}

	template <typename T, typename ...Args>
	T *new_obj(Args &&...args)
	{
		T *ptr = this->allocate_memory<T>();
		return new (ptr) T(std::forward<Args>(args)...);
	}

	template <typename T>
	void del_obj(T *ptr)
	{
		if (ptr) {
			ptr->~T();
			this->free_memory(ptr);
		}
	}
};

struct DefaultAllocator : Allocator {
	void *allocate_bytes(int n) override;
	void free_bytes(void *mem) override;
};

extern DefaultAllocator default_allocator;

struct AlignedAllocator : Allocator {
private:
	int align_to;

public:
	AlignedAllocator(int n);
	void *allocate_bytes(int n) override;
	void free_bytes(void *mem) override;
};

struct PoolAllocator : Allocator {
private:
	int block_size;
	struct Block {
		int size;
		void *memory;
		Block *next;
	};
	int used = 0;
	Block *current = nullptr;
	Block *free = nullptr;

public:
	PoolAllocator(int block_size = 4096);
	~PoolAllocator();

	void reset();
	void *allocate_bytes(int n) override;
	void free_bytes(void *mem) override;
	void dump();
};

template <size_t DesiredSize>
struct FreeListAllocator : Allocator {
private:
	static constexpr size_t Size = DesiredSize < sizeof(void*) ? sizeof(void*) : DesiredSize;
	struct FreeList {
		FreeList *next;
	};
	FreeList *list = nullptr;

public:
	void *allocate_bytes(int n) override
	{
		NG_ASSERT(n == Size);
		if (list != nullptr) {
			FreeList *mem = list;
			list = list->next;
			return mem;
		}
		return xmalloc(Size);
	}
	void free_bytes(void *mem) override
	{
		FreeList *item = (FreeList*)mem;
		item->next = list;
		list = item;
	}
};

// Allocator relies on thread-local and global data, so its instance is
// stateless, but it uses global state.
struct ShortLivedAllocator : Allocator {
	void *allocate_bytes(int n) override;
	void free_bytes(void *mem) override;
	void dump();
	void dump(void *ptr);
};

// aligned to 16 bytes
extern AlignedAllocator sse_allocator;
extern ShortLivedAllocator short_lived_allocator;
