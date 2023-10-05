An xclip alternative, with support for pasting multiple targets.

### Why not xclip

You can't expose data with multiple targets, making it unfit for usage in a clipboard manager that wants to support rich text / images in web apps like google docs. And it has lots of additional functionality that I don't need.

### How to use

- Get from clipboard

```bash
    gcc -lX11 get.c -o getcp
    ./getcp text/plain # or any other target
    ./getcp TARGETS # to query a list of available targets
    # writes to stdout
```

What it says on the tin.

- Put in clipboard

```bash
    gcc -lX11 put.c -o putcp
    # simple, single target
    echo hello | ./putcp text/plain -
    # multiple targets
    ./putcp image/png file.png image/jpeg file.jpg text/html fragment.html
    # streams as data source
    cat file.png | ./putcp image/png - image/jpeg <(curl http://so.m/e.jpg) - text/html <(cat file.html | awk ... | grep ... | cut ...)
```

The format is
`./putcp target1 fd1 target2 fd2`. You can use `-` in place of any fd to specify stdin. Any file/stream can be given as a data source.

All the files/streams provided will be read to completion and stored in memory _at the time the corresponding target is requested_. Hence, if the target specified for a file/stream is _never_ requested, it will _never_ be read. Once read, the data will be kept in memory to serve future requests.

**You should use `& disown` in your shell if you're using `putcp` as part of a script**

In X11, clipboard is implemented as message passing between the window data is being copied from (this program) and wherever you're pasting it. So, this app will run in the background when you use `./putcp` (Until some other app becomes the copied-from window, at which point this program quits).

**There is no default target, or default source, or default anything**

Make a shell alias if you want.

