#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FREAD_SIZE
#define FREAD_SIZE 4194304
#endif

#define Failure !Success

#define MAX_ACTIVE 512  // this is the max number of X11 clients, so we will never™ reach it

Display *display;
Window window;
Atom CLIPBOARD, INCR, TARGETS;
Atom *targets;
size_t politeChunkSize;

typedef struct {
    const char *filename;
    char *contents;
    size_t length;
} Listing;
Listing *listings;
int listing_count;

typedef struct {
    Window requestor;
    Atom property;
    Listing *listing;
    Atom target;
    size_t offset;
    Bool _PRESENT;
} ActiveRequestor;

ActiveRequestor active_requestors[MAX_ACTIVE] = {0};

Bool lost_ownership = False;
XEvent event;

Bool slurp_file(Listing *fc) {
    size_t size = 0, used = 0;
    char *buf = NULL;

    FILE *fd = fopen(fc->filename, "r");
    if (!fd) return Failure;

    while (True) {
        if (used + FREAD_SIZE + 1 > size) {
            size = used + FREAD_SIZE + 1;
            if (size <= used) {  // overflow
                if (buf) free(buf);
                return Failure;
            }
            char *temp = buf;
            if (!(buf = realloc(buf, size))) {
                free(temp);
                return Failure;
            }
        }

        size_t n = fread(buf + used, 1, FREAD_SIZE, fd);
        if (n == 0) break;
        used += n;
    }

    if (ferror(fd)) {
        free(buf);
        return Failure;
    }

    char *temp = buf;
    if (!(buf = realloc(buf, used + 1))) {
        free(temp);
        return Failure;
    }

    buf[used] = '\0';
    fc->contents = buf;
    fc->length = used;

    return Success;
}

void usage(const char *program) {
    fprintf(stderr, "Usage: %s target1 file1 target2 file2... Use - for stdin\n", program);
}

Bool serviceNewRequestor() {
    XSelectionRequestEvent *request = &event.xselectionrequest;
    if (TARGETS == request->target) {
        XChangeProperty(display, request->requestor, request->property, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)targets, listing_count + 1);
        return Success;
    }

    int i;
    for (i = 0; i < listing_count; i++) {
        if (targets[i] == request->target) break;
    }

    if (!listings[i].length || !listings[i].contents) {
        if (Success != slurp_file(listings + i)) {
            fprintf(stderr, "Failed to read file %s\n", listings[i].filename);
            return Failure;
        }
    }

    if (listings[i].length < politeChunkSize) {
        XChangeProperty(display, request->requestor, request->property, request->target, 8, PropModeReplace,
                        (unsigned char *)listings[i].contents, listings[i].length);
        return Success;
    }

    // have to start INCR transfer
    XChangeProperty(display, request->requestor, request->property, INCR, 32, PropModeReplace, NULL, 0);
    XSelectInput(display, request->requestor, PropertyChangeMask);

    for (int i = 0; i < MAX_ACTIVE; i++) {
        if (!active_requestors[i]._PRESENT) {
            active_requestors[i] = (ActiveRequestor){.requestor = request->requestor,
                                                     .property = request->property,
                                                     .listing = listings + i,
                                                     .target = targets[i],
                                                     .offset = 0,
                                                     ._PRESENT = True};
            return Success;
        }
    }
    return Failure;
}

Bool serviceActiveRequestor() {
    ActiveRequestor *state = NULL;
    for (int i = 0; i < MAX_ACTIVE; i++) {
        if (active_requestors[i]._PRESENT && (active_requestors[i].requestor == event.xproperty.window)) {
            state = active_requestors + i;
            break;
        }
    }
    if (!state) {
        return Failure;
    }

    if (state->offset >= state->listing->length) {
        XChangeProperty(display, state->requestor, state->property, state->target, 8, PropModeReplace, NULL, 0);
        *state = (ActiveRequestor){0};
        return Success;
    }

    size_t chunk_size = politeChunkSize;
    if (state->offset + chunk_size > state->listing->length) {
        chunk_size = state->listing->length - state->offset;
    }

    XChangeProperty(display, state->requestor, state->property, state->target, 8, PropModeReplace,
                    state->listing->contents + state->offset, chunk_size);

    state->offset += chunk_size;

    return Success;
}

int main(const int argc, const char *const argv[]) {
    if ((argc - 1) % 2 != 0) {
        usage(argv[0]);
        return 1;
    }

    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Could not open X display\n");
        return 1;
    }
    window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
    CLIPBOARD = XInternAtom(display, "CLIPBOARD", False);
    INCR = XInternAtom(display, "INCR", False);
    TARGETS = XInternAtom(display, "TARGETS", False);

    listing_count = (argc - 1) / 2;
    targets = malloc((listing_count + 1) * sizeof *targets);
    listings = malloc(listing_count * sizeof *listings);
    for (int i = 1, j = 0; i < argc, j < listing_count; i += 2, j++) {
        const char *filename = strcmp(argv[i + 1], "-") == 0 ? "/dev/stdin" : argv[i + 1];
        listings[j] = (Listing){.filename = filename, .contents = NULL, .length = 0};
        targets[j] = XInternAtom(display, argv[i], False);
    }
    targets[listing_count] = TARGETS;

    XSetSelectionOwner(display, CLIPBOARD, window, CurrentTime);
    if (XGetSelectionOwner(display, CLIPBOARD) != window) {
        fprintf(stderr, "Failed to acquire ownership of clipboard\n");
        return 1;
    }

    politeChunkSize = (size_t)XExtendedMaxRequestSize(display) / 4;
    if (!politeChunkSize) politeChunkSize = (size_t)XMaxRequestSize(display) / 4;

    while (True) {
        if (lost_ownership) {
            Bool active_requestors_remaining = False;
            for (int i = 0; i < MAX_ACTIVE; i++) {
                if (active_requestors[i]._PRESENT) {
                    active_requestors_remaining = True;
                    break;
                }
            }
            if (!active_requestors_remaining) break;
        }
        XNextEvent(display, &event);
        switch (event.type) {
            case SelectionClear:
                lost_ownership = True;
                break;
            case SelectionRequest:
                if (event.xselectionrequest.selection != CLIPBOARD) break;
                Bool status = serviceNewRequestor();
                XSendEvent(
                    display, event.xselectionrequest.requestor, False, NoEventMask,
                    &(XEvent){.xselection = {.display = display,
                                             .type = SelectionNotify,
                                             .requestor = event.xselectionrequest.requestor,
                                             .selection = event.xselectionrequest.selection,
                                             .target = event.xselectionrequest.target,
                                             .property = (status == Success ? event.xselectionrequest.property : None),
                                             .time = event.xselectionrequest.time}});
                break;
            case PropertyNotify:
                if (event.xproperty.state != PropertyDelete) break;
                serviceActiveRequestor();
                break;
        }
    }

    return 0;
}