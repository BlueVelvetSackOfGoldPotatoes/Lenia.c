# Lenia.c — Remaining Feature Todos

## 1. Multi-Channel Rendering (Polymorphic Cells)

**What**: Render multi-channel Lenia where each channel maps to a color (R/G/B), producing the polymorphic cell visuals seen in LeniaNDKC demos.

**What we have**: `lenia_multichannel.h/.cpp` — MultiBoard, MultiAutomaton with N kernels and C channels. Compiles and runs.

**What's missing**:
- [ ] Server mode for multi-channel: stream C channels as separate base64 arrays (or composite RGB)
- [ ] API command to switch between single-channel and multi-channel mode
- [ ] Multi-channel parameter initialization: need known-good multi-channel organisms (not in animals.json — those are single-channel). Either port multi-channel animals from LeniaNDKC's JSON format or implement a multi-channel random search
- [ ] UI: RGB composite rendering — channel 0 → Red, channel 1 → Green, channel 2 → Blue, blended
- [ ] UI: per-channel parameter controls (each kernel has its own R, T, m, s, h, r, c0, c1)
- [ ] UI: channel visibility toggles (show/hide individual channels)
- [ ] Known-good test: find or create a stable multi-channel organism that produces the polymorphic cell look

**Technical approach**:
- `lenia_server_mode.cpp`: add `--channels N --kernels-per-channel K --cross-kernels X` flags
- When multi-channel is active, use `MultiAutomaton` instead of `Automaton`
- Composite view: for each pixel, R = channel[0], G = channel[1], B = channel[2], each scaled to 0-255
- Per-channel parameters need a richer JSON protocol: `{"kernels":[{c0:0,c1:0,m:0.15,...},{c0:0,c1:1,...}]}`

---

## 2. 4D Simulation and Higher Dimensions

**What**: Run Lenia in 4D+ and display as 2D slices with a slice navigator.

**What we have**: `FFTND` supports n-dimensional FFT. `calc_kernel` and `calc_once` in Automaton work for any dimension. RLE decoder handles 2D and 3D.

**What's missing**:
- [ ] Server mode: `--dim 4` flag, passing 4D size (e.g., 16x16x16x16 = 65536 cells)
- [ ] 4D kernel computation: already works (D = sqrt(sum(X_i^2)))
- [ ] 4D RLE decoder: extend `decode()` for dim=4 (add `@A` delimiter handling)
- [ ] Slice navigator in UI: two slice axes to choose which 2D plane to view
- [ ] UI: slice position sliders for Z and W axes
- [ ] Load 4D animals from `animals4D.json` (47 patterns available in the repo)
- [ ] Performance: 4D at 16^4 = 65K cells is manageable; 32^4 = 1M cells needs optimization
- [ ] Consider computing FFT only on the 2D visible slice for preview, full 4D for actual sim

**Technical approach**:
- `Board` already uses `NDArray` which supports arbitrary dimensions
- Server: encode the visible 2D slice (at chosen Z, W indices) as the cells_b64
- Add `set_slice_z N` and `set_slice_w N` commands
- UI: add slider controls for Z/W position, update displayed slice in real-time

---

## 3. Trajectory Tracking and Physiology Overlay

**What**: Track organism movement over time and display trajectory arcs, velocity vectors, and physiological stats as overlays on the simulation canvas.

**What we have**: Analyzer computes center of mass, speed, gyradius each step. Server streams speed/gyradius.

**What's missing**:
- [ ] Server: stream center-of-mass position (cx, cy) each frame
- [ ] Server: stream trajectory history (last N positions) — or accumulate client-side
- [ ] UI: draw trajectory curve on canvas (white arc showing past N positions)
- [ ] UI: draw velocity arrow from center of mass
- [ ] UI: draw gyradius circle (radius = gyradius centered on CoM)
- [ ] UI: coordinate overlay text (x, y, vx, vy) in corner
- [ ] UI: toggle overlay on/off (button or checkbox)
- [ ] Optional: draw the trajectory in 3D phase space (mass × growth × speed) like the original

**Technical approach**:
- Add `"cx":float, "cy":float` to the JSON frame output
- Client accumulates last 200 positions in a ring buffer
- Canvas overlay layer: draw polyline through positions, scaled to canvas coordinates
- Arrow: from current position in direction of velocity (use speed + angle from consecutive positions)
- Circle: centered at (cx,cy) with radius proportional to gyradius

---

## 4. Colorbar and Grid Overlay

**What**: The original Lenia shows a continuous gradient colorbar (0.0 → 0.5 → 1.0) on the right edge of the canvas, and a grid of small cross markers (+) at regular intervals.

**What we have**: Colormaps applied to cell values. No colorbar or grid overlay.

**What's missing**:
- [ ] UI: draw a vertical gradient bar on the right side of the main canvas, 20px wide, with 0.0/0.5/1.0 labels
- [ ] UI: draw small + markers at every R pixels (or every 20px) across the canvas
- [ ] UI: scale bar showing "R" distance in pixels at the bottom
- [ ] Toggle overlays on/off

**Technical approach**:
- After rendering cells to canvas, draw overlays using Canvas 2D API (lines, text)
- Grid spacing = R * (canvas_width / grid_size) pixels
- Colorbar: vertical rect filled with the current colormap gradient

---

## 5. Multi-Creature Ecosystem Mode

**What**: Place multiple organisms in the same world and watch them interact, collide, merge, or orbit each other.

**What we have**: Single organism loading from library.

**What's missing**:
- [ ] "Sprinkle" button: load the current organism at a random position WITHOUT clearing the world (additive placement)
- [ ] "Add" command: `add CODE x y` — place a specific organism at coordinates
- [ ] Multiple different species: load different organisms at different positions
- [ ] Interaction detection: when two organisms overlap, their growth fields interact naturally (this already happens — just need the UI to support placing multiple)

**Technical approach**:
- Modify `load_part` to have a `replace=false` mode that adds without clearing
- Server command: `sprinkle` (add current organism at random position), `sprinkle CODE` (add specific creature)
- UI: "Sprinkle" button, optionally with count (sprinkle 5 = add 5 copies)

---

## 6. Better Colormaps (Matching Original)

**What**: The original uses smooth, perceptually uniform colormaps. Our colormaps are approximated in JavaScript. The original also has special per-channel coloring for multi-channel mode.

**What's missing**:
- [ ] Implement the exact 253-entry palette from the Python code (9 colormaps)
- [ ] Multi-channel composite coloring: channel 0 = magenta, channel 1 = cyan, channel 2 = yellow (or RGB)
- [ ] Smooth interpolation in the colormap (current JS colorize() has hard segments)
- [ ] "Auto" colormap mode: auto-normalize to the organism's actual value range

---

## 7. Proper WASD Control via Physics

**What**: Make the organism feel like it's being steered, not just the grid shifting.

**Current state**: Grid roll (1px per keypress). Works but feels like moving the camera, not the creature.

**Possible approaches**:
- [ ] **Approach A — Asymmetric kernel**: temporarily make the kernel asymmetric (egg-shaped instead of circular) so the organism naturally drifts in one direction. Revert kernel to symmetric when key released.
- [ ] **Approach B — Local environment modification**: temporarily increase cell values slightly in a thin strip ahead of the organism and decrease behind. Very small amounts (0.5%) so the organism survives but is nudged.
- [ ] **Approach C — Velocity field**: add a global velocity field (constant wind) that shifts all cells by sub-pixel amounts in the desired direction each step. This is like adding convection to the Lenia equation: A' = clip(A + dt*(G(K*A)) + v·∇A, 0, 1).

**Approach C is the most physically correct** — it's equivalent to the organism moving through a medium. Requires computing the spatial gradient ∇A and adding a convection term.

---

## Priority Order

1. **Trajectory tracking** (easy, high visual impact)
2. **Grid overlay + colorbar** (easy, matches original look)
3. **Multi-creature sprinkle** (easy, fun)
4. **Multi-channel rendering** (medium, enables polymorphic cells)
5. **4D simulation** (medium, needs slice navigator)
6. **Better colormaps** (easy polish)
7. **Physics-based WASD** (hard, requires equation modification)
