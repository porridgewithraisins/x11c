#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>

// x11 primitives and common atoms
Display *display;
Window window;
Atom CLIPBOARD, A, INCR, target;

XEvent event;

// containers for the data sent
Atom actualTarget;
int format;
size_t bytesLeft, count;
unsigned char *data;

int bitsToBytes(int bits) {
    if (bits == 32) return sizeof(long);
    if (bits == 16) return sizeof(short);
    if (bits == 8) return 1;
    return 0;
}

// print it out depending on the type of data
// I don't know who even sends integers without ascii encoding them, but whatever I guess
void output() {
    switch (actualTarget) {
        case XA_ATOM:
            Atom *targets = (Atom *)data;
            for (size_t i = 0; i < count; i++) {
                printf("%s\n", XGetAtomName(display, targets[i]));
            }
            break;
        case XA_INTEGER:
            long *nums = (long *)data;
            size_t num_longs = count * bitsToBytes(format) / sizeof *nums;
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
        fprintf(stderr, "Usage: %s <target you want to request, or `TARGETS` to query the list of available targets>\n",
                argv[0]);
        return 1;
    }

    display = XOpenDisplay(NULL);
    window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
    CLIPBOARD = XInternAtom(display, "CLIPBOARD", False);
    A = XInternAtom(display, "A", False);
    INCR = XInternAtom(display, "INCR", False);
    target = XInternAtom(display, argv[1], False);

    // request data for the target `target` and wait for a response
    XConvertSelection(display, CLIPBOARD, target, A, window, CurrentTime);
    do {
        XNextEvent(display, &event);
    } while (event.type != SelectionNotify || event.xselection.selection != CLIPBOARD);

    // the target is not available
    if (event.xselection.property == None) {
        fprintf(stderr, "Failed to convert selection to target %s\n", argv[1]);
        return 1;
    }

    // get the data
    XGetWindowProperty(display, window, A, 0, 0, False, AnyPropertyType, &actualTarget, &format, &count, &bytesLeft,
                       &data);

    // we got it all in one go
    if (actualTarget != INCR) {
        XGetWindowProperty(display, window, A, 0, bytesLeft, True, AnyPropertyType, &actualTarget, &format, &count,
                           &bytesLeft, &data);
        // print out the data
        output();
        return 0;
    }

    // start the INCR transfer
    XDeleteProperty(display, window, A);
    // we need to listen to when our property is changed
    XSelectInput(display, window, PropertyChangeMask);

    while (True) {
        do {
            XNextEvent(display, &event);
        } while (event.type != PropertyNotify || event.xproperty.state != PropertyNewValue);

        XGetWindowProperty(display, window, A, 0, 0, False, AnyPropertyType, &actualTarget, &format, &count, &bytesLeft,
                           &data);

        // transfer is complete
        if (bytesLeft == 0) {
            XDeleteProperty(display, window, A);
            break;
        }

        XGetWindowProperty(display, window, A, 0, bytesLeft, True, AnyPropertyType, &actualTarget, &format, &count,
                           &bytesLeft, &data);
        // print out this chunk
        output();
    }
    return 0;
}