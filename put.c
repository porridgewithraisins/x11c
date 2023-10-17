#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FREAD_SIZE 4194304  // 4MiB
#define MAX_ACTIVE 512      // this is the max number of X11 windows, so we will never™ reach it
#define Failure !Success

// x11 primitives and common atoms
Display *display;
Window window;
Atom CLIPBOARD, INCR, TARGETS;

// maximum property size as per convention for this x server
// there is also a 1MB hard limit on property sizes
size_t politeChunkSize;

// data structure for a file/stream
typedef struct {
    const char *filename;
    char *contents;
    size_t length;
} Listing;

Listing *listings;
int listing_count;
Atom *targets;

// data structure for a guy that we are currently sending data incrementally to
typedef struct {
    Window requestor;
    Atom property;
    Listing *listing;
    Atom target;
    size_t offset;
    Bool _PRESENT;  // flag to indicate if this spot in the array is taken
} OngoingTransfer;
OngoingTransfer ongoing_transfers[MAX_ACTIVE] = {0};

XEvent event;

/* Reads an entire file/stream into a buffer. Does not use ftell, fseek, etc, so works on streams as well. */
Bool slurp_file(Listing *listing) {
    size_t size = 0, used = 0;
    char *buf = NULL;

    FILE *fd = fopen(listing->filename, "r");
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
    listing->contents = buf;
    listing->length = used;

    return Success;
}

void usage(const char *program) {
    fprintf(stderr, "Usage: %s target1 file1 target2 file2... Use - for stdin\n", program);
}

Bool serviceNewTransfer() {
    XSelectionRequestEvent *request = &event.xselectionrequest;

    // they requested TARGETS, so send that and return
    if (TARGETS == request->target) {
        XChangeProperty(display, request->requestor, request->property, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)targets, listing_count + 1);  // +1 as the TARGETS atom there
        return Success;
    }

    // find the listing index for the target requested
    for (int listing_idx = 0; listing_idx < listing_count; listing_idx++) {
        if (targets[listing_idx] != request->target) continue;

        // if we haven't already read the file/stream, do so
        if (!listings[listing_idx].length || !listings[listing_idx].contents) {
            if (Success != slurp_file(listings + listing_idx)) {
                fprintf(stderr, "Failed to read file %s for target %s\n", listings[listing_idx].filename,
                        XGetAtomName(display, targets[listing_idx]));
                return Failure;
            }
        }
        // we can send it all in one go without INCR
        if (listings[listing_idx].length < politeChunkSize) {
            XChangeProperty(display, request->requestor, request->property, request->target, 8, PropModeReplace,
                            listings[listing_idx].contents, listings[listing_idx].length);
            return Success;
        }

        // otherwise, we have to signal the start of an INCR transfer
        XChangeProperty(display, request->requestor, request->property, INCR, 32, PropModeReplace, NULL, 0);
        // we have to subscribe to property changes on the requestors window
        // ACKs by the requestor in the INCR protocol are indicated by deleting properties on their window
        XSelectInput(display, request->requestor, PropertyChangeMask);

        // find the first empty slot and put the initial state there
        for (int i = 0; i < MAX_ACTIVE; i++) {
            if (!ongoing_transfers[i]._PRESENT) {
                ongoing_transfers[i] = (OngoingTransfer){.requestor = request->requestor,
                                                         .property = request->property,
                                                         .listing = listings + listing_idx,
                                                         .target = targets[listing_idx],
                                                         .offset = 0,
                                                         ._PRESENT = True};
                return Success;
            }
        }

        fprintf(stderr, "All slots full, what are you even doing?\n");
        return Failure;
    }

    fprintf(stderr, "No data specified for target %s. Ignoring request\n", XGetAtomName(display, request->target));
    return Failure;
}

Bool serviceOngoingTransfer() {
    // retrieve the state we had stored earlier related to this requestor
    OngoingTransfer *state = NULL;
    for (int i = 0; i < MAX_ACTIVE; i++) {
        if (ongoing_transfers[i]._PRESENT && (ongoing_transfers[i].requestor == event.xproperty.window)) {
            state = ongoing_transfers + i;
            break;
        }
    }

    // "I don't even know who you are"
    if (!state) {
        return Failure;
    }

    // we have already sent everything
    if (state->offset >= state->listing->length) {
        XChangeProperty(display, state->requestor, state->property, state->target, 8, PropModeReplace, NULL, 0);
        *state = (OngoingTransfer){0};
        return Success;
    }

    // send the next chunk and update our offset
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
    if ((argc - 1) % 2 != 0 || argc == 1) {
        usage(argv[0]);
        return Failure;
    }

    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Could not open X display\n");
        return Failure;
    }
    window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
    CLIPBOARD = XInternAtom(display, "CLIPBOARD", False);
    INCR = XInternAtom(display, "INCR", False);
    TARGETS = XInternAtom(display, "TARGETS", False);

    listing_count = (argc - 1) / 2;
    targets = malloc((listing_count + 1) * sizeof *targets);  // +1 to store TARGETS itself
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
        return Failure;
    }

    politeChunkSize = (size_t)XExtendedMaxRequestSize(display) / 4;
    if (!politeChunkSize) politeChunkSize = (size_t)XMaxRequestSize(display) / 4;

    Bool lost_ownership = False;
    while (True) {
        // if we lost ownership, we still need to service ongoing transfers to completion
        if (lost_ownership) {
            Bool ongoing_transfers_remaining = False;
            for (int i = 0; i < MAX_ACTIVE; i++) {
                if (ongoing_transfers[i]._PRESENT) {
                    ongoing_transfers_remaining = True;
                    break;
                }
            }
            if (!ongoing_transfers_remaining) {
                break;
            }
        }

        XNextEvent(display, &event);
        switch (event.type) {
            case SelectionClear:
                lost_ownership = True;
                fprintf(stderr, "Lost ownership of the clipboard. Will quit after completing ongoing transfers\n");
                break;
            case SelectionRequest:
                if (event.xselectionrequest.selection != CLIPBOARD) break;
                if (lost_ownership) break;  // if we lost ownership, we shouldn't service new requestors

                Bool status = serviceNewTransfer();
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
                serviceOngoingTransfer();
                break;
        }
    }

    return Success;
}
