# mruby-denko-fastcanvas

This mrbgem overrides a subset of low-level drawing methods from `Denko::Display::Canvas`, speeding them up with C, but modifying the same framebuffers created in mruby, and leaving them accessible there.

## Optimized Methods:
  - #_get_pixel
  - #_set_pixel
  - #_line
  - #_rectangle
  - #_path
  - #_polygon
  - #_ellipse
  - #_draw_char
