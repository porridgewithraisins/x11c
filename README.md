An xclip alternative, with support for pasting multiple targets.

Only touches the main (ctrl+c, right-click + copy) clipboard, doesn't touch the primary selection (middle click) or secondary or the cut buffer.

### Why not xclip

You can't expose data with multiple targets, making it unfit for usage in a clipboard manager that wants to support rich text / images in web apps like google docs. And it has lots of additional functionality that I don't need.

### How to get
```bash
git clone https://github.com/porridgewithraisins/x11cp
cd x11cp
```

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
`./putcp target1 fd1 target2 fd2...`. You can use `-` in place of any fd to specify stdin. Any file/stream can be given as a data source.

Each file/stream provided will be read to completion and stored in memory _at the time the corresponding target is requested_. Hence, if the target specified is _never_ requested, it will _never_ be read. Once read, the data will be kept in memory to serve future requests.

### You should use `& disown` in your shell if you're using `putcp` as part of a script
(Or if you want your shell prompt back)

In X11, clipboard is implemented as message passing between the window data is being copied from (this program) and wherever you're pasting it. So, `./putcp` has to run it's event loop until some other window gets copied from (xclip uses `fork()` to background the event loop).

### There is no default target, or default source, or default anything

Make a shell alias if you want.

### For the initiated

This follows the protocol well
 - It supports INCR
 - Finishes serving all ongoing transfers even when the selection owner changes
