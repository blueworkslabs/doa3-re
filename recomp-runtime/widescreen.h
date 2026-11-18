#ifndef RECOMP_WIDESCREEN_H
#define RECOMP_WIDESCREEN_H

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* RECOMP_D3D_WIDESCREEN: unset or 0 is the dashboard's 4:3 setting. Any other
   value reports its widescreen flag; "W:H" from 4:3 to 4:1, such as 16:10
   or a monitor's 3440:1440, also gives the screen's shape, and anything else
   means 16:9. The kernel, the runner and both presenters read it here so
   they agree. */
static inline bool recomp_widescreen(unsigned *aspect_w, unsigned *aspect_h)
{
    const char *value = getenv("RECOMP_D3D_WIDESCREEN");
    char *end = NULL;
    unsigned long w = 0ul, h = 0ul;

    if (value == NULL || strcmp(value, "0") == 0) {
        return false;
    }
    w = strtoul(value, &end, 10);
    if (*end == ':') {
        h = strtoul(end + 1, &end, 10);
    }
    if (*end != '\0' || w > 16384ul || h > 16384ul || h == 0ul ||
        w * 3ul < h * 4ul || w > h * 4ul) {
        w = 16ul;
        h = 9ul;
    }
    *aspect_w = (unsigned)w;
    *aspect_h = (unsigned)h;
    return true;
}

/* DOA3 widens only its 3D view and its movies at 16:9; HUD, menu and
   subtitle quads keep 4:3 positions and would stretch by a third. A
   screen-space quad narrower than the centered 4:3 window is that 2D layer. */
static inline bool recomp_squeeze_widescreen_2d(float left, float right, float width)
{
    return left > width / 8.0f + 1.0f || right < width * 7.0f / 8.0f - 1.0f;
}

/* RECOMP_MOVIE_FIT: "original" shows each movie's whole 4:3 frame at full
   height with bars at the sides; unset or "fill" fills the screen's width,
   cropping above and below. Read once per presenter translation unit. */
static inline bool recomp_movie_original(void)
{
    static int original = -1;
    if (original < 0) {
        const char *value = getenv("RECOMP_MOVIE_FIT");
        original = value != NULL && strcmp(value, "original") == 0;
    }
    return original != 0;
}

typedef struct RecompWidescreen2dScale {
    float x, y;
} RecompWidescreen2dScale;

/* How a widescreen screen-space quad scales about the screen center, for a
   guest target of width x height shown at aspect_w:aspect_h. The 4:3 2D
   layer narrows to 4:3 of the screen's shape (3/4 at 16:9, 5/6 at 16:10).
   Movies arrive in a centered 4:3 frame the game sized for 16:9, at least as
   tall as the screen: 540x480 guest pixels around 4:3 film, or a taller
   zoomed frame around letterboxed 16:9 film. By default (fill) the 4:3 frame
   grows to the screen's width and the zoomed frame keeps the game's zoom,
   shrinking to full width on a screen narrower than 16:9. With
   RECOMP_MOVIE_FIT=original either frame shows whole at full height, as on a
   4:3 television. Fades and backdrops keep their geometry. */
static inline RecompWidescreen2dScale recomp_widescreen_2d_scale(float width, float height,
    unsigned aspect_w, unsigned aspect_h, float left, float right, float top, float bottom)
{
    const float wide = 16.0f * aspect_h / (9.0f * aspect_w);
    const float frame = 0.75f * width / height; /* a 4:3 frame in 16:9 guest units */
    RecompWidescreen2dScale scale = {1.0f, 1.0f};

    if (fabsf(left + right - width) <= 2.0f && bottom - top >= height - 2.0f &&
        fabsf(right - left - frame * (bottom - top)) <= 0.01f * (right - left)) {
        const bool zoomed = bottom - top > height + 2.0f;
        if (recomp_movie_original()) {
            /* the whole frame at full height, kept 4:3 on the screen */
            scale.y = height / (bottom - top);
            scale.x = wide * scale.y;
        } else if (zoomed && wide > 1.0f) {
            scale.y = 1.0f / wide;
        } else if (zoomed) {
            scale.x = wide;
        } else {
            /* 4:3 film: 0.75 * wide of the screen's width at full height */
            scale.y = 1.0f / (0.75f * wide);
            scale.x = wide * scale.y;
        }
    } else if (recomp_squeeze_widescreen_2d(left, right, width)) {
        scale.x = 0.75f * wide;
    }
    return scale;
}

/* At 16:9 the game pre-squeezes a few 2D quads for a television that
   stretches the whole picture: the character select's face tiles keep their
   size and place but sample 4/3 as many texels across (80 for 60 pixels).
   The 2D layer stays 4:3 here, so that squeeze would apply twice. For a
   screen-space quad whose texels per pixel are 4/3 across and 1 down, narrow
   its U span by 3/4 about its centre, back to what 4:3 samples. vertices is
   the presenter's own copy; true if it changed them. */
static inline bool recomp_widescreen_unsqueeze_quad(unsigned char *vertices, unsigned stride,
    unsigned count, unsigned uv_offset, float texture_w, float texture_h)
{
    float x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
    float u0 = INFINITY, u1 = -INFINITY, v0 = INFINITY, v1 = -INFINITY;
    unsigned i;

    if (count != 4u || texture_w <= 0.0f || texture_h <= 0.0f || uv_offset + 8u > stride) return false;
    for (i = 0u; i < count; ++i) {
        float xy[2], uv[2];
        memcpy(xy, vertices + i * stride, sizeof xy);
        memcpy(uv, vertices + i * stride + uv_offset, sizeof uv);
        x0 = fminf(x0, xy[0]); x1 = fmaxf(x1, xy[0]);
        y0 = fminf(y0, xy[1]); y1 = fmaxf(y1, xy[1]);
        u0 = fminf(u0, uv[0]); u1 = fmaxf(u1, uv[0]);
        v0 = fminf(v0, uv[1]); v1 = fmaxf(v1, uv[1]);
    }
    if (!(x1 - x0 >= 8.0f) || !(y1 - y0 >= 8.0f) ||
        fabsf((u1 - u0) * texture_w - (x1 - x0) * 4.0f / 3.0f) > 0.5f ||
        fabsf((v1 - v0) * texture_h - (y1 - y0)) > 0.5f) {
        return false;
    }
    {
        const float centre = 0.5f * (u0 + u1);
        for (i = 0u; i < count; ++i) {
            float u;
            memcpy(&u, vertices + i * stride + uv_offset, sizeof u);
            u = centre + (u - centre) * 0.75f;
            memcpy(vertices + i * stride + uv_offset, &u, sizeof u);
        }
    }
    return true;
}

#endif
