#include "emotions.h"

#include <stdlib.h>
#include <string.h>
#include "lcddriver.h"

#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

typedef enum {
    FACE_HAPPY = 0,
    FACE_NORMAL,
    FACE_SAD,
    FACE_ANGRY,
    FACE_SURPRISED,
    FACE_SLEEPY,
    FACE_SHY,
    FACE_LOVE,
} face_expression_t;

typedef struct {
    face_expression_t expression;
    bool mouth_open;
    int gaze_x;
    int gaze_y;
} face_render_ctx_t;

const emotion_bitmap_t g_emotions[] = {
    {"happy"},
    {"normal"},
    {"sad"},
    {"angry"},
    {"surprised"},
    {"sleepy"},
    {"shy"},
    {"love"},
};

const size_t g_emotion_count = sizeof(g_emotions) / sizeof(g_emotions[0]);

static const char *alias_to_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    if (strcmp(name, "neutral") == 0) {
        return "normal";
    }
    if (strcmp(name, "loving") == 0 || strcmp(name, "kissy") == 0) {
        return "love";
    }
    if (strcmp(name, "embarrassed") == 0) {
        return "shy";
    }
    if (strcmp(name, "shocked") == 0) {
        return "surprised";
    }
    return name;
}

const emotion_bitmap_t *emotion_find(const char *name)
{
    const char *canonical = alias_to_name(name);
    if (canonical == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < g_emotion_count; ++i) {
        if (strcmp(g_emotions[i].name, canonical) == 0) {
            return &g_emotions[i];
        }
    }
    return NULL;
}

static bool in_ellipse(int x, int y, int cx, int cy, int rx, int ry)
{
    int dx = x - cx;
    int dy = y - cy;
    return (dx * dx * ry * ry + dy * dy * rx * rx) <= (rx * rx * ry * ry);
}

static bool near_segment(int x, int y, int x1, int y1, int x2, int y2, int width)
{
    int vx = x2 - x1;
    int vy = y2 - y1;
    int wx = x - x1;
    int wy = y - y1;
    int len2 = vx * vx + vy * vy;
    if (len2 == 0) {
        return in_ellipse(x, y, x1, y1, width, width);
    }

    int t_num = wx * vx + wy * vy;
    if (t_num < 0) {
        return in_ellipse(x, y, x1, y1, width, width);
    }
    if (t_num > len2) {
        return in_ellipse(x, y, x2, y2, width, width);
    }

    int cross = wx * vy - wy * vx;
    return (cross * cross) <= (width * width * len2);
}

static bool smile_arc(int x, int y, int cx, int cy, int width, int depth, int thickness, bool frown)
{
    int dx = x - cx;
    if (abs(dx) > width) {
        return false;
    }
    int curve = (dx * dx * depth) / (width * width);
    int target_y = frown ? (cy + curve) : (cy - curve);
    return abs(y - target_y) <= thickness;
}

static bool heart(int x, int y, int cx, int cy, int scale)
{
    return in_ellipse(x, y, cx - scale, cy - scale / 2, scale, scale) ||
           in_ellipse(x, y, cx + scale, cy - scale / 2, scale, scale) ||
           (y >= cy - scale / 2 && y <= cy + scale * 2 &&
            abs(x - cx) <= (scale * 2 - (y - cy + scale / 2)));
}

static bool z_mark(int x, int y, int cx, int cy, int size)
{
    return near_segment(x, y, cx - size, cy - size, cx + size, cy - size, 1) ||
           near_segment(x, y, cx + size, cy - size, cx - size, cy + size, 1) ||
           near_segment(x, y, cx - size, cy + size, cx + size, cy + size, 1);
}

static bool eye_dot(int x, int y, int cx, int cy, int rx, int ry, const face_render_ctx_t *ctx)
{
    int gx = ctx == NULL ? 0 : ctx->gaze_x;
    int gy = ctx == NULL ? 0 : ctx->gaze_y;
    return in_ellipse(x, y, cx + gx, cy + gy, rx, ry);
}

static uint16_t face_pixel(const face_render_ctx_t *ctx, int x, int y)
{
    face_expression_t expression = ctx->expression;
    const uint16_t bg = RGB565(0, 0, 0);
    const uint16_t fg = RGB565(255, 255, 255);
    const uint16_t red = RGB565(255, 64, 112);
    const uint16_t pink = RGB565(255, 100, 130);
    const uint16_t blue = RGB565(70, 150, 255);

    bool on = false;
    bool accent_red = false;
    bool accent_pink = false;
    bool accent_blue = false;

    if (expression == FACE_SHY || expression == FACE_LOVE) {
        accent_pink = in_ellipse(x, y, 58, 142, 22, 9) || in_ellipse(x, y, 262, 142, 22, 9);
    }

    switch (expression) {
    case FACE_HAPPY:
        on = smile_arc(x, y, 90, 98, 20, 14, 3, false) ||
             smile_arc(x, y, 230, 98, 20, 14, 3, false) ||
             (ctx->mouth_open ? in_ellipse(x, y, 160, 162, 30, 20) :
              smile_arc(x, y, 160, 162, 48, 34, 4, false));
        if (ctx->mouth_open && in_ellipse(x, y, 160, 162, 16, 10)) {
            on = false;
        }
        break;
    case FACE_NORMAL:
        on = eye_dot(x, y, 90, 96, 13, 13, ctx) ||
             eye_dot(x, y, 230, 96, 13, 13, ctx) ||
             (ctx->mouth_open ? in_ellipse(x, y, 160, 160, 22, 16) :
              near_segment(x, y, 132, 160, 188, 160, 3));
        if (ctx->mouth_open && in_ellipse(x, y, 160, 160, 10, 7)) {
            on = false;
        }
        break;
    case FACE_SAD:
        on = eye_dot(x, y, 90, 96, 12, 12, ctx) ||
             eye_dot(x, y, 230, 96, 12, 12, ctx) ||
             near_segment(x, y, 76, 86, 104, 96, 4) ||
             near_segment(x, y, 216, 96, 244, 86, 4) ||
             smile_arc(x, y, 160, 176, 42, 26, 4, true);
        accent_blue = in_ellipse(x, y, 102, 122, 6, 12);
        break;
    case FACE_ANGRY:
        on = near_segment(x, y, 70, 86, 108, 104, 7) ||
             near_segment(x, y, 212, 104, 250, 86, 7) ||
             near_segment(x, y, 128, 165, 192, 158, 4);
        break;
    case FACE_SURPRISED:
        on = eye_dot(x, y, 90, 96, 17, 17, ctx) ||
             eye_dot(x, y, 230, 96, 17, 17, ctx) ||
             in_ellipse(x, y, 160, 162, 25, 34);
        if (eye_dot(x, y, 90, 96, 7, 7, ctx) || eye_dot(x, y, 230, 96, 7, 7, ctx) ||
            in_ellipse(x, y, 160, 162, 13, 21)) {
            on = false;
        }
        break;
    case FACE_SLEEPY:
        on = near_segment(x, y, 72, 100, 108, 108, 4) ||
             near_segment(x, y, 212, 108, 248, 100, 4) ||
             near_segment(x, y, 138, 160, 182, 160, 3) ||
             z_mark(x, y, 260, 62, 6) ||
             z_mark(x, y, 278, 46, 9);
        break;
    case FACE_SHY:
        on = eye_dot(x, y, 90, 96, 11, 11, ctx) ||
             eye_dot(x, y, 230, 96, 11, 11, ctx) ||
             smile_arc(x, y, 160, 162, 34, 18, 3, false);
        break;
    case FACE_LOVE:
        accent_red = heart(x, y, 90, 96, 10) || heart(x, y, 230, 96, 10);
        on = smile_arc(x, y, 160, 164, 48, 32, 4, false);
        break;
    }

    if (accent_red) {
        return red;
    }
    if (accent_blue) {
        return blue;
    }
    if (on) {
        return fg;
    }
    if (accent_pink) {
        return pink;
    }
    return bg;
}

static void render_face_line(int y, uint16_t *line, int width, void *ctx)
{
    face_render_ctx_t *render_ctx = (face_render_ctx_t *)ctx;
    for (int x = 0; x < width; ++x) {
        line[x] = face_pixel(render_ctx, x, y);
    }
}

bool emotion_draw_presence(const char *name, bool mouth_open, int gaze_x, int gaze_y)
{
    const emotion_bitmap_t *emotion = emotion_find(name);
    if (emotion == NULL) {
        return false;
    }

    face_render_ctx_t ctx = {.expression = FACE_NORMAL};
    ctx.mouth_open = mouth_open;
    ctx.gaze_x = gaze_x < -8 ? -8 : (gaze_x > 8 ? 8 : gaze_x);
    ctx.gaze_y = gaze_y < -6 ? -6 : (gaze_y > 6 ? 6 : gaze_y);
    if (strcmp(emotion->name, "happy") == 0) {
        ctx.expression = FACE_HAPPY;
    } else if (strcmp(emotion->name, "sad") == 0) {
        ctx.expression = FACE_SAD;
    } else if (strcmp(emotion->name, "angry") == 0) {
        ctx.expression = FACE_ANGRY;
    } else if (strcmp(emotion->name, "surprised") == 0) {
        ctx.expression = FACE_SURPRISED;
    } else if (strcmp(emotion->name, "sleepy") == 0) {
        ctx.expression = FACE_SLEEPY;
    } else if (strcmp(emotion->name, "shy") == 0) {
        ctx.expression = FACE_SHY;
    } else if (strcmp(emotion->name, "love") == 0) {
        ctx.expression = FACE_LOVE;
    }

    lcd_draw_rgb565_lines(render_face_line, &ctx);
    return true;
}

bool emotion_draw(const char *name)
{
    return emotion_draw_presence(name, false, 0, 0);
}
