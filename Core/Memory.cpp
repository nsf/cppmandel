#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <atomic>
#include "Core/Memory.h"
#include "Core/Utils.h"
#include "Math/Utils.h"

static std::atomic<int64_t> counter_add(0);
static std::atomic<int64_t> counter_del(0);

void xtrack_add()
{
	counter_add++;
}

void xtrack_del()
{
	counter_del++;
}

void xtrack_report()
{
	const int64_t a = counter_add.load();
	const int64_t d = counter_del.load();
	printf("counter_add: %ld, counter_del: %ld, diff: %ld\n", a, d, a-d);
}

void *xmalloc(int n)
{
	void *mem = malloc(n);
	if (!mem)
		die("nextgame: out of memory");
	xtrack_add();
	return mem;
}

void xfree(void *ptr)
{
	xtrack_del();
	free(ptr);
}

int xcopy(void *dst, const void *src, int n)
{
	memmove(dst, src, n);
	return n;
}

void xclear(void *dst, int n)
{
	memset(dst, 0, n);
}

int64_t xtrack_get_add()
{
	return counter_add.load();
}

void *DefaultAllocator::allocate_bytes(int n)
{
	return xmalloc(n);
}

void DefaultAllocator::free_bytes(void *mem)
{
	xfree(mem);
}

DefaultAllocator default_allocator;

AlignedAllocator::AlignedAllocator(int n): align_to(n)
{
}

void *AlignedAllocator::allocate_bytes(int n)
{
	void *ptr;
	if (posix_memalign(&ptr, align_to, n) != 0)
		die("nextgame: out of memory (aligned: %d)", align_to);
	return ptr;
	// TODO: use _aligned_alloc(size, alignment) on windows
}

void AlignedAllocator::free_bytes(void *mem)
{
	free(mem);
	// TODO: use _aligned_free(ptr) on windows
}

PoolAllocator::PoolAllocator(int block_size): block_size(block_size)
{
	NG_ASSERT(block_size >= 4096); // smaller doesn't make sense
}

PoolAllocator::~PoolAllocator()
{
	while (current != nullptr) {
		Block *next = current->next;
		xfree(current->memory);
		::del_obj(current);
		current = next;
	}
	while (free != nullptr) {
		Block *next = free->next;
		xfree(free->memory);
		::del_obj(free);
		free = next;
	}
}

void PoolAllocator::reset()
{
	// move all "current" blocks to free blocks
	Block *p = current;
	while (p != nullptr) {
		Block *next = p->next;
		p->next = free;
		free = p;
		p = next;
	}
	current = nullptr;
	used = 0;
}

void *PoolAllocator::allocate_bytes(int n)
{
	if (current == nullptr || used + n > current->size) {
		if (free != nullptr && free->size >= n) {
			// get a block from free memory
			Block *p = free;
			free = free->next;
			p->next = current;
			current = p;
			used = n;
			return p->memory;
		} else {
			// allocate a new block
			Block *b = ::new_obj<Block>();
			b->size = max(block_size, n);
			b->memory = xmalloc(b->size);
			b->next = current;
			current = b;
			used = n;
			return b->memory;
		}
	} else {
		void *p = static_cast<uint8_t*>(current->memory) + used;
		used += n;
		return p;
	}
}

void PoolAllocator::free_bytes(void*)
{
	// do nothing, memory lives as long as pool is alive
}

void PoolAllocator::dump()
{
	printf("used: %d, current: ", used);
	for (auto *p = current; p != nullptr; p = p->next)
		printf("%d (%p) ", p->size, p->memory);
	printf("||| free: ");
	for (auto *p = free; p != nullptr; p = p->next)
		printf("%d (%p) ", p->size, p->memory);
	printf("\n");
}

AlignedAllocator sse_allocator(16);
