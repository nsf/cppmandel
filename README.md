# cppmandel

Interactive multithreaded mandelbrot set renderer implemented using experimental C++ coroutines.

For coroutines spec see: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4680.pdf

Experimental implementation is available in clang 5.0 compiler. Also in Microsoft's compiler, but I'm working on linux, never tried it.

Controls:

 - Left mouse button - pan
 - Middle mouse button - reset pan and zoom
 - Right mouse button - hold and drag to select zoom region

Build instructions:

1. Make sure clang 5.0, libc++, SDL2 and OpenGL are properly installed.

2. Use the following commands (roughly):

```
mkdir build
cd build
CC=clang CXX=clang++ cmake -DCMAKE_BUILD_TYPE=Release -GNinja ..
ninja
./cppmandel
```

