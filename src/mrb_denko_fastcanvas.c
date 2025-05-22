#include <mruby.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/variable.h>
#include <mruby/value.h>

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
}

void
mrb_mruby_denko_fastcanvas_gem_final(mrb_state* mrb) {
}
