/* ASCII pastry world -- keyboard characters, no colour.
 * gcc -O3 -ffast-math -lm -o donut_latin_no_colour donut_latin_no_colour.c && ./donut_latin_no_colour */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define RMAJOR 1.00f
#define RMINOR 0.38f
#define MAX_COLS 512
#define MAX_ROWS 256

enum { MAT_HERO = 1, MAT_MOON = 2 };

/* Dense keyboard ramp, dark -> bright. No colour, so luminance does all the work. */
static const char RAMP[] =
    " .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";

static volatile sig_atomic_t g_resized = 1;
static volatile sig_atomic_t g_running = 1;

static void on_winch(int s) {
    (void)s;
    g_resized = 1;
}
static void on_int(int s) {
    (void)s;
    g_running = 0;
}

typedef struct {
    float x, y, z;
} vec3;
typedef struct {
    uint8_t r, g, b, hit;
} pix;

static inline vec3 v3(float x, float y, float z) {
    vec3 v = {x, y, z};
    return v;
}
static inline vec3 vadd(vec3 a, vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline vec3 vsub(vec3 a, vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline vec3 vmul(vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline vec3 vcross(vec3 a, vec3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float vdot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float clampf(float x, float a, float b) {
    return x < a ? a : (x > b ? b : x);
}
static inline int clampi(int x, int a, int b) { return x < a ? a : (x > b ? b : x); }
static inline vec3 vnorm(vec3 a) {
    float l = sqrtf(vdot(a, a));
    return l > 1e-8f ? vmul(a, 1.f / l) : v3(0, 1, 0);
}
static inline vec3 vclamp01(vec3 a) {
    return v3(clampf(a.x, 0.f, 1.f), clampf(a.y, 0.f, 1.f), clampf(a.z, 0.f, 1.f));
}
static inline float saturate(float x) { return clampf(x, 0.f, 1.f); }

static float gA, gB, gT, gCA, gSA, gCB, gSB;
static vec3 gSun;

static inline vec3 to_object(vec3 p) {
    /* Inverse of classic donut pose Rx(A)*Rz(B): Rz(-B) then Rx(-A). */
    float x = p.x * gCB + p.y * gSB;
    float y = -p.x * gSB + p.y * gCB;
    float y2 = y * gCA + p.z * gSA;
    float z2 = -y * gSA + p.z * gCA;
    return v3(x, y2, z2);
}

static inline float sd_torus_xz(vec3 p, float R, float r) {
    float qx = sqrtf(p.x * p.x + p.z * p.z) - R;
    return sqrtf(qx * qx + p.y * p.y) - r;
}

static float map_scene(vec3 p, int *id) {
    vec3 o = to_object(p);
    float dH = sd_torus_xz(o, RMAJOR, RMINOR);

    float my = 1.48f * sinf(gT * 0.95f);
    vec3 m = v3(o.x, o.y - my, o.z);
    float cm = cosf(gT * 2.35f), sm = sinf(gT * 2.35f);
    float my2 = m.y * cm - m.z * sm;
    float mz2 = m.y * sm + m.z * cm;
    float dM = sd_torus_xz(v3(m.x, my2, mz2), 0.20f, 0.072f);

    if (dM < dH) {
        *id = MAT_MOON;
        return dM;
    }
    *id = MAT_HERO;
    return dH;
}

static float map_d(vec3 p) {
    int id;
    return map_scene(p, &id);
}

static vec3 nrm_scene(vec3 p) {
    const float e = 0.0014f;
    return vnorm(v3(map_d(v3(p.x + e, p.y, p.z)) - map_d(v3(p.x - e, p.y, p.z)),
                    map_d(v3(p.x, p.y + e, p.z)) - map_d(v3(p.x, p.y - e, p.z)),
                    map_d(v3(p.x, p.y, p.z + e)) - map_d(v3(p.x, p.y, p.z - e))));
}

static float softshadow(vec3 ro, vec3 rd) {
    float t = 0.03f, res = 1.f;
    for (int i = 0; i < 12; i++) {
        float d = map_d(vadd(ro, vmul(rd, t)));
        if (d < 0.001f)
            return 0.12f;
        res = fminf(res, 10.f * d / t);
        t += d;
        if (t > 7.f)
            break;
    }
    return 0.12f + 0.88f * fmaxf(res, 0.f);
}

static vec3 shade_mat(vec3 pos, vec3 rd, int id) {
    vec3 n = nrm_scene(pos);
    vec3 v = vmul(rd, -1.f);
    float ndv = saturate(vdot(n, v));
    float rim = powf(1.f - ndv, 3.0f);

    vec3 l1 = gSun;
    vec3 l2 = vnorm(v3(-0.55f, 0.25f, 0.70f));
    float d1 = saturate(vdot(n, l1));
    float d2 = saturate(vdot(n, l2));
    vec3 h = vnorm(vadd(l1, v));
    float spec = powf(saturate(vdot(n, h)), 48.f);
    float sh = 0.42f + 0.58f * softshadow(vadd(pos, vmul(n, 0.025f)), l1);

    vec3 o = to_object(pos);
    float ring = sqrtf(o.x * o.x + o.z * o.z);
    float cavity = saturate((ring - (RMAJOR - RMINOR)) / (2.f * RMINOR));
    float ao = 0.42f + 0.58f * (0.30f + 0.70f * cavity);

    float lum;
    if (id == MAT_MOON) {
        lum = 0.22f + 0.78f * (d1 * sh + 0.28f * d2);
        lum += spec * sh * 1.15f + rim * 0.35f;
    } else {
        lum = (0.16f + 0.90f * (0.80f * d1 * sh + 0.30f * d2)) * ao;
        lum += spec * sh * 0.95f + rim * 0.28f;
    }
    lum = powf(saturate(lum), 0.86f);
    return v3(lum, lum, lum);
}

static pix march_hit(vec3 ro, vec3 rd) {
    float t = 0.f;
    pix miss = {0, 0, 0, 0};
    for (int i = 0; i < 40; i++) {
        vec3 p = vadd(ro, vmul(rd, t));
        int id;
        float d = map_scene(p, &id);
        if (d < 0.0015f) {
            vec3 col = vclamp01(shade_mat(p, rd, id));
            pix o;
            o.r = (uint8_t)(col.x * 255.f + 0.5f);
            o.g = (uint8_t)(col.y * 255.f + 0.5f);
            o.b = (uint8_t)(col.z * 255.f + 0.5f);
            o.hit = 1;
            return o;
        }
        t += d;
        if (t > 12.f)
            break;
    }
    return miss;
}

static char ramp_at(int lum) {
    /* lum is roughly 0..2550 (3*r+6*g+b). Stretch a little so mid greys don't mud. */
    int n = (int)(sizeof(RAMP) - 1);
    float t = (float)lum / 2550.f;
    t = (t - 0.03f) / 0.82f;
    if (t < 0.f)
        t = 0.f;
    if (t > 1.f)
        t = 1.f;
    t = powf(t, 0.78f);
    int i = (int)(t * (float)(n - 1) + 0.5f);
    if (i < 0)
        i = 0;
    if (i >= n)
        i = n - 1;
    return RAMP[i];
}

static char pick_cell(const pix p[4]) {
    int sum = 0, n = 0;
    for (int i = 0; i < 4; i++) {
        if (p[i].hit) {
            sum += (int)p[i].r * 3 + (int)p[i].g * 6 + (int)p[i].b;
            n++;
        }
    }
    if (!n)
        return ' ';
    return ramp_at(sum / n);
}

static void cam_basis(vec3 *ro, vec3 *uu, vec3 *vv, vec3 *ww) {
    float breathe = 0.10f * sinf(gT * 0.33f);
    *ro = v3(0.f, 0.10f + 0.04f * sinf(gT * 0.21f), -2.95f + breathe);
    vec3 ta = v3(0.f, 0.f, 0.f);
    *ww = vnorm(vsub(ta, *ro));
    *uu = vnorm(vcross(v3(0, 1, 0), *ww));
    *vv = vcross(*ww, *uu);
}

static pix sample_pixel(int x, int y, int W, int H, float aspect, vec3 ro, vec3 uu, vec3 vv,
                        vec3 ww, int ssaa) {
    (void)ssaa;
    float tanfov = 0.68f;
    float ndc_x = 2.f * ((float)x + 0.5f) / (float)W - 1.f;
    float ndc_y = 1.f - 2.f * ((float)y + 0.5f) / (float)H;
    vec3 rd = vnorm(vadd(vadd(vmul(uu, ndc_x * aspect * tanfov), vmul(vv, ndc_y * tanfov)), ww));
    return march_hit(ro, rd);
}

static void render_fb(pix *fb, int W, int H) {
    const float cell_aspect = 0.5f;
    float aspect = ((float)W / (float)H) * cell_aspect;
    int ssaa = 1;
    vec3 ro, uu, vv, ww;
    cam_basis(&ro, &uu, &vv, &ww);
    gSun = vnorm(v3(0.72f, 0.42f + 0.06f * sinf(gT * 0.23f), 0.48f));

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            fb[y * W + x] = sample_pixel(x, y, W, H, aspect, ro, uu, vv, ww, ssaa);
        }
    }
}

static size_t emit_text(char *out, size_t cap, const pix *fb, int cols, int rows) {
    int W = cols * 2;
    size_t n = 0;
    /* Home only -- no colour, no SGR. Needed so frames overwrite instead of scroll. */
    if (n + 3 < cap) {
        out[n++] = '\x1b';
        out[n++] = '[';
        out[n++] = 'H';
    }
    for (int row = 0; row < rows; row++) {
        int y0 = row * 2;
        for (int col = 0; col < cols; col++) {
            int x0 = col * 2;
            pix q[4] = {
                fb[(y0 + 0) * W + (x0 + 0)],
                fb[(y0 + 0) * W + (x0 + 1)],
                fb[(y0 + 1) * W + (x0 + 0)],
                fb[(y0 + 1) * W + (x0 + 1)],
            };
            if (n + 1 < cap)
                out[n++] = pick_cell(q);
        }
        if (n + 1 < cap)
            out[n++] = '\n';
    }
    return n;
}

static int write_ppm(const char *path, const pix *fb, int W, int H) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return 1;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        unsigned char c[3] = {fb[i].r, fb[i].g, fb[i].b};
        fwrite(c, 1, 3, f);
    }
    fclose(f);
    return 0;
}

static void term_size(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    } else {
        *cols = 80;
        *rows = 24;
    }
    *cols = clampi(*cols, 20, MAX_COLS);
    *rows = clampi(*rows - 1, 10, MAX_ROWS);
}

static void restore(void) {
    fputs("\x1b[?25h\x1b[?1049l\x1b[?7h\x1b[0m", stdout);
    fflush(stdout);
}

static void sync_angles(void) {
    gCA = cosf(gA);
    gSA = sinf(gA);
    gCB = cosf(gB);
    gSB = sinf(gB);
}

int main(int argc, char **argv) {
    float A0 = 0.0f, B0 = 0.0f, T0 = 0.0f;

    if (argc >= 2 && strcmp(argv[1], "--ppm") == 0) {
        int W = 280, H = 140;
        if (argc >= 4) {
            A0 = strtof(argv[2], NULL);
            B0 = strtof(argv[3], NULL);
        }
        T0 = A0 / 1.80f;
        const char *path = "/tmp/donut.ppm";
        if (argc >= 5) {
            if (argv[4][0] == '/')
                path = argv[4];
            else
                T0 = strtof(argv[4], NULL);
        }
        if (argc >= 6)
            path = argv[5];
        gA = A0;
        gB = B0;
        gT = T0;
        sync_angles();
        pix *fb = calloc((size_t)W * (size_t)H, sizeof(pix));
        if (!fb)
            return 1;
        render_fb(fb, W, H);
        int rc = write_ppm(path, fb, W, H);
        free(fb);
        return rc;
    }

    if (!isatty(STDOUT_FILENO)) {
        fputs("run this in a real terminal\n", stderr);
        return 1;
    }

    struct sigaction sa = {0};
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);
    sa.sa_handler = on_int;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    atexit(restore);

    fputs("\x1b[?1049h\x1b[?25l\x1b[2J", stdout);
    fflush(stdout);

    int cols = 80, rows = 24;
    pix *fb = NULL;
    size_t fb_cap = 0;
    char *obuf = NULL;
    size_t obuf_cap = 0;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double t0 = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
    double prev = t0;
    gA = A0;
    gB = B0;
    gT = 0.f;

    while (g_running) {
        if (g_resized) {
            g_resized = 0;
            term_size(&cols, &rows);
            fputs("\x1b[2J", stdout);
        }

        clock_gettime(CLOCK_MONOTONIC, &ts);
        double now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
        double dt = now - prev;
        prev = now;
        if (dt > 0.08)
            dt = 0.08;
        gA += (float)(1.80 * dt);
        gB += (float)(0.92 * dt);
        gT = (float)(now - t0);
        sync_angles();

        int W = cols * 2, H = rows * 2;
        size_t need = (size_t)W * (size_t)H;
        if (need > fb_cap) {
            pix *nfb = realloc(fb, need * sizeof(pix));
            if (!nfb)
                break;
            fb = nfb;
            fb_cap = need;
        }
        size_t onneed = (size_t)cols * (size_t)rows + (size_t)rows + 8;
        if (onneed > obuf_cap) {
            char *nb = realloc(obuf, onneed);
            if (!nb)
                break;
            obuf = nb;
            obuf_cap = onneed;
        }

        render_fb(fb, W, H);
        size_t n = emit_text(obuf, obuf_cap, fb, cols, rows);
        if (n > obuf_cap)
            n = obuf_cap;
        if (n)
            fwrite(obuf, 1, n, stdout);
        fflush(stdout);

        clock_gettime(CLOCK_MONOTONIC, &ts);
        double done = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
        double sl = 0.028 - (done - now);
        if (sl > 0.001) {
            struct timespec req;
            req.tv_sec = 0;
            req.tv_nsec = (long)(sl * 1e9);
            nanosleep(&req, NULL);
        }
    }

    free(fb);
    free(obuf);
    return 0;
}
