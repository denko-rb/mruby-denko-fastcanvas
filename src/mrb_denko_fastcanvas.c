#include <mruby.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/variable.h>
#include <mruby/value.h>
#include <mruby/string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

// C struct to avoid constantly getting ivars.
typedef struct {
  mrb_value framebuffers;
  mrb_int   colors;
  mrb_int   columns;
  mrb_int   rows;
  mrb_int   x_max;
  mrb_int   y_max;
  mrb_bool  invert_x;
  mrb_bool  invert_y;
  mrb_bool  swap_xy;
  mrb_int   current_color;
} canvas_t;

// Get the ivars from the ruby Canvas once.
static void
mrb_get_canvas_data(mrb_state* mrb, mrb_value self, canvas_t* canvas) {
  canvas->framebuffers  = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@framebuffers"));
  canvas->colors        = mrb_fixnum(mrb_iv_get(mrb, self,  mrb_intern_lit(mrb, "@colors")));
  canvas->columns       = mrb_fixnum(mrb_iv_get(mrb, self,  mrb_intern_lit(mrb, "@columns")));
  canvas->rows          = mrb_fixnum(mrb_iv_get(mrb, self,  mrb_intern_lit(mrb, "@rows")));
  canvas->x_max         = mrb_fixnum(mrb_iv_get(mrb, self,  mrb_intern_lit(mrb, "@x_max")));
  canvas->y_max         = mrb_fixnum(mrb_iv_get(mrb, self,  mrb_intern_lit(mrb, "@y_max")));
  canvas->invert_x      = mrb_bool(mrb_iv_get(mrb, self,    mrb_intern_lit(mrb, "@invert_x")));
  canvas->invert_y      = mrb_bool(mrb_iv_get(mrb, self,    mrb_intern_lit(mrb, "@invert_y")));
  canvas->swap_xy       = mrb_bool(mrb_iv_get(mrb, self,    mrb_intern_lit(mrb, "@swap_xy")));
  canvas->current_color = mrb_fixnum(mrb_iv_get(mrb, self,  mrb_intern_lit(mrb, "@current_color")));
}

//
// #clear
//
static mrb_value
mrb_canvas_clear(mrb_state* mrb, mrb_value self) {
  // Get canvas ivars
  canvas_t canvas;
  mrb_get_canvas_data(mrb, self, &canvas);

  for(int i=1; i <= canvas.colors; i++) {
    mrb_value fb = mrb_ary_ref(mrb, canvas.framebuffers, i-1);
    uint8_t* fb_data = (uint8_t*)RSTRING_PTR(fb);
    mrb_int fb_size = RSTRING_LEN(fb);
    memset(fb_data, 0, fb_size);
  }
  return mrb_nil_value();
}

//
// #fill
//
static mrb_value
mrb_canvas_fill(mrb_state* mrb, mrb_value self) {
  // Get canvas ivars
  canvas_t canvas;
  mrb_get_canvas_data(mrb, self, &canvas);

  for(int i=1; i <= canvas.colors; i++) {
    mrb_value fb = mrb_ary_ref(mrb, canvas.framebuffers, i-1);
    uint8_t* fb_data = (uint8_t*)RSTRING_PTR(fb);
    mrb_int fb_size = RSTRING_LEN(fb);
    if (i == 1) {
      // color = 1 means 0th buffer (black). Fill with 255.
      memset(fb_data, 255, fb_size);    
    } else {
      // Clear others with 0.
      memset(fb_data, 0, fb_size);
    }
  }
  return mrb_nil_value();
}

//
// #_get_pixel
//
static int
c_canvas_get_pixel(mrb_state* mrb, canvas_t* c, int x, int y) {
  int byte_index = ((y / 8) * c->columns) + x;
  int bit = y % 8;

  // If bit not set in any framebuffer, color is 0.
  int color = 0;

  // Check all the framebuffers. If bit set, color is i.
  for(int i=1; i <= c->colors; i++) {
    mrb_value fb = mrb_ary_ref(mrb, c->framebuffers, i-1);
    uint8_t* fb_data = (uint8_t*)RSTRING_PTR(fb);
    uint8_t fb_byte = fb_data[byte_index];
    if ((fb_byte >> bit) & 0b1) {
      color = i;
      break;
    }
  }

  return color;
}

static mrb_value
mrb_canvas_get_pixel(mrb_state* mrb, mrb_value self) {
  // Get canvas ivars
  canvas_t canvas;
  mrb_get_canvas_data(mrb, self, &canvas);

  // Get args
  mrb_int x, y;
  mrb_get_args(mrb, "ii", &x, &y);

  int color = c_canvas_get_pixel(mrb, &canvas, x, y);
  return mrb_fixnum_value(color);
}

//
// #_set_pixel
//
static void
c_canvas_set_pixel(mrb_state* mrb, canvas_t* c, int x, int y, int color) {
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
    uint8_t* fb_data = (uint8_t*)RSTRING_PTR(fb);
    uint8_t fb_byte = fb_data[byte_index];

    // Set pixel in fb for given color
    if (i == color) {
      fb_byte = fb_byte | (0b1 << bit);
    // Clear in other colors
    } else {
      fb_byte = fb_byte & ~(0b1 << bit);
    }
    fb_data[byte_index] = fb_byte;
  }
}

static mrb_value
mrb_canvas_set_pixel(mrb_state* mrb, mrb_value self) {
  // Get canvas ivars
  canvas_t canvas;
  mrb_get_canvas_data(mrb, self, &canvas);

  // Get args
  mrb_int x, y;
  mrb_int color = -1;
  mrb_get_args(mrb, "ii|i", &x, &y, &color);
  if (color == -1) color = canvas.current_color;

  c_canvas_set_pixel(mrb, &canvas, x, y, color);

  return mrb_nil_value();
}

//
// #_line
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
      c_canvas_set_pixel(mrb, c, x1, y, color);
    }
    return;
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
      c_canvas_set_pixel(mrb, c, x, y1, color);
    }
    return;
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
    c_canvas_set_pixel(mrb, c, x, y, color);

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
  // Get canvas ivars
  canvas_t canvas;
  mrb_get_canvas_data(mrb, self, &canvas);

  // Get args
  mrb_int x1, y1, x2, y2;
  mrb_int color = -1;
  mrb_get_args(mrb, "iiii|i", &x1, &y1, &x2, &y2, &color);
  if (color == -1) color = canvas.current_color;

  c_canvas_line(mrb, &canvas, x1, y1, x2, y2, color);

  return mrb_nil_value();
}

//
// #_rectangle
//
static mrb_value
mrb_canvas_rectangle(mrb_state* mrb, mrb_value self) {
  // Get canvas ivars
  canvas_t canvas;
  mrb_get_canvas_data(mrb, self, &canvas);

  // Get args
  mrb_int x1, y1, x2, y2;
  mrb_bool filled = FALSE;
  mrb_int color = -1;
  mrb_get_args(mrb, "iiii|bi", &x1, &y1, &x2, &y2, &filled, &color);
  if (color == -1) color = canvas.current_color;

  // Rectangles and squares as a combination of lines.
  if (filled) {
    mrb_int yt;
    if (y2 < y1) {
      yt = y2;
      y2 = y1;
      y1 = yt;
    }
    for(int y=y1; y<=y2; y++) {
      c_canvas_line(mrb, &canvas, x1, y, x2, y, color);
    }
  } else {
    c_canvas_line(mrb, &canvas, x1, y1, x2, y1, color);
    c_canvas_line(mrb, &canvas, x2, y1, x2, y2, color);
    c_canvas_line(mrb, &canvas, x2, y2, x1, y2, color);
    c_canvas_line(mrb, &canvas, x1, y2, x1, y1, color);
  }

  return mrb_nil_value();
}

//
// #_path
//
static void
c_canvas_path(mrb_state* mrb, canvas_t* c, mrb_value mrb_points, int color) {
  mrb_int point_count = RARRAY_LEN(mrb_points);
  if (point_count == 0) return;

  mrb_int x1;
  mrb_int y1;
  mrb_value point = mrb_ary_entry(mrb_points, 0);
  mrb_int x2 = mrb_as_int(mrb, mrb_ary_entry(point, 0));
  mrb_int y2 = mrb_as_int(mrb, mrb_ary_entry(point, 1));

  for (int i=1; i<point_count; i++) {
    x1 = x2;
    y1 = y2;
    point = mrb_ary_entry(mrb_points, i);
    x2 = mrb_as_int(mrb, mrb_ary_entry(point, 0));
    y2 = mrb_as_int(mrb, mrb_ary_entry(point, 1));
    c_canvas_line(mrb, c, x1, y1, x2, y2, color);
  }
}

static mrb_value
mrb_canvas_path(mrb_state* mrb, mrb_value self) {
  // Get canvas ivars
  canvas_t canvas;
  mrb_get_canvas_data(mrb, self, &canvas);

  // Get args
  mrb_value mrb_points;
  mrb_int color = -1;
  mrb_get_args(mrb, "A|i", &mrb_points, &color);
  if (color == -1) color = canvas.current_color;

  c_canvas_path(mrb, &canvas, mrb_points, color);
  return mrb_nil_value();
}

//
// #_polygon
//
static void
c_canvas_polygon(mrb_state* mrb, canvas_t* c, mrb_value mrb_points, mrb_bool filled, int color) {
  mrb_int point_count = RARRAY_LEN(mrb_points);
  if (point_count == 0) return;

  if (filled) {
    // y_min and y_max both start as the first point's y value.
    mrb_value point = mrb_ary_entry(mrb_points, 0);
    mrb_int y_min = mrb_as_int(mrb, mrb_ary_entry(point, 1));
    mrb_int y_max = mrb_as_int(mrb, mrb_ary_entry(point, 1));

    // Separate x and y coords into float arrays, and also find true y_min and y_max.
    float coords_x[point_count];
    float coords_y[point_count];

    for (int i=0; i<point_count; i++) {
      point = mrb_ary_entry(mrb_points, i);
      int x = mrb_as_int(mrb, mrb_ary_entry(point, 0));
      int y = mrb_as_int(mrb, mrb_ary_entry(point, 1));

      coords_x[i] = x;
      coords_y[i] = y;

      if (y < y_min) y_min = y;
      if (y > y_max) y_max = y;
    }

    // Cast horizontal ray on each row, storing nodes where it intersects polygon edges.
    for (int y=y_min; y <= y_max; y++) {
      // Maximum possible intersections
      int nodes[point_count*2];
      int node_count = 0;
      int i = 0;
      int j = point_count - 1;

      while (i < point_count) {
        // First condition excludes horizontal edges.
        // Second and third check for +ve and -ve intersection respectively.
        if (coords_y[i] != coords_y[j] && ((coords_y[i] < y && coords_y[j] >= y) || (coords_y[j] < y && coords_y[i] >= y))) {
          // Interoplate to find the intersection point (node).
          float x_intersect = coords_x[i] + (float)(y - coords_y[i]) / (coords_y[j] - coords_y[i]) * (coords_x[j] - coords_x[i]);
          nodes[node_count++] = (int)(x_intersect + 0.5f);
        }
        j = i;
        i++;
      }

      // Sort the nodes.
      for (int a=0; a < node_count-1; a++) {
        for (int b=0; b < node_count-a-1; b++) {
          if (nodes[b] > nodes[b+1]) {
            int temp = nodes[b];
            nodes[b] = nodes[b+1];
            nodes[b+1] = temp;
          }
        }
      }

      // Take pairs of nodes and fill between them.
      // This ignores the spaces between odd then even nodes (eg. 1->2), which are outside the polygon.
      for (int n=0; n < node_count-1; n += 2) {
        if (n+1 < node_count) {
          c_canvas_line(mrb, c, nodes[n], y, nodes[n+1], y, color);
        }
      }
    }
  }

  // Stroke regardless, since floating point math misses thin areas of fill.
  // Use _path to stroke without connecting last back to first.
  c_canvas_path(mrb, c, mrb_points, color);

  // Connect last to first. NOTE: order is important here.
  mrb_value first = mrb_ary_entry(mrb_points, 0);
  mrb_value last  = mrb_ary_entry(mrb_points, point_count-1);
  int x1 = mrb_as_int(mrb, mrb_ary_entry(last, 0));
  int y1 = mrb_as_int(mrb, mrb_ary_entry(last, 1));
  int x2 = mrb_as_int(mrb, mrb_ary_entry(first, 0));
  int y2 = mrb_as_int(mrb, mrb_ary_entry(first, 1));
  c_canvas_line(mrb, c, x1, y1, x2, y2, color);
}

static mrb_value
mrb_canvas_polygon(mrb_state* mrb, mrb_value self) {
  // Get canvas ivars
  canvas_t canvas;
  mrb_get_canvas_data(mrb, self, &canvas);

  // Get args
  mrb_value mrb_points;
  mrb_bool filled = FALSE;
  mrb_int color = -1;
  mrb_get_args(mrb, "A|bi", &mrb_points, &filled, &color);
  if (color == -1) color = canvas.current_color;

  c_canvas_polygon(mrb, &canvas, mrb_points, filled, color);
  return mrb_nil_value();
}

//
// #_ellipse
//
static void
c_canvas_ellipse(mrb_state* mrb, canvas_t* c, int x_center, int y_center, int a, int b, mrb_bool filled, int color) {
  // Start position
  int x = -a;
  int y = 0;

  // Precompute x and y increments for each step
  int x_increment = 2 * b * b;
  int y_increment = 2 * a * a;

  // Start errors
  int dx = (1 + (2 * x)) * b * b;
  int dy = x * x;
  int e1 = dx + dy;
  int e2 = dx;

  // Since starting at max negative X, continue until x is 0
  while (x <= 0) {
    if (filled) {
      // Fill quadrants using horizontal lines
      c_canvas_line(mrb, c, x_center - x, y_center + y, x_center + x, y_center + y, color);
      c_canvas_line(mrb, c, x_center - x, y_center - y, x_center + x, y_center - y, color);
    } else {
      // Stroke quadrants in order, as if y-axis is reversed and going counter-clockwise from +ve X.
      c_canvas_set_pixel(mrb, c, x_center - x, y_center - y, color);
      c_canvas_set_pixel(mrb, c, x_center + x, y_center - y, color);
      c_canvas_set_pixel(mrb, c, x_center + x, y_center + y, color);
      c_canvas_set_pixel(mrb, c, x_center - x, y_center + y, color);
    }

    e2 = 2 * e1;
    if (e2 >= dx) {
      x += 1;
      dx += x_increment;
      e1 += dx;
    }
    if (e2 <= dy) {
      y  += 1;
      dy += y_increment;
      e1 += dy;
    }
  }

  // Continue if y hasn't reached the vertical size
  while (y < b) {
    y += 1;
    c_canvas_set_pixel(mrb, c, x_center, y_center + y, color);
    c_canvas_set_pixel(mrb, c, x_center, y_center - y, color);
  }
}

static mrb_value
mrb_canvas_ellipse(mrb_state* mrb, mrb_value self) {
  // Get canvas ivars
  canvas_t canvas;
  mrb_get_canvas_data(mrb, self, &canvas);

  // Get args
  mrb_int x_center, y_center, a, b;
  mrb_bool filled = FALSE;
  mrb_int color = -1;
  mrb_get_args(mrb, "iiii|bi", &x_center, &y_center, &a, &b, &filled, &color);
  if (color == -1) color = canvas.current_color;

  c_canvas_ellipse(mrb, &canvas, x_center, y_center, a, b, filled, color);

  return mrb_nil_value();
}

//
// #_char
//
static void
c_canvas_char(mrb_state* mrb, canvas_t* c, mrb_value char_bytes, int x, int y, int width, int scale, int color) {
  // How many total bytes
  mrb_int byte_count = RARRAY_LEN(char_bytes);

  // How many vertical chunks. Split by displayed font width, allowing partial last.
  int chunks = byte_count / width;
  if (byte_count % width > 0) chunks += 1;
  int y_current = y;

  for (int chunk=0; chunk<chunks; chunk++) {
    for (int column=0; column<width; column++) {
      // Which byte
      int index = chunk*width + column;
      if (index >= byte_count) continue;

      // Get it and show the pixels.
      uint8_t bite = (uint8_t)mrb_as_int(mrb, mrb_ary_entry(char_bytes, index));
      for (int bit=0; bit < 8; bit++) {
        // Don't do anything if this bit isn't set in the font.
        if (((bite >> bit) & 0b1)) {
          for (int sx=0; sx<scale; sx++){
            for(int sy=0; sy<scale; sy++){
              c_canvas_set_pixel(
                mrb,
                c,
                x + (column*scale) + sx,
                y_current + (bit*scale) + sy,
                color
              );
            }
          }
        }
      }
    }
    y_current += (8 * scale);
  }
}

static mrb_value
mrb_canvas_char(mrb_state* mrb, mrb_value self) {
  // Get canvas ivars
  canvas_t canvas;
  mrb_get_canvas_data(mrb, self, &canvas);

  // Get args
  mrb_value char_bytes;
  mrb_int x, y, width, scale;
  mrb_int color = -1;
  mrb_get_args(mrb, "Aiiii|i", &char_bytes, &x, &y, &width, &scale, &color);
  if (color == -1) color = canvas.current_color;

  c_canvas_char(mrb, &canvas, char_bytes, x, y, width, scale, color);
  return mrb_nil_value();
}

//
// #text
//
static mrb_value
mrb_canvas_text(mrb_state* mrb, mrb_value self) {
  // Get canvas ivars
  canvas_t canvas;
  mrb_get_canvas_data(mrb, self, &canvas);

  // Get args
  mrb_value str;
  mrb_value kwargs = mrb_nil_value();
  mrb_int color = -1;
  mrb_get_args(mrb, "S|H", &str, &kwargs);

  // Get color kwarg if given
  if (!mrb_nil_p(kwargs)) {
    mrb_value color_val = mrb_hash_get(mrb, kwargs, mrb_symbol_value(mrb_intern_lit(mrb, "color")));
    if (!mrb_nil_p(color_val)) color = mrb_fixnum(color_val);
  }
  if (color == -1) color = canvas.current_color;

  // Font ivars
  mrb_value font_characters   = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@font_characters"));
  mrb_int font_last_character = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@font_last_character")));
  mrb_int font_height         = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@font_height")));
  mrb_int font_scale          = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@font_scale")));
  mrb_int font_width          = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@font_width")));
  mrb_value text_cursor       = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@text_cursor"));

  // String vars
  const char* str_ptr = mrb_string_cstr(mrb, str);
  mrb_int str_len = RSTRING_LEN(str);

  // Offset by scaled height, since bottom left of char starts at text cursor.
  mrb_int x = mrb_fixnum(mrb_ary_ref(mrb, text_cursor, 0));
  mrb_int y = mrb_fixnum(mrb_ary_ref(mrb, text_cursor, 1)) + 1 - (font_height * font_scale);

  // Each char of string
  for (mrb_int i = 0; i < str_len; i++) {
    unsigned char ch = (unsigned char)str_ptr[i];

    // 0th character in font is SPACE. Offset ASCII code and show ? if character doesn't exist in font.
    mrb_int index = ch - 32;
    if (index < 0 || index > font_last_character) index = 31;
    mrb_value char_map = mrb_ary_ref(mrb, font_characters, index);

    // Draw it
    c_canvas_char(mrb, &canvas, char_map, x, y, font_width, font_scale, color);

    // Increment x, scaling width.
    x += font_width * font_scale;
  }

  // Update x value of @text_cursor ivar.
  mrb_ary_set(mrb, text_cursor, 0, mrb_fixnum_value(x));
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
  mrb_define_method(mrb, mrb_Canvas, "fill",        mrb_canvas_fill,         MRB_ARGS_NONE());
  mrb_define_method(mrb, mrb_Canvas, "clear",       mrb_canvas_clear,        MRB_ARGS_NONE());
  mrb_define_method(mrb, mrb_Canvas, "_get_pixel",  mrb_canvas_get_pixel,    MRB_ARGS_REQ(2));
  mrb_define_method(mrb, mrb_Canvas, "_set_pixel",  mrb_canvas_set_pixel,    MRB_ARGS_REQ(2) | MRB_ARGS_OPT(1));
  mrb_define_method(mrb, mrb_Canvas, "_line",       mrb_canvas_line,         MRB_ARGS_REQ(4) | MRB_ARGS_OPT(1));
  mrb_define_method(mrb, mrb_Canvas, "_rectangle",  mrb_canvas_rectangle,    MRB_ARGS_REQ(4) | MRB_ARGS_OPT(2));
  mrb_define_method(mrb, mrb_Canvas, "_path",       mrb_canvas_path,         MRB_ARGS_REQ(1) | MRB_ARGS_OPT(1));
  mrb_define_method(mrb, mrb_Canvas, "_polygon",    mrb_canvas_polygon,      MRB_ARGS_REQ(1) | MRB_ARGS_OPT(2));
  mrb_define_method(mrb, mrb_Canvas, "_ellipse",    mrb_canvas_ellipse,      MRB_ARGS_REQ(4) | MRB_ARGS_OPT(2));
  mrb_define_method(mrb, mrb_Canvas, "_char",       mrb_canvas_char,         MRB_ARGS_REQ(5) | MRB_ARGS_OPT(1));
  mrb_define_method(mrb, mrb_Canvas, "text",        mrb_canvas_text,         MRB_ARGS_REQ(1) | MRB_ARGS_OPT(1));
}

void
mrb_mruby_denko_fastcanvas_gem_final(mrb_state* mrb) {
}
