// SPDX-License-Identifier: GPL-3.0-or-later
// The screen shown while a game loads: dusk over the water, drawn entirely in this shader (no
// textures), with a turning ring and the LOADING wordmark. Shared by both graphics backends,
// which add their own #version line, loading_wordmark.glsl before this file, and a main().
//
//   vec3 loading_scene(vec2 pixel, vec2 size, float seconds)
//
// pixel has its origin at the bottom left; seconds counts from the start of loading. With 1000
// added to it the scenery holds one moment and only the ring turns (Settings > Accessibility,
// reduced motion).

const float kHorizon = 0.40;   // height of the horizon, as a fraction of the screen
const float kSunRadius = 0.085;

float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float value_noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),
               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x), f.y);
}

float fbm(vec2 p)
{
    float sum = 0.0;
    float amplitude = 0.5;
    for (int octave = 0; octave < 4; ++octave)
    {
        sum += amplitude * value_noise(p);
        p = p * 2.03 + vec2(17.1, 9.2);
        amplitude *= 0.5;
    }
    return sum;
}

// The sky at q (x in screen heights, y up from 0 to 1), with the sun at `sun`.
vec3 sky_color(vec2 q, vec2 sun, float seconds)
{
    float height = clamp((q.y - kHorizon) / (1.0 - kHorizon), 0.0, 1.0);
    vec3 color = mix(vec3(0.150, 0.125, 0.035), vec3(0.016, 0.086, 0.066), smoothstep(0.0, 0.34, height));
    color = mix(color, vec3(0.003, 0.018, 0.016), smoothstep(0.22, 1.0, height));
    float distance_to_sun = length(q - sun);
    // The glow of the setting sun, and the band of light it leaves along the horizon.
    color += vec3(1.00, 0.60, 0.12) * 0.50 * exp(-distance_to_sun * 5.0);
    color += vec3(1.00, 0.82, 0.32) * 0.30 * exp(-distance_to_sun * 15.0);
    color += vec3(0.95, 0.52, 0.10) * 0.26 * exp(-abs(q.y - kHorizon) * 13.0) *
             exp(-abs(q.x - sun.x) * 1.3);
    // Thin clouds drift across, lit from below near the sun.
    vec2 cloud_at = vec2(q.x * 1.5 + seconds * 0.010, q.y * 7.5);
    float cloud = fbm(cloud_at + fbm(cloud_at * 0.5 + 3.7) * 0.9);
    cloud = smoothstep(0.50, 0.80, cloud) * smoothstep(0.02, 0.16, height) *
            (1.0 - smoothstep(0.50, 0.92, height));
    vec3 lit = vec3(1.00, 0.55, 0.13) * exp(-distance_to_sun * 2.0) * 0.85 + vec3(0.016, 0.042, 0.030);
    float disc = smoothstep(kSunRadius + 0.0025, kSunRadius - 0.0025, distance_to_sun);
    // The disc is paler at its heart and deeper orange towards its rim and its foot.
    float rim = smoothstep(0.15, 1.0, distance_to_sun / kSunRadius);
    vec3 sun_color = mix(vec3(1.00, 0.91, 0.50), vec3(1.00, 0.60, 0.14), rim * 0.75);
    sun_color = mix(sun_color, vec3(1.00, 0.52, 0.10), smoothstep(0.02, -0.07, q.y - sun.y) * 0.5);
    color = mix(color, sun_color, disc);
    return mix(color, lit, cloud * (0.72 - 0.30 * disc));
}

// How high the land rises above the horizon at x (in screen heights).
float land_height(float x, float aspect)
{
    float land = 0.0;
    land += 0.052 * smoothstep(0.26, 0.0, abs(x - 0.52 * aspect));
    land += 0.030 * smoothstep(0.13, 0.0, abs(x - 0.40 * aspect));
    land += 0.018 * smoothstep(0.07, 0.0, abs(x - 0.33 * aspect));
    land += 0.200 * smoothstep(0.76 * aspect, 1.02 * aspect, x); // the coast on the right
    land *= 0.72 + 0.56 * value_noise(vec2(x * 19.0, 3.0));
    land += 0.012 * value_noise(vec2(x * 70.0, 7.0)) * step(0.004, land);
    return land;
}

// Distance to a leaf blade: the lens between two circles of `radius`, `half_width` wide.
float leaf_distance(vec2 p, float radius, float offset)
{
    p = abs(p);
    float b = sqrt(radius * radius - offset * offset);
    return ((p.y - b) * offset > p.x * b) ? length(p - vec2(0.0, b)) :
                                            length(p - vec2(-offset, 0.0)) - radius;
}

vec2 rotate(vec2 p, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

// A banana leaf in the foreground, from `base` to `tip`: lit through by the low sun, veined,
// with the light caught along the edge that faces it. `blur` puts it out of focus.
void add_leaf(inout vec3 color, vec2 q, vec2 base, vec2 tip, float half_width, float blur,
              float lit_side, float seconds, float phase)
{
    float sway = 0.030 * sin(seconds * 0.55 + phase) + 0.010 * sin(seconds * 1.3 + phase * 2.0);
    vec2 axis = rotate(tip - base, sway);
    float half_length = 0.5 * length(axis);
    vec2 along = axis / (2.0 * half_length);
    vec2 across = vec2(along.y, -along.x);
    vec2 v = q - base;
    vec2 p = vec2(dot(v, across), dot(v, along) - half_length);
    float radius = (half_length * half_length + half_width * half_width) / (2.0 * half_width);
    float distance_ = leaf_distance(p, radius, radius - half_width);
    float cover = 1.0 - smoothstep(-blur, blur, distance_);
    if (cover <= 0.0 && distance_ > 0.05)
        return;
    // Brighter towards the lit edge and the tip; veins run out from the midrib.
    float side = clamp(0.5 + 0.5 * lit_side * p.x / half_width, 0.0, 1.0);
    float veins = 0.5 + 0.5 * sin((p.y + abs(p.x) * 0.8) * 150.0);
    vec3 blade = mix(vec3(0.010, 0.060, 0.020), vec3(0.085, 0.340, 0.060), side * side);
    blade *= 0.86 + 0.14 * veins;
    blade *= 0.55 + 0.45 * smoothstep(-half_length, half_length, p.y);
    blade = mix(blade, vec3(0.20, 0.42, 0.10), (1.0 - smoothstep(0.0, 0.0035 + blur, abs(p.x))) * 0.5);
    color = mix(color, blade, cover);
    float edge = exp(-abs(distance_) / (0.0035 + blur * 0.7)) * smoothstep(-0.2, 0.6, lit_side * p.x / half_width);
    color += vec3(0.78, 0.90, 0.22) * edge * 0.60;
}

float word_texel(int x, int y)
{
    int index = clamp(y, 0, kWordHeight - 1) * kWordWidth + clamp(x, 0, kWordWidth - 1);
    return float((kWord[index >> 2] >> uint((index & 3) * 8)) & 255u) / 255.0;
}

// Signed distance to the wordmark's outline in texels of its field (positive inside).
float word_distance(vec2 st)
{
    vec2 at = st - 0.5;
    ivec2 i = ivec2(floor(at));
    vec2 f = at - vec2(i);
    float value = mix(mix(word_texel(i.x, i.y), word_texel(i.x + 1, i.y), f.x),
                      mix(word_texel(i.x, i.y + 1), word_texel(i.x + 1, i.y + 1), f.x), f.y);
    return (value - 128.0 / 255.0) * 2.0 * kWordSpread;
}

vec3 loading_scene(vec2 pixel, vec2 size, float seconds)
{
    bool calm = seconds >= 1000.0;
    if (calm)
        seconds -= 1000.0;
    float world = calm ? 4.0 : seconds; // the time the scenery lives in
    vec2 uv = pixel / size;
    float aspect = size.x / size.y;
    vec2 q = vec2(uv.x * aspect, uv.y);
    vec2 sun = vec2(0.655 * aspect, kHorizon + 0.030);
    vec3 color;

    if (uv.y >= kHorizon)
    {
        color = sky_color(q, sun, world);
        // Islands and the coast stand dark against the light, hazier towards the sun.
        float land = land_height(q.x, aspect);
        float shore = smoothstep(0.0025, -0.0025, uv.y - kHorizon - land);
        vec3 silhouette = vec3(0.008, 0.030, 0.020) +
                          vec3(0.20, 0.12, 0.03) * exp(-abs(q.x - sun.x) * 2.2) * 0.45;
        color = mix(color, silhouette, shore);
    }
    else
    {
        // Water: the sky upside down, broken by ripples that grow towards the viewer.
        float depth = (kHorizon - uv.y) / kHorizon; // 0 at the horizon, 1 at the bottom
        vec2 wave = vec2((q.x - sun.x) / (0.022 + 0.085 * depth),
                         48.0 * log(1.0 + 3.0 * depth) + world * 0.8);
        float coarse = value_noise(wave);
        float fine = value_noise(wave * vec2(2.1, 2.6) + vec2(11.0 - world * 0.35, 3.0));
        float ripple = (coarse - 0.5) + 0.5 * (fine - 0.5);
        vec2 mirrored = vec2(q.x + ripple * 0.030 * (0.2 + depth),
                             2.0 * kHorizon - uv.y + ripple * 0.060 * (0.15 + depth));
        mirrored.y = max(mirrored.y, kHorizon);
        color = sky_color(mirrored, sun, world) * vec3(0.50, 0.70, 0.62) * (0.82 - 0.46 * depth);
        float land = land_height(mirrored.x, aspect) * 0.85;
        float shore = smoothstep(0.004, -0.004, mirrored.y - kHorizon - land);
        color = mix(color, vec3(0.004, 0.018, 0.013), shore * (0.9 - 0.3 * depth));
        // The sun's path across the water breaks into glints.
        float path = exp(-abs(q.x - sun.x) / (0.030 + depth * 0.33));
        float glint = smoothstep(0.54, 0.90, coarse * 0.62 + fine * 0.38);
        color += vec3(1.00, 0.74, 0.24) * path * glint * (1.35 - 0.80 * depth);
        // A thin bright line where water meets sky.
        color += vec3(0.80, 0.55, 0.16) * 0.22 * exp(-depth * 90.0) * exp(-abs(q.x - sun.x) * 1.6);
    }

    // Motes of light rise slowly, as in the menu.
    for (int i = 0; i < 16; ++i)
    {
        float n = float(i);
        float speed = 0.010 + 0.016 * hash21(vec2(n, 1.0));
        float travel = fract(world * speed + hash21(vec2(n, 2.0)));
        vec2 at = vec2((0.08 + 0.84 * hash21(vec2(n, 3.0))) * aspect +
                           0.018 * sin(world * (0.3 + 0.4 * hash21(vec2(n, 4.0))) + n * 1.7),
                       0.16 + 0.62 * travel);
        float radius = 0.0022 + 0.0030 * hash21(vec2(n, 5.0));
        float twinkle = 0.6 + 0.4 * sin(world * (1.2 + 1.8 * hash21(vec2(n, 6.0))) + n * 2.3);
        float life = sin(3.14159265 * travel);
        float glow = exp(-length(q - at) / radius);
        color += mix(vec3(0.87, 0.91, 0.65), vec3(1.00, 0.84, 0.42), hash21(vec2(n, 7.0))) *
                 glow * 0.30 * life * twinkle;
    }

    // Leaves frame the view from the corners, swaying.
    add_leaf(color, q, vec2(0.99 * aspect, 1.12), vec2(0.60 * aspect, 0.88), 0.070, 0.006, 1.0, world, 1.7);
    add_leaf(color, q, vec2(1.07 * aspect, 1.04), vec2(0.72 * aspect, 0.60), 0.090, 0.004, 1.0, world, 0.0);
    add_leaf(color, q, vec2(1.09 * aspect, 0.66), vec2(0.83 * aspect, 0.25), 0.080, 0.009, 1.0, world, 3.1);
    add_leaf(color, q, vec2(-0.07 * aspect, 0.50), vec2(0.17 * aspect, 0.83), 0.075, 0.013, -1.0, world, 4.4);

    // Darker towards the edges, and at the bottom where the wordmark sits.
    vec2 centred = uv - vec2(0.5, 0.52);
    color *= 1.0 - 0.55 * smoothstep(0.35, 0.95, length(centred * vec2(1.0, 1.25)));
    color *= 0.55 + 0.45 * smoothstep(0.0, 0.30, uv.y);

    // ---- the ring and the wordmark, bottom centre ----
    float unit = size.y / 1080.0; // one design pixel
    float word_height = 38.0 * unit;
    float texel = word_height / float(kWordHeight);
    float word_width = texel * float(kWordWidth);
    float ring_radius = 17.0 * unit;
    float gap = 16.0 * unit;
    float group = ring_radius * 2.0 + gap + word_width;
    vec2 origin = vec2(0.5 * size.x - 0.5 * group, 0.118 * size.y);
    vec3 lime = vec3(0.663, 0.859, 0.388);
    vec3 pale = vec3(0.875, 0.910, 0.651);

    // The ring: a faint track with an arc that runs round it, stretching and closing.
    vec2 from_centre = pixel - (origin + vec2(ring_radius, 0.0));
    float ring = abs(length(from_centre) - ring_radius) - 1.8 * unit;
    float ring_cover = clamp(0.5 - ring, 0.0, 1.0);
    float angle = atan(from_centre.y, from_centre.x);
    float turn = seconds * 3.3;
    float reach = 1.9 + 1.2 * sin(seconds * 1.7); // length of the arc, radians
    float along = mod(turn - angle, 6.2831853);
    float arc = smoothstep(reach, reach - 0.9, along) * smoothstep(0.0, 0.12, along);
    color = mix(color, pale, ring_cover * 0.14);
    color = mix(color, lime, ring_cover * arc);
    color += lime * 0.22 * arc * exp(-max(ring, 0.0) / (5.0 * unit));

    // The wordmark, with a slow glimmer passing along it.
    vec2 word_at = pixel - vec2(origin.x + ring_radius * 2.0 + gap, origin.y - 0.5 * word_height);
    if (word_at.x > -texel && word_at.x < word_width + texel && word_at.y > -texel &&
        word_at.y < word_height + texel)
    {
        vec2 st = vec2(word_at.x, word_height - word_at.y) / texel;
        float cover = clamp(word_distance(st) * texel + 0.5, 0.0, 1.0);
        float glimmer = exp(-pow((word_at.x / word_width - fract(world * 0.38) * 1.6 + 0.3) * 5.0, 2.0));
        color = mix(color, mix(pale, vec3(1.0), glimmer * 0.8), cover * (0.80 + 0.20 * glimmer));
    }

    // Arrive out of the dark; a little noise keeps the gradients from banding.
    color *= smoothstep(0.0, 0.7, seconds);
    color += (hash21(pixel + fract(seconds) * 61.0) - 0.5) / 255.0 * 1.4;
    return clamp(color, 0.0, 1.0);
}

// The step a game's start is at, under the wordmark: up to 24 glyphs of the overlay's 3x5 font
// (hud.h), centred. pixel has its origin at the bottom left.
vec3 loading_status(vec3 color, vec2 pixel, vec2 size, uint glyphs[24])
{
    int count = 0;
    for (int i = 0; i < 24; ++i)
        if (glyphs[i] != 0u)
            count = i + 1;
    if (count == 0)
        return color;
    float cell = 5.0 * size.y / 1080.0; // a glyph is 3 cells wide and 5 high, one cell apart
    vec2 p = pixel - vec2(0.5 * (size.x - float(count * 4 - 1) * cell), 0.058 * size.y);
    if (p.x < 0.0 || p.y < 0.0)
        return color;
    ivec2 c = ivec2(p / cell);
    int i = c.x / 4;
    int x = c.x % 4;
    if (i >= count || x >= 3 || c.y >= 5)
        return color;
    bool ink = ((glyphs[i] >> uint(c.y * 3 + 2 - x)) & 1u) != 0u;
    return ink ? mix(color, vec3(0.80, 0.86, 0.94), 0.72) : color;
}
