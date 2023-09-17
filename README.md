A xclip alternative (hopefully)

### Why not xclip

You can't expose data with multiple targets, making it unfit for usage in a clipboard manager that wants to support rich text / images in web apps like google docs.

The source code for that thing has a `goto` to the middle of two while loops, and there's a switch case in the mix as well. I decided to start from scratch rather than try and understand that.

### TODO

INCR transfers bug fix -> event not coming.

Use single `XGetAtomNames` call instead of many singular calls.

