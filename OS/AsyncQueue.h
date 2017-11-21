#pragma once

#include <SDL2/SDL_mutex.h>
#include "Core/Vector.h"
#include "Core/Defer.h"

template <typename T>
struct AsyncQueue {
	SDL_mutex *mutex;
	SDL_cond *cond;
	Vector<T> queue;

	int length() const
	{
		SDL_LockMutex(mutex);
		DEFER { SDL_UnlockMutex(mutex); };
		return queue.length();
	}

	void push(const T &elem)
	{
		SDL_LockMutex(mutex);
		DEFER { SDL_UnlockMutex(mutex); };
		queue.append(elem);
		SDL_CondSignal(cond);
	}

	bool try_pop(T *out)
	{
		SDL_LockMutex(mutex);
		DEFER { SDL_UnlockMutex(mutex); };
		if (queue.length() == 0) {
			return false;
		}
		*out = queue[0];
		queue.remove(0);
		return true;
	}

	T pop()
	{
		SDL_LockMutex(mutex);
		DEFER { SDL_UnlockMutex(mutex); };
		while (queue.length() == 0)
			SDL_CondWait(cond, mutex);
		T tmp = queue[0];
		queue.remove(0);
		return tmp;
	}

	// uses all the vector, be careful and clear before use
	void pop_all(Vector<T> *out)
	{
		SDL_LockMutex(mutex);
		DEFER { SDL_UnlockMutex(mutex); };
		while (queue.length() == 0)
			SDL_CondWait(cond, mutex);
		out->resize(queue.length());
		copy(out->sub(), queue.sub());
		queue.clear();
	}

	// uses all the vector, be careful and clear before use
	bool try_pop_all(Vector<T> *out)
	{
		SDL_LockMutex(mutex);
		DEFER { SDL_UnlockMutex(mutex); };
		if (queue.length() == 0)
			return false;
		out->resize(queue.length());
		copy(out->sub(), queue.sub());
		queue.clear();
		return true;
	}

	int *queue_length_pointer()
	{
		return &queue.m_len;
	}

	AsyncQueue():
		mutex(SDL_CreateMutex()), cond(SDL_CreateCond())
	{
		NG_ASSERT(mutex != nullptr);
		NG_ASSERT(cond != nullptr);
	}

	~AsyncQueue()
	{
		SDL_DestroyMutex(mutex);
		SDL_DestroyCond(cond);
	}
};
