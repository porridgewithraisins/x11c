// gcc -lX11 copy.c
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

Display *display;
Window window;
Atom CLIPBOARD, A, TARGETS, INCR, target;
XEvent event;
Atom actualTarget;
int format;
unsigned long bytesLeft;
unsigned char *data;
unsigned long count;

int EXITCODE = Success;

Bool blockUntilDataReady()
{
    do
    {
        XNextEvent(display, &event);
    } while (event.type != SelectionNotify || event.xselection.selection != CLIPBOARD);
    return event.xselection.property != None;
}

void requestClipboardDataAs(Atom target)
{
    XConvertSelection(display, CLIPBOARD, target, A, window, CurrentTime);
}

void loadClipboardData()
{
    long maxSize = target == TARGETS ? (sizeof(Atom) * 1024) : __LONG_MAX__ / 4;
    XGetWindowProperty(display, window, A, 0, maxSize, True,
                       AnyPropertyType, &actualTarget, &format,
                       &count, &bytesLeft, &data);
}

void outputData()
{
    fwrite(data, 1, format * count / 8, stdout);
    fflush(stdout);
}

Bool ableToConvert()
{
    return event.xselection.property != None;
}

Bool chunkingRequired()
{
    return actualTarget == INCR;
}

void blockUntilNextChunkReady()
{
    do
    {
        fprintf(stderr, "DEBUG: going to wait for event\n");
        XNextEvent(display, &event);
        fprintf(stderr, "DEBUG: Event occurred\n");
    } while (event.type != PropertyNotify || event.xproperty.atom != A || event.xproperty.state != PropertyNewValue);
}

Bool hasNextChunk()
{
    return count > 0;
}

int main(const int argc, const char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <target you want to request>\n", argv[0]);
        return !Success;
    }

    display = XOpenDisplay(NULL);
    if (!display)
    {
        fprintf(stderr, "Could not open X display\n");
        EXITCODE = !Success;
        goto cleanup;
    }

    window = XCreateSimpleWindow(display, DefaultRootWindow(display), -10, -10, 1, 1, 0, 0, 0);
    CLIPBOARD = XInternAtom(display, "CLIPBOARD", False);
    A = XInternAtom(display, "A", False);
    TARGETS = XInternAtom(display, "TARGETS", False);
    INCR = XInternAtom(display, "INCR", False);
    target = XInternAtom(display, argv[1], False);

    requestClipboardDataAs(target);
    if (!blockUntilDataReady())
    {
        fprintf(stderr, "Failed to convert selection to target %s\n", argv[1]);
        EXITCODE = !Success;
        goto cleanup;
    }
    loadClipboardData();

    if (target == TARGETS)
    {
        Atom *targetList = (Atom *)data;
        for (unsigned long i = 0; i < count; i++)
        {
            char *targetName = XGetAtomName(display, targetList[i]);
            if (targetName)
            {
                printf("%s\n", targetName);
            }
        }
    }
    else if (chunkingRequired())
    {
        fprintf(stderr, "DEBUG: chunking required\n");
        do
        {
            fprintf(stderr, "DEBUG: There is a chunk remaining\n");
            blockUntilNextChunkReady();
            fprintf(stderr, "DEBUG: chunk is ready\n");
            loadClipboardData();
            fprintf(stderr, "DEBUG: chunk is loaded\n");
            outputData();
        } while (hasNextChunk());
    }
    else
    {
        outputData();
    }

cleanup:
    XCloseDisplay(display);
    return EXITCODE;
}