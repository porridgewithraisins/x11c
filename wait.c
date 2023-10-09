#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <stdio.h>

int main() {
    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    Atom CLIPBOARD = XInternAtom(display, "CLIPBOARD", False);

    XFixesSelectSelectionInput(display, DefaultRootWindow(display), CLIPBOARD, XFixesSetSelectionOwnerNotifyMask);

    XEvent evt;
    XNextEvent(display, &evt);
    printf("%lu\n", XGetSelectionOwner(display, CLIPBOARD));
}