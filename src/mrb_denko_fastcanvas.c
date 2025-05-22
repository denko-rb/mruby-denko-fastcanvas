#include <mruby.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/variable.h>
#include <mruby/value.h>
#include <stdlib.h>

// C struct to avoid constantly getting ivars.
typedef struct {
  mrb_value framebuffers;
  mrb_int colors;
  mrb_int columns;
  mrb_int rows;
  mrb_int x_max;
  mrb_int y_max;
  mrb_bool invert_x;
  mrb_bool invert_y;
  mrb_bool swap_xy;
} canvas_t;

// Get the ivars from the ruby Canvas once.
static void
mrb_get_canvas_data(mrb_state* mrb, mrb_value self, canvas_t* canvas) {
  canvas->framebuffers = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@framebuffers"));
  canvas->colors       = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@colors")));
  canvas->columns      = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@columns")));
  canvas->rows         = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@rows")));
  canvas->x_max        = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@x_max")));
  canvas->y_max        = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@y_max")));
  canvas->invert_x     = mrb_bool(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@invert_x")));
  canvas->invert_y     = mrb_bool(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@invert_y")));
  canvas->swap_xy      = mrb_bool(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@swap_xy")));
}

//
// #pixel
//
static void
c_canvas_pixel(mrb_state* mrb, canvas_t* c, int x, int y, int color) {
  // Reverse current canvas transformations.
  mrb_int xt = (c->invert_x) ? c->x_max - x : x;
  mrb_int yt = (c->invert_y) ? c->y_max - y : y;
  if (c->swap_xy) {
    mrb_int tt;
    tt = xt;
    xt = yt;
    yt = tt;
  }

  // Bounds check
  if ((xt < 0) || (xt >= c->columns) || (yt < 0) || (yt >= c->rows)) return;
  if ((color < 0) || (color > c->colors)) return;

  mrb_int byte_index = ((yt / 8) * c->columns) + xt;
  uint8_t bit = yt % 8;

  // Colors are 1-indexed so "0" means blank/clear.
  for(int i=1; i <= c->colors; i++) {
    mrb_value fb = mrb_ary_ref(mrb, c->framebuffers, i-1);
    mrb_value mrb_fb_byte = mrb_ary_ref(mrb, fb, byte_index);
    mrb_int fb_byte = mrb_fixnum(mrb_fb_byte);

    // Set pixel in fb for given color
    if (i == color) {
      fb_byte = fb_byte | (0b1 << bit);
    // Clear in other colors
    } else {
      fb_byte = fb_byte & ~(0b1 << bit);
    }
    mrb_ary_set(mrb, fb, byte_index, mrb_fixnum_value(fb_byte));
  }
}

static mrb_value
mrb_canvas_pixel(mrb_state* mrb, mrb_value self) {
  mrb_int x, y;
  mrb_value kwargs;
  mrb_get_args(mrb, "iiH", &x, &y, &kwargs);

  mrb_value mrb_color = mrb_hash_fetch(mrb, kwargs, mrb_symbol_value(mrb_intern_lit(mrb, "color")), mrb_fixnum_value(0));
  mrb_int color = mrb_fixnum(mrb_color);

  canvas_t canvas;
  mrb_get_canvas_data(mrb, self, &canvas);

  c_canvas_pixel(mrb, &canvas, x, y, color);
  return mrb_nil_value();
}

//
// #line
//
static void
c_canvas_line(mrb_state* mrb, canvas_t* c, int x1, int y1, int x2, int y2, int color) {
  // Deltas in each axis.
  int dy = y2 - y1;
  int dx = x2 - x1;

  // Optimize vertical lines and avoid division by 0.
  if (dx == 0) {
    // Ensure y1 < y2.
    if (y2 < y1) {
      int t = y1;
      y1 = y2;
      y2 = t;
    }
    for(int y=y1; y<=y2; y++) {
      c_canvas_pixel(mrb, c, x1, y, color);
    }
    return mrb_nil_value();
  }

  // Optimize horizontal lines.
  if (dy == 0) {
    // Ensure x1 < x2.
    if (x2 < x1) {
      int t = x1;
      x1 = x2;
      x2 = t;
    }
    for(int x=x1; x<=x2; x++) {
      c_canvas_pixel(mrb, c, x, y1, color);
    }
    return mrb_nil_value();
  }

  // Bresenham's algorithm for sloped lines.
  //
  // Slope calculations
  int dx_abs      = abs(dx);
  int dy_abs      = abs(dy);
  int step_axis   = (dx_abs > dy_abs) ? 0 : 1; // 0 = x, 1 = y
  int step_count  = (step_axis == 0)  ? dx_abs : dy_abs;
  int x_step      = (dx > 0) ? 1 : -1;
  int y_step      = (dy > 0) ? 1 : -1;

  // Error calculations
  int error_step      = (step_axis == 0) ? dy_abs : dx_abs;
  int error_threshold = (step_axis == 0) ? dx_abs : dy_abs;

  int x = x1;
  int y = y1;
  int error = 0;
  for (int i=0; i<=step_count; i++) {
    c_canvas_pixel(mrb, c, x, y, color);

    if (step_axis == 0) { // Step on x-axis
      x += x_step;
      error += error_step;
      if (error >= error_threshold) {
        y += y_step;
        error -= error_threshold;
      }
    } else { // Step on y-axis
      y += y_step;
      error += error_step;
      if (error >= error_threshold) {
        x += x_step;
        error -= error_threshold;
      }
    }
  }
}

static mrb_value
mrb_canvas_line(mrb_state* mrb, mrb_value self) {
  mrb_int x1, y1, x2, y2;
  mrb_value kwargs;
  mrb_get_args(mrb, "iiiiH", &x1, &y1, &x2, &y2, &kwargs);

  mrb_value mrb_color = mrb_hash_fetch(mrb, kwargs, mrb_symbol_value(mrb_intern_lit(mrb, "color")), mrb_fixnum_value(0));
  mrb_int color = mrb_fixnum(mrb_color);

  canvas_t canvas;
  mrb_get_canvas_data(mrb, self, &canvas);

  c_canvas_line(mrb, &canvas, x1, y1, x2, y2, color);
  return mrb_nil_value();
}

void
mrb_mruby_denko_fastcanvas_gem_init(mrb_state* mrb) {
  // Denko module
  struct RClass *mrb_Denko = mrb_define_module(mrb, "Denko");

  // Denko::Display module
  struct RClass *mrb_Denko_Display = mrb_define_module_under(mrb, mrb_Denko, "Display");

  // Denko::Display::Canvas class
  struct RClass *mrb_Canvas = mrb_define_class_under(mrb, mrb_Denko_Display, "Canvas", mrb->object_class);

  // Optimized pixel methods for Canvas
  mrb_define_method(mrb, mrb_Canvas, "pixel",       mrb_canvas_pixel,       MRB_ARGS_REQ(3));
  mrb_define_method(mrb, mrb_Canvas, "line",        mrb_canvas_line,        MRB_ARGS_REQ(4));
}

void
mrb_mruby_denko_fastcanvas_gem_final(mrb_state* mrb) {
}
