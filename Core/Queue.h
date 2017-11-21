#pragma once

#include "Core/Vector.h"

// too lazy to make it fancy right now, let's it with two stacks
template <typename T>
struct Queue {
	Vector<T> s1;
	Vector<T> s2;

	int length() const { return s1.length() + s2.length(); }
	void push(const T &item) { s1.append(item); }
	void push(T &&item) { s1.append(std::move(item)); }

	T pop()
	{
		if (s2.length() == 0) {
			int it = s1.length();
			if (it == 0)
				die("Queue: empty queue, please check length before popping");

			while (--it >= 0)
				s2.append(std::move(s1[it]));
			s1.clear();
		}
		T result = std::move(s2.last());
		s2.remove(s2.length()-1);
		return result;
	}

	T &first() { return (s2.length() != 0) ? s2.last() : s1.first(); }
	T &last() { return (s2.length() != 0) ? s2.first() : s1.last(); }
	const T &first() const { return (s2.length() != 0) ? s2.last() : s1.first(); }
	const T &last() const { return (s2.length() != 0) ? s2.first() : s1.last(); }
};
