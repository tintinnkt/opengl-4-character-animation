#version 330 core

// Infinite procedural grid, rendered on a full-screen quad.
//
// Algorithm:
//   1. Unproject the NDC fragment position back to a view-space ray.
//   2. Intersect that ray with the world Y=0 plane to get world XZ.
//   3. Use fract() on world XZ to draw grid lines analytically.
//   4. Fade alpha by distance so far lines disappear gracefully.
//
// Cost: purely per-fragment math, zero geometry overhead.

in  vec2 ndcPos;
out vec4 fragColor;

uniform mat4 invProjection;
uniform mat4 invView;

// Grid parameters
const float CELL_SIZE   = 1.0;    // metres per cell
const float LINE_WIDTH  = 0.02;   // fraction of cell
const float FADE_NEAR   = 10.0;   // start fading at this distance
const float FADE_FAR    = 60.0;   // fully transparent at this distance
const vec3  LINE_COLOR  = vec3(0.35, 0.35, 0.45);
const vec3  ORIGIN_COLOR= vec3(0.55, 0.55, 0.70);  // slightly brighter at origin

void main()
{
    // ── Step 1: reconstruct world-space position on Y=0 ──────────────────────
    // NDC -> view space (at near plane, w=1)
    vec4 ndcFar  = vec4(ndcPos, 1.0, 1.0);
    vec4 viewFar = invProjection * ndcFar;
    viewFar /= viewFar.w;

    // View space -> world space direction
    vec4 worldFar  = invView * viewFar;
    vec3 camPos    = vec3(invView[3]);          // camera world position
    vec3 rayDir    = normalize(worldFar.xyz - camPos);

    // Intersect Y=0 plane: camPos.y + t * rayDir.y = 0
    if (abs(rayDir.y) < 0.0001) discard;       // ray is parallel to ground
    float t = -camPos.y / rayDir.y;
    if (t < 0.0) discard;                       // intersection behind camera

    vec3 worldPos = camPos + t * rayDir;

    // ── Step 2: draw grid lines using fract ───────────────────────────────────
    vec2  xz       = worldPos.xz / CELL_SIZE;
    vec2  grid     = abs(fract(xz - 0.5) - 0.5);   // 0 at cell edge, 0.5 at centre
    vec2  dGrid    = fwidth(xz);                     // screen-space derivative for AA
    vec2  line     = smoothstep(LINE_WIDTH - dGrid, LINE_WIDTH + dGrid, grid);
    float lineMask = 1.0 - min(line.x, line.y);

    if (lineMask < 0.01) discard;

    // ── Step 3: choose colour (highlight axes) ────────────────────────────────
    bool onX = abs(worldPos.z) < CELL_SIZE * (LINE_WIDTH + dGrid.y * 2.0);
    bool onZ = abs(worldPos.x) < CELL_SIZE * (LINE_WIDTH + dGrid.x * 2.0);
    vec3 col = (onX || onZ) ? ORIGIN_COLOR : LINE_COLOR;

    // ── Step 4: distance fade ─────────────────────────────────────────────────
    float dist  = length(worldPos.xz - camPos.xz);
    float alpha = lineMask * (1.0 - smoothstep(FADE_NEAR, FADE_FAR, dist)) * 0.65;

    fragColor = vec4(col, alpha);
}
