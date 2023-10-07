#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

int main() {
    Display *disp = XOpenDisplay(NULL);
    if (!disp) return 1;
    XFixesSelectSelectionInput(disp, DefaultRootWindow(disp), XInternAtom(disp, "CLIPBOARD", False),
                               XFixesSetSelectionOwnerNotifyMask);
    XEvent evt;
    XNextEvent(disp, &evt);
    return 0;
}