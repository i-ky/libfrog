# libfrog
Library allowing different parts of application (application binary itself, its dependencies and libraries loaded in runtime) to use same symbols from different libraries.

**Fun fact**:
project is named after
[the part of railroad junction](https://en.wikipedia.org/wiki/Railroad_switch#Frog_(common_crossing))
allowing wheels to pass the crossing point of two rails.

## problem

Imagine there are two libraries `libx.so` and `liby.so`
which in turn depend on libraries `libxz.so` and `libyz.so` respectively.
Imagine both `libxz.so` and `libyz.so` provide function `z()`
and both `libx.so` and `liby.so` use `z()`.
But `z()` of `libxz.so` and `z()` of `libyz.so` are very different and must not be used instead of one another!

Unfortunately, if we try to use both `libx.so` and `liby.so` in one application,
one of them
(that comes later in the linking order)
will get symbol `z()` resolved incorrectly
which in turn will lead to incorrect behavior or even crashes.
The issue is trivial to remedy if we have access to the sources of these libraries or can replace them with an alternative which does not use `z()`. But what if that is not an option?

## solution

This project uses
[auditing API for the dynamic linker](http://man7.org/linux/man-pages/man7/rtld-audit.7.html)
to fix incorrect symbol bindings on **without any changes** to the binaries.
All you need to do is to find out which libraries get incorrectly linked and specify them in [YAML](http://yaml.org) configuration file.
```yaml
libx.so:
  libyz.so: libxz.so
```
This notation means that `libx.so` gets incorrectly linked to `libyz.so` and should be instead linked to `libxz.so`.
You can specify as many libraries and as many replacement pairs as needed:
```yaml
lib-1.so:
  lib-1-1.so: lib-1-a.so
  lib-1-2.so: lib-1-b.so
  # ...
lib-2.so:
  lib-2-1.so: lib-2-1-a.so
  # ...
# ...
```

Next step is to install `libyaml` and compile `libfrog`:
```
$ sudo apt install libyaml-dev
$ cc -shared -fPIC frog.c -o libfrog.so -ldl -lyaml
```

And now you set `LD_AUDIT` variable to point to `libfrog.so`, `LIBFROG_CONFIG` variable to point to configuration file and launch the application:
```
$ export LD_AUDIT=/path/to/libfrog.so
$ export LIBFROG_CONFIG=/path/to/config.yml
$ ./a.out
```

## demo

Here is how header files and source code of these libraries could look like.

library   | header          | source
----------|-----------------|-----------------
`libx.so` |[x.h](demo/x.h)  |[x.c](demo/x.c)
`liby.so` |[y.h](demo/y.h)  |[y.c](demo/y.c)
`libxz.so`|[xz.h](demo/xz.h)|[xz.c](demo/xz.c)
`libyz.so`|[yz.h](demo/yz.h)|[yz.c](demo/yz.c)

To compile them do the following:
```
$ cd demo
$ cc xz.c -fPIC -shared -o libxz.so
$ cc yz.c -fPIC -shared -o libyz.so
$ cc x.c -L. -lxz -fPIC -shared -o libx.so
$ cc y.c -L. -lyz -fPIC -shared -o liby.so
```

And here is an example of how they can be used one by one.
[test-x.c](demo/test-x.c):
```
$ LD_LIBRARY_PATH=. cc test-x.c -L. -lx -o test-x
$ LD_LIBRARY_PATH=. ./test-x
foo
```
[test-y.c](demo/test-y.c):
```
$ LD_LIBRARY_PATH=. cc test-y.c -L. -ly -o test-y
$ LD_LIBRARY_PATH=. ./test-y
bar
```

Note that printing either `foo` or `bar` is actually the responsibility of one of `z()` functions.

If we call `x()` and then `y()` we would get `foobar`, right?
Let's try with [test-xy.c](demo/test-xy.c):
```
$ LD_LIBRARY_PATH=. cc test-xy.c -L. -lx -ly -o test-xy
$ LD_LIBRARY_PATH=. ./test-xy
foofoo
```
Wait... What?
Let's change the order we link `libx.so` and `liby.so`:
```
$ LD_LIBRARY_PATH=. cc test-xy.c -L. -ly -lx -o test-yx
$ LD_LIBRARY_PATH=. ./test-yx
barbar
```

Whichever library (`libxz.so` or `libyz.so`) gets loaded first
adds a symbol `z()` to the global namespace
and whichever library needs symbol `z()` (`libx.so` or `liby.so`)
has to use that symbol,
regardless of the fact
that it could be incorrect symbol for that library.

With the help of `libfrog.so` and [test.yaml](demo/test.yml) the situation can be fixed **without modifying** binaries in any way:
```
$ LD_AUDIT=../libfrog.so LIBFROG_CONFIG=test.yaml LD_LIBRARY_PATH=. ./test-xy
foobar
```
```
$ LD_AUDIT=../libfrog.so LIBFROG_CONFIG=test.yaml LD_LIBRARY_PATH=. ./test-yx
foobar
```
