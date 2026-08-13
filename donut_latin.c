/* ASCII pastry world -- keyboard characters only.
 * gcc -O3 -ffast-math -lm -o donut_latin donut_latin.c && ./donut_latin */
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
#define FLOOR_Y (-1.92f)
#define MAX_COLS 512
#define MAX_ROWS 256

enum { MAT_HERO = 1, MAT_SPR = 2, MAT_MOON = 3, MAT_FLOOR = 4 };

/* Typeable stand-ins for the 16 quarter-block patterns. */
static const char ASCII_QUAD[16] = {
    ' ', '`', '\'', '"', ',', '[', '/', 'P', '.', '\\', ']', '7', '_', 'L', 'J', '#',
};

/* Dense keyboard ramp, dark -> bright. */
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
static inline vec3 vmix(vec3 a, vec3 b, float t) {
    return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}
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
static inline float hash21(float x, float y) {
    float n = sinf(x * 127.1f + y * 311.7f) * 43758.5453f;
    return n - floorf(n);
}
static inline float hash31(float x, float y, float z) {
    float n = sinf(x * 127.1f + y * 311.7f + z * 74.7f) * 43758.5453f;
    return n - floorf(n);
}

static float gA, gB, gT, gCA, gSA, gCB, gSB;
static int gSprSeed;
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

    float pr = sqrtf(o.x * o.x + o.z * o.z);
    float phi = atan2f(o.z, o.x);
    float theta = atan2f(o.y, pr - RMAJOR);
    float dH = sd_torus_xz(o, RMAJOR, RMINOR);

    float dS = 1e5f;
    int seed = 0;
    {
        const float su = (float)(2.0 * M_PI / 16.0);
        const float sv = (float)(2.0 * M_PI / 9.0);
        float iu = floorf(phi / su + 0.5f);
        float iv = floorf(theta / sv + 0.5f);
        float h = hash21(iu + 19.f, iv + 7.f);
        if (h > 0.40f && theta > -0.20f && theta < 1.30f) {
            float uu = iu * su, vv = iv * sv;
            float cv = cosf(vv), svv = sinf(vv);
            float cu = cosf(uu), suu = sinf(uu);
            float cx = (RMAJOR + RMINOR * cv) * cu;
            float cy = RMINOR * svv;
            float cz = (RMAJOR + RMINOR * cv) * suu;
            float rr = 0.042f + 0.018f * h;
            float dx = o.x - cx, dy = o.y - cy, dz = o.z - cz;
            dS = sqrtf(dx * dx + dy * dy + dz * dz) - rr;
            seed = (int)(h * 97.f) + (int)iu * 3 + (int)iv * 17;
        }
    }

    float my = 1.48f * sinf(gT * 0.95f);
    vec3 m = v3(o.x, o.y - my, o.z);
    float cm = cosf(gT * 2.35f), sm = sinf(gT * 2.35f);
    float my2 = m.y * cm - m.z * sm;
    float mz2 = m.y * sm + m.z * cm;
    float dM = sd_torus_xz(v3(m.x, my2, mz2), 0.20f, 0.072f);

    float d = dH;
    *id = MAT_HERO;
    if (dS < d) {
        d = dS;
        *id = MAT_SPR;
        gSprSeed = seed;
    }
    if (dM < d) {
        d = dM;
        *id = MAT_MOON;
    }
    return d;
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

static vec3 sky(vec3 rd) {
    float hy = saturate(rd.y * 0.72f + 0.28f);
    vec3 zenith = v3(0.018f, 0.010f, 0.055f);
    vec3 mid = v3(0.07f, 0.02f, 0.09f);
    vec3 horiz = v3(0.34f, 0.07f, 0.14f);
    vec3 col = hy < 0.55f ? vmix(horiz, mid, hy / 0.55f) : vmix(mid, zenith, (hy - 0.55f) / 0.45f);

    float nd = saturate(vdot(rd, gSun));
    col = vadd(col, vmul(v3(1.00f, 0.62f, 0.28f), powf(nd, 140.f) * 2.2f));
    col = vadd(col, vmul(v3(1.00f, 0.28f, 0.48f), powf(nd, 6.0f) * 0.38f));
    col = vadd(col, vmul(v3(0.55f, 0.12f, 0.55f), powf(nd, 2.0f) * 0.16f));

    float n1 = 0.5f + 0.5f * sinf(rd.x * 5.4f + gT * 0.11f) * sinf(rd.y * 4.2f + rd.z * 3.1f);
    float n2 = 0.5f + 0.5f * sinf(rd.x * 13.0f + rd.z * 8.0f + gT * 0.07f);
    float neb = n1 * n1 * (0.45f + 0.55f * n2);
    col = vadd(col, v3(neb * 0.28f, neb * 0.05f, neb * 0.22f));

    if (rd.y > -0.05f) {
        float sx = floorf(rd.x * 72.f);
        float sy = floorf(rd.y * 72.f);
        float sz = floorf(rd.z * 72.f);
        float s = hash31(sx, sy, sz);
        if (s > 0.9964f) {
            float tw = 0.55f + 0.45f * sinf(gT * 6.f + s * 40.f);
            float mag = (s - 0.9964f) * 180.f * tw;
            col = vadd(col, v3(mag, mag * 0.92f, mag * 1.05f));
        }
    }
    return col;
}

static vec3 shade_mat(vec3 pos, vec3 rd, int id, int do_refl) {
    vec3 n = (id == MAT_FLOOR) ? v3(0, 1, 0) : nrm_scene(pos);
    vec3 v = vmul(rd, -1.f);
    float ndv = saturate(vdot(n, v));
    float fres = powf(1.f - ndv, 2.2f);
    float rim = powf(1.f - ndv, 3.0f);

    vec3 l1 = gSun;
    vec3 l2 = vnorm(v3(-0.55f, 0.25f, 0.70f));
    float d1 = saturate(vdot(n, l1));
    float d2 = saturate(vdot(n, l2));
    vec3 h = vnorm(vadd(l1, v));
    float spec = powf(saturate(vdot(n, h)), 52.f);
    float sh = 0.42f + 0.58f * softshadow(vadd(pos, vmul(n, 0.025f)), l1);

    vec3 albedo, col;
    if (id == MAT_HERO) {
        vec3 o = to_object(pos);
        float ring = sqrtf(o.x * o.x + o.z * o.z);
        float cavity = saturate((ring - (RMAJOR - RMINOR)) / (2.f * RMINOR));
        float ao = 0.42f + 0.58f * (0.30f + 0.70f * cavity);
        float pr = ring;
        float phi = atan2f(o.z, o.x);
        float theta = atan2f(o.y, pr - RMAJOR);
        float icing = saturate(theta * 1.15f + 0.15f);
        vec3 dough = v3(0.42f, 0.16f, 0.09f);
        vec3 glaze = v3(0.58f - fres * 0.10f, 0.10f + fres * 0.28f, 0.40f + fres * 0.38f);
        float swirl = 0.5f + 0.5f * sinf(phi * 14.f + theta * 6.f + gT * 0.4f);
        albedo = vmix(dough, glaze, saturate(0.28f + 0.72f * icing));
        albedo = vmix(albedo, vmul(albedo, 1.12f), swirl * icing * 0.25f);
        float diff = (0.80f * d1 * sh + 0.30f * d2) * ao;
        col = vmul(albedo, 0.14f + 0.92f * diff);
        col = vadd(col, v3(spec * sh * 1.05f, spec * sh * 0.82f, spec * sh * 0.42f));
        col = vadd(col, v3(rim * 0.40f, rim * 0.18f, rim * 0.82f));
        if (do_refl) {
            vec3 rdir = vsub(rd, vmul(n, 2.f * vdot(rd, n)));
            vec3 env = sky(vnorm(rdir));
            col = vadd(col, vmul(env, 0.18f + 0.55f * fres));
        }
    } else if (id == MAT_SPR) {
        float h = hash21((float)(gSprSeed % 13), (float)(gSprSeed / 13));
        float h2 = hash21((float)gSprSeed, 3.7f);
        if (h < 0.22f)
            albedo = v3(0.95f, 0.18f, 0.28f);
        else if (h < 0.42f)
            albedo = v3(0.20f, 0.78f, 0.92f);
        else if (h < 0.62f)
            albedo = v3(0.98f, 0.82f, 0.18f);
        else if (h < 0.80f)
            albedo = v3(0.95f, 0.95f, 0.97f);
        else
            albedo = v3(0.55f, 0.95f, 0.32f);
        if (h2 > 0.7f)
            albedo = v3(0.95f, 0.40f, 0.78f);
        float diff = 0.25f + 0.75f * (d1 * sh + 0.35f * d2);
        col = vmul(albedo, diff);
        col = vadd(col, vmul(v3(1, 1, 1), spec * sh * 0.65f));
    } else if (id == MAT_MOON) {
        albedo = v3(0.95f, 0.72f, 0.28f);
        float diff = 0.18f + 0.82f * (d1 * sh + 0.25f * d2);
        col = vmul(albedo, diff);
        col = vadd(col, v3(spec * sh * 1.2f, spec * sh * 0.95f, spec * sh * 0.45f));
        col = vadd(col, v3(rim * 0.55f, rim * 0.25f, rim * 0.15f));
        if (do_refl) {
            vec3 rdir = vsub(rd, vmul(n, 2.f * vdot(rd, n)));
            col = vadd(col, vmul(sky(vnorm(rdir)), 0.22f + 0.40f * fres));
        }
    } else {
        /* Floor: neon grid over dark glass. */
        float ax = fabsf(pos.x), az = fabsf(pos.z);
        float gx = fabsf(fmodf(pos.x + 40.f, 1.00f) - 0.50f);
        float gz = fabsf(fmodf(pos.z + 40.f, 1.00f) - 0.50f);
        float lx = 1.f - saturate((gx - 0.018f) / 0.030f);
        float lz = 1.f - saturate((gz - 0.018f) / 0.030f);
        float line = fmaxf(lx, lz);
        float mx = fabsf(fmodf(pos.x + 40.f, 4.00f) - 2.00f);
        float mz = fabsf(fmodf(pos.z + 40.f, 4.00f) - 2.00f);
        float major = fmaxf(1.f - saturate((mx - 0.03f) / 0.04f),
                            1.f - saturate((mz - 0.03f) / 0.04f));
        vec3 base = v3(0.035f, 0.012f, 0.05f);
        vec3 neon = vmix(v3(0.95f, 0.15f, 0.62f), v3(0.25f, 0.85f, 1.00f),
                         0.5f + 0.5f * sinf(gT * 0.4f + pos.x * 0.2f));
        col = vadd(base, vmul(neon, line * 0.55f + major * 0.70f));
        float fade = saturate(1.f - sqrtf(ax * ax + az * az) * 0.07f);
        col = vmul(col, fade);
        col = vmul(col, 0.35f + 0.65f * sh);
        if (do_refl) {
            vec3 rdir = v3(rd.x, -rd.y, rd.z);
            vec3 env = sky(vnorm(rdir));
            /* cheap object bounce: march the reflection a short way */
            float t = 0.04f;
            vec3 rcol = env;
            for (int i = 0; i < 22; i++) {
                vec3 q = vadd(pos, vmul(rdir, t));
                int kid = 0;
                float d = map_scene(q, &kid);
                if (d < 0.002f) {
                    rcol = shade_mat(q, rdir, kid, 0);
                    break;
                }
                t += d;
                if (t > 8.f)
                    break;
            }
            col = vadd(vmul(col, 0.55f), vmul(rcol, 0.45f));
        }
    }
    return col;
}

static vec3 march_color(vec3 ro, vec3 rd) {
    float tPlane = 1e9f;
    if (rd.y < -1e-4f) {
        float tp = (FLOOR_Y - ro.y) / rd.y;
        if (tp > 0.f)
            tPlane = tp;
    }

    float t = 0.f;
    float glow = 0.f;
    int hit_id = 0;
    vec3 hit_p = ro;
    float hit_t = 1e9f;

    for (int i = 0; i < 40; i++) {
        vec3 p = vadd(ro, vmul(rd, t));
        int id;
        float d = map_scene(p, &id);

        /* Wormhole core + accretion disk, in pastry space. */
        vec3 o = to_object(p);
        float hole = sqrtf(o.x * o.x + o.z * o.z);
        float disk = expf(-18.f * fabsf(o.y)) * expf(-10.f * (hole - 0.42f) * (hole - 0.42f));
        float beam = expf(-14.f * hole) * expf(-0.45f * o.y * o.y);
        float pulse = 0.78f + 0.22f * sinf(gT * 4.6f + o.y * 8.f);
        glow += (disk * 0.018f + beam * 0.008f) * pulse;

        if (d < 0.0015f && t < tPlane) {
            hit_id = id;
            hit_p = p;
            hit_t = t;
            break;
        }
        t += d;
        if (t > 14.f || t > tPlane)
            break;
    }

    vec3 col;
    float fog_t;
    if (hit_id) {
        col = shade_mat(hit_p, rd, hit_id, 1);
        fog_t = hit_t;
    } else if (tPlane < 1e8f) {
        vec3 fp = vadd(ro, vmul(rd, tPlane));
        col = shade_mat(fp, rd, MAT_FLOOR, 1);
        fog_t = tPlane;
    } else {
        col = sky(rd);
        fog_t = 14.f;
    }

    float fog = 1.f - expf(-0.012f * fog_t * fog_t);
    vec3 fogc = sky(vnorm(v3(rd.x, 0.0f, rd.z)));
    col = vmix(col, fogc, fog * 0.72f);

    float ph = 0.55f + 0.45f * sinf(gT * 1.7f);
    vec3 gcol = vmix(v3(1.00f, 0.45f, 0.85f), v3(1.00f, 0.82f, 0.35f), 0.5f + 0.5f * sinf(gT * 0.9f));
    col = vadd(col, vmul(gcol, glow * (1.15f + 0.35f * ph)));

    col.x = powf(saturate(col.x), 0.88f);
    col.y = powf(saturate(col.y), 0.88f);
    col.z = powf(saturate(col.z), 0.88f);
    return vclamp01(col);
}

static pix to_pix(vec3 c) {
    pix o;
    o.r = (uint8_t)(c.x * 255.f + 0.5f);
    o.g = (uint8_t)(c.y * 255.f + 0.5f);
    o.b = (uint8_t)(c.z * 255.f + 0.5f);
    o.hit = 1;
    return o;
}

static char ramp_at(int lum) {
    /* lum is roughly 0..2550 (3*r+6*g+b). */
    int n = (int)(sizeof(RAMP) - 1);
    int i = lum * (n - 1) / 2550;
    if (i < 0)
        i = 0;
    if (i >= n)
        i = n - 1;
    return RAMP[i];
}

static void pick_cell(const pix p[4], int bgr, int bgg, int bgb, char *ch, int fg[3], int bg[3]) {
    int mask = 0, hr = 0, hg = 0, hb = 0, n = 0;
    int L[4];
    for (int i = 0; i < 4; i++) {
        if (p[i].hit) {
            mask |= 1 << i;
            hr += p[i].r;
            hg += p[i].g;
            hb += p[i].b;
            n++;
            L[i] = (int)p[i].r * 3 + (int)p[i].g * 6 + (int)p[i].b;
        } else {
            L[i] = -1;
        }
    }

    if (mask == 0) {
        *ch = ' ';
        fg[0] = bgr;
        fg[1] = bgg;
        fg[2] = bgb;
        bg[0] = bgr;
        bg[1] = bgg;
        bg[2] = bgb;
        return;
    }

    if (mask != 15) {
        *ch = ASCII_QUAD[mask];
        fg[0] = hr / n;
        fg[1] = hg / n;
        fg[2] = hb / n;
        bg[0] = bgr;
        bg[1] = bgg;
        bg[2] = bgb;
        return;
    }

    int lo = L[0], hi = L[0], sumL = L[0];
    for (int i = 1; i < 4; i++) {
        if (L[i] < lo)
            lo = L[i];
        if (L[i] > hi)
            hi = L[i];
        sumL += L[i];
    }
    if (hi - lo < 40) {
        *ch = ramp_at(sumL / 4);
        fg[0] = hr / 4;
        fg[1] = hg / 4;
        fg[2] = hb / 4;
        bg[0] = (fg[0] * 2) / 5;
        bg[1] = (fg[1] * 2) / 5;
        bg[2] = (fg[2] * 2) / 5;
        return;
    }

    int mid = (lo + hi) / 2;
    int m = 0, fr = 0, fg_ = 0, fb = 0, fn = 0, br = 0, bg_ = 0, bb = 0, bn = 0;
    for (int i = 0; i < 4; i++) {
        if (L[i] >= mid) {
            m |= 1 << i;
            fr += p[i].r;
            fg_ += p[i].g;
            fb += p[i].b;
            fn++;
        } else {
            br += p[i].r;
            bg_ += p[i].g;
            bb += p[i].b;
            bn++;
        }
    }
    if (!fn || !bn) {
        *ch = ramp_at(sumL / 4);
        fg[0] = hr / 4;
        fg[1] = hg / 4;
        fg[2] = hb / 4;
        bg[0] = (fg[0] * 2) / 5;
        bg[1] = (fg[1] * 2) / 5;
        bg[2] = (fg[2] * 2) / 5;
        return;
    }
    *ch = ASCII_QUAD[m];
    fg[0] = fr / fn;
    fg[1] = fg_ / fn;
    fg[2] = fb / fn;
    bg[0] = br / bn;
    bg[1] = bg_ / bn;
    bg[2] = bb / bn;
}

static void cam_basis(vec3 *ro, vec3 *uu, vec3 *vv, vec3 *ww) {
    float breathe = 0.14f * sinf(gT * 0.33f);
    float yaw = 0.18f * sinf(gT * 0.17f);
    *ro = v3(0.22f * sinf(gT * 0.19f), 1.02f + 0.07f * sinf(gT * 0.21f), -3.42f + breathe);
    vec3 ta = v3(0.f, -0.28f, 0.f);
    vec3 f = vnorm(vsub(ta, *ro));
    /* small extra yaw so the floor slides, not just the pastry */
    float cy = cosf(yaw), sy = sinf(yaw);
    f = v3(f.x * cy + f.z * sy, f.y, -f.x * sy + f.z * cy);
    *ww = f;
    *uu = vnorm(vcross(v3(0, 1, 0), *ww));
    *vv = vcross(*ww, *uu);
}

static pix sample_pixel(int x, int y, int W, int H, float aspect, vec3 ro, vec3 uu, vec3 vv,
                        vec3 ww, int ssaa) {
    vec3 acc = v3(0, 0, 0);
    float inv = 1.f / (float)ssaa;
    float tanfov = 0.62f;
    for (int j = 0; j < ssaa; j++) {
        for (int i = 0; i < ssaa; i++) {
            float fx = (float)x + ((float)i + 0.5f) * inv;
            float fy = (float)y + ((float)j + 0.5f) * inv;
            float ndc_x = 2.f * fx / (float)W - 1.f;
            float ndc_y = 1.f - 2.f * fy / (float)H;
            vec3 rd = vnorm(vadd(vadd(vmul(uu, ndc_x * aspect * tanfov),
                                      vmul(vv, ndc_y * tanfov)),
                                 ww));
            acc = vadd(acc, march_color(ro, rd));
        }
    }
    float s = 1.f / (float)(ssaa * ssaa);
    return to_pix(vmul(acc, s));
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

static size_t emit_ansi(char *out, size_t cap, const pix *fb, int cols, int rows) {
    int W = cols * 2;
    int bgr = 8, bgg = 3, bgb = 12;

    size_t n = 0;
#define APP(fmt, ...)                                                                              \
    do {                                                                                           \
        int _k = snprintf(out + n, cap > n ? cap - n : 0, fmt, ##__VA_ARGS__);                     \
        if (_k > 0)                                                                                \
            n += (size_t)_k;                                                                       \
    } while (0)

    APP("\x1b[H\x1b[?7l");
    int lfg[3] = {-1, -1, -1}, lbg[3] = {-1, -1, -1};
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
            char ch;
            int fg[3], bg[3];
            pick_cell(q, bgr, bgg, bgb, &ch, fg, bg);
            if (fg[0] != lfg[0] || fg[1] != lfg[1] || fg[2] != lfg[2]) {
                APP("\x1b[38;2;%d;%d;%dm", fg[0], fg[1], fg[2]);
                lfg[0] = fg[0];
                lfg[1] = fg[1];
                lfg[2] = fg[2];
            }
            if (bg[0] != lbg[0] || bg[1] != lbg[1] || bg[2] != lbg[2]) {
                APP("\x1b[48;2;%d;%d;%dm", bg[0], bg[1], bg[2]);
                lbg[0] = bg[0];
                lbg[1] = bg[1];
                lbg[2] = bg[2];
            }
            APP("%c", ch);
        }
        APP("\x1b[0m\n");
        lfg[0] = lfg[1] = lfg[2] = -1;
        lbg[0] = lbg[1] = lbg[2] = -1;
    }
#undef APP
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
        size_t onneed = (size_t)cols * (size_t)rows * 48 + 64;
        if (onneed > obuf_cap) {
            char *nb = realloc(obuf, onneed);
            if (!nb)
                break;
            obuf = nb;
            obuf_cap = onneed;
        }

        render_fb(fb, W, H);
        size_t n = emit_ansi(obuf, obuf_cap, fb, cols, rows);
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
