#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>

Display *display;
Window window;
Atom CLIPBOARD, A, INCR, target;
XEvent event;
Atom actualTarget;
int format;
unsigned long bytesLeft, count;
unsigned char *data;

int bitsToBytes(int bits) {
    if (bits == 32) return sizeof(long);
    if (bits == 16) return sizeof(short);
    if (bits == 8) return 1;
    return 0;
}

void output() {
    switch (actualTarget) {
        case XA_ATOM:
            Atom *targets = (Atom *)data;
            for (unsigned long i = 0; i < count; i++) {
                printf("%s\n", XGetAtomName(display, targets[i]));
            }
            break;
        case XA_INTEGER:
            long *nums = (long *)data;
            unsigned long num_longs = count * bitsToBytes(format) / sizeof *nums;
            for (int i = 0; i < num_longs; i++) {
                printf("%ld\n", nums[i]);
            }
            break;
        default:
            fwrite(data, 1, count * bitsToBytes(format), stdout);
    }
}

int paste(const int argc, const char *const argv[]) {
    if (argc < 2) {
        fprintf(stderr,
                "Usage: %s <target you want to request e.g text/plain, text/html, image/png>\n"
                "Use TARGETS to query the full list of available targets.\n ",
                argv[0]);
        return 1;
    }

    display = XOpenDisplay(NULL);
    window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
    CLIPBOARD = XInternAtom(display, "CLIPBOARD", False);
    A = XInternAtom(display, "A", False);
    INCR = XInternAtom(display, "INCR", False);
    target = XInternAtom(display, argv[1], False);

    XConvertSelection(display, CLIPBOARD, target, A, window, CurrentTime);
    do {
        XNextEvent(display, &event);
    } while (event.type != SelectionNotify || event.xselection.selection != CLIPBOARD);
    if (event.xselection.property == None) {
        fprintf(stderr, "Failed to convert selection to target %s\n", argv[1]);
        return 1;
    }
    XGetWindowProperty(display, window, A, 0, 0, False, AnyPropertyType, &actualTarget, &format, &count, &bytesLeft,
                       &data);
    if (actualTarget != INCR) {
        XGetWindowProperty(display, window, A, 0, bytesLeft, True, AnyPropertyType, &actualTarget, &format, &count,
                           &bytesLeft, &data);
        output();
        return 0;
    }
    XDeleteProperty(display, window, A);
    XSelectInput(display, window, PropertyChangeMask);
    while (True) {
        do {
            XNextEvent(display, &event);
        } while (event.type != PropertyNotify || event.xproperty.state != PropertyNewValue);

        XGetWindowProperty(display, window, A, 0, 0, False, AnyPropertyType, &actualTarget, &format, &count, &bytesLeft,
                           &data);
        if (bytesLeft == 0) {
            XDeleteProperty(display, window, A);
            break;
        }

        XGetWindowProperty(display, window, A, 0, bytesLeft, True, AnyPropertyType, &actualTarget, &format, &count,
                           &bytesLeft, &data);
        output();
    }
    return 0;
}