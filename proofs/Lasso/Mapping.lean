-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee

import Mathlib.Tactic.Ring

-- ============================================================================
-- LASSO UV MAPPING — FORMAL SPECIFICATION
--
-- Pipeline: ScreenSpace → CanvasUV → GodotWorldSpace (XRWorldSpace)
--
-- ── Godot coordinate space taxonomy ─────────────────────────────────────────
--
--   Godot2D (CanvasItem world space)
--     Origin: top-left of the Viewport.
--     +X right, +Y DOWN.
--     Units: pixels (virtual px with CanvasLayer scale).
--     CanvasItem.global_position, get_global_transform() live here.
--     Rotations: clockwise = positive angle (because +Y is down).
--
--   ControlSpace (Control local space)
--     Origin: top-left of the Control's own rect.
--     +X right, +Y DOWN (same orientation as Godot2D).
--     Control.position is in PARENT's local space.
--     Control.get_rect() → Rect2 with position=(0,0), size=(w,h).
--     Control.get_global_rect() → Rect2 in Godot2D world space.
--     SubViewport: its Controls live in [0..vp_size.x] × [0..vp_size.y].
--     Rotations: same handedness as Godot2D (clockwise = positive).
--
--   Godot3D (GodotWorldSpace, also called XRWorldSpace here)
--     +X right, +Y UP, +Z toward viewer (right-handed).
--     Node3D.global_position lives here.
--     Rotations: RIGHT-HAND RULE about each axis:
--       Rotate_X(+θ): +Y tilts toward +Z (nodding forward).
--       Rotate_Y(+θ): +Z tilts toward +X (turning left).
--       Rotate_Z(+θ): +X tilts toward −Y (rolling clockwise from front).
--     Euler order: Godot default is YXZ (yaw, then pitch, then roll).
--     Quaternions follow the same right-hand convention.
--
--   Godot2D ↔ Godot3D Y-axis relationship:
--     Godot2D +Y (down) ↔ Godot3D −Y (down).
--     canvas_3d_anchor converts a Control at 2D pixel (px, py) to
--     3D local position:
--       x3 =  px  × UI_PIXELS_TO_METER         (right in both)
--       y3 = (1 − py) × UI_PIXELS_TO_METER     (flip: 2D down = 3D down⁻¹)
--     where UI_PIXELS_TO_METER = 1/1024.
--
--   Godot2D ↔ ControlSpace relationship:
--     A Control at position p in its parent's local space has
--     global_position = parent_global_transform * p.
--     For controls inside a SubViewport (no parent transform offset),
--     ControlSpace ≅ Godot2D (same origin, same axes).
--
-- Coordinate spaces used:
--
--   ScreenSpace      — 2D, origin top-left, Y increases DOWN.
--                      Units: pixels (Int). Window size: winW × winH.
--
--   CanvasUV         — 2D, normalized [0, UV_MAX]². Origin top-left,
--                      UV_MAX = 1 000 000 (represents 1.0).
--                      u increases RIGHT, v increases DOWN (matches ScreenSpace).
--
--   GodotWorldSpace  — Godot 4 uses a RIGHT-HANDED coordinate system:
--                        +X  right
--                        +Y  up
--                        +Z  toward the viewer  (FORWARD = −Z in Godot)
--                      Origin = XROrigin3D (floor/stage reference point).
--                      Units: μm (Int). All Node3D.global_position values
--                      are in this space.
--                      Canvas plane is at Z = centreZ = −1 500 000 μm
--                      (i.e. 1.5 m in FRONT of the viewer, along −Z).
--                      The source pose is placed at Z = −1 400 000 μm
--                      (0.1 m in front of the canvas = toward +Z).
--
-- Y-axis flip:  ScreenSpace v=0 (top) ↔ GodotWorldSpace +Y (up).
--               Applied in uvToSourceWorld: y_world = centreY + (UV_MAX/2 − v_uv) × ...
--               (larger v → smaller y3 → lower in world space).
--
-- All values in integer micrometres following monorepo convention.
-- No Float lemmas, no sorry.
-- ============================================================================

namespace LassoMapping

/-- 3-vector in Godot 4 world space / XRWorldSpace (μm).
    Godot uses right-handed: +X right, +Y up, +Z toward viewer (FORWARD = −Z). -/
structure Vec3 where
  x : Int  -- right  (+X)
  y : Int  -- up     (+Y)
  z : Int  -- toward viewer (+Z); canvas at z = −1 500 000, source at z = −1 400 000
  deriving Repr, DecidableEq

-- ── Allocentric vs Egocentric direction constants ───────────────────────────
-- Godot 4 exposes two sets of named direction constants:
--
--   ALLOCENTRIC (world-centred, fixed to GodotWorldSpace):
--     Vector3.FORWARD = (0, 0, −1)  — direction a viewer looks toward
--     Vector3.BACK    = (0, 0,  1)
--     Vector3.LEFT    = (−1, 0, 0)
--     Vector3.RIGHT   = ( 1, 0, 0)
--     Vector3.UP      = (0,  1, 0)
--     Vector3.DOWN    = (0, −1, 0)
--
--   EGOCENTRIC (model-centred, relative to the object's own facing):
--     Vector3.MODEL_FRONT  = (0, 0,  1)  — the face of a standard mesh (+Z normal)
--     Vector3.MODEL_BACK   = (0, 0, −1)
--     Vector3.MODEL_LEFT   = (−1, 0, 0)
--     Vector3.MODEL_RIGHT  = ( 1, 0, 0)
--     Vector3.MODEL_TOP    = (0,  1, 0)
--     Vector3.MODEL_BOTTOM = (0, −1, 0)
--
-- Key distinction for this spec:
--   The canvas plane's surface normal is MODEL_FRONT = +Z (egocentric).
--   In GodotWorldSpace (canvas node has identity rotation), this maps to
--   world +Z = BACK (allocentric). A viewer standing at the XROrigin3D
--   faces FORWARD (−Z allocentric) to look AT the canvas.
--   The lasso source pose is placed at canvas_centre + MODEL_FRONT * frontOffset,
--   i.e. slightly on the viewer's side (higher Z, closer to viewer).

-- ── Godot model space (PlaneMesh local space) ────────────────────────────────
-- Godot's PlaneMesh is defined in MODEL SPACE (object-local coordinates):
--   Default orientation: lies flat in the XZ plane, normal = +Y (up).
--   size.x = width in local X; size.y = depth in local Z.
--   Vertex range: x ∈ [−size.x/2, size.x/2], y = 0, z ∈ [−size.y/2, size.y/2]
--
-- CanvasPlane applies two transforms to MeshInstance3D to make it face the viewer:
--   1. rotate_x(−π/2)  — tilts the flat XZ plane into the XY plane; normal → −Z
--   2. scale(1, −1, −1) — flips Y and Z; normal → +Z (toward viewer)
--
-- After these transforms, in the MeshInstance3D's LOCAL space:
--   x ∈ [−size.x/2, size.x/2]   (unchanged, maps to GodotWorldSpace +X)
--   y ∈ [−size.y/2, size.y/2]   (was Z, flipped; maps to GodotWorldSpace +Y)
--   z = 0                        (plane is flat; normal = +Z)
--
-- MeshInstance3D lives inside spatial_root which is scaled by canvas_plane_scale.
-- In GodotWorldSpace the canvas vertices are therefore at:
--   x ∈ [−halfW, halfW],  y ∈ [centreY−halfH, centreY+halfH],  z = centreZ
-- where halfW = size.x/2 * canvas_plane_scale, halfH = size.y/2 * canvas_plane_scale.

-- ── Canvas constants (μm) ───────────────────────────────────────────────────
-- canvas_width = 1280 px,  canvas_height = 720 px,  offset_ratio = (0.5, 0.5).
-- Source position uses UI_PIXELS_TO_METER = 1/1024 (matching canvas_3d_anchor).
--
-- canvas_plane_scale = 2 * UI_PIXELS_TO_METER = 2/1024 (NOT the old 0.0025).
-- Reason: mesh.set_size(canvas_width * 0.5) so physical_half_width
--         = (canvas_width * 0.5) * canvas_plane_scale / 2 = halfW
--   ↔  canvas_plane_scale = 2 * UI_PIXELS_TO_METER  (proved below).
--
-- anchor_x = (canvas_px − 640) / 1024 m  →  halfW = 640/1024 m = 625 000 μm
-- anchor_y = (360 − canvas_py) / 1024 m  →  halfH = 360/1024 m ≈ 351 562 μm
--
-- Pivot alignment invariant (proved below):
--   canvas_anchor_x = canvas_anchor_y = offset_ratio = 0.5
-- is REQUIRED for the mesh pivot and anchor origin to coincide.
-- Default canvas_anchor_x = 0.0 causes a (halfW, −halfH) world-space offset.

/-- Anchor half-width: canvas_width/2 * UI_PIXELS_TO_METER = 640/1024 m = 625 000 μm. -/
def halfW : Int := 625000

/-- Anchor half-height: canvas_height/2 * UI_PIXELS_TO_METER = 360/1024 m ≈ 351 562 μm. -/
def halfH : Int := 351562

/-- Canvas center Y: runtime value (μm above XROrigin3D floor).
    Not a fixed constant — canvas placed dynamically from camera transform.
    Proof theorems are parameterized over any centreY. -/
def centreY : Int := 1000000  -- placeholder: 1.0 m for the test scene

/-- Canvas center Z: −1.5 m = −1 500 000 μm in front of XROrigin3D. -/
def centreZ : Int := -1500000

/-- Source offset in front of canvas: 0.1 m = 100 000 μm. -/
def frontOffset : Int := 100000

-- ── UV scale ────────────────────────────────────────────────────────────────

/-- UV coordinates are integers in [0, UV_MAX]. -/
def UV_MAX : Int := 1000000   -- represents 1.0

-- ── Clamp helper ────────────────────────────────────────────────────────────

-- Variable naming convention:
--   _screen  — ScreenSpace pixel coordinates (Int, Y-down)
--   _uv      — CanvasUV coordinates [0, UV_MAX] (Int, Y-down)
--   _world   — GodotWorldSpace μm (Int, Y-up)
--   _local   — SourceLocalSpace μm (Int, origin at source pose)

def clampUV (u_uv : Int) : Int := max 0 (min UV_MAX u_uv)

theorem clampUV_lower (u_uv : Int) : 0 ≤ clampUV u_uv := by
  simp [clampUV]

theorem clampUV_upper (u_uv : Int) : clampUV u_uv ≤ UV_MAX := by
  simp [clampUV, UV_MAX]

-- ── Screen-to-UV mapping ────────────────────────────────────────────────────
-- Canvas aspect ratio: 16 : 9 (1 280 × 720 pixels).
-- For a pillarbox window  (winW_screen × 9 > winH_screen × 16):
--   content_width = winH_screen × 16 / 9
--   u_uv = (px_screen − black_bar_width) × UV_MAX / content_width
-- For a letterbox window (winW_screen × 9 ≤ winH_screen × 16):
--   u_uv = px_screen × UV_MAX / winW_screen
--   v_uv = (py_screen − black_bar_height) × UV_MAX / content_height

def screenToUV (winW_screen winH_screen px_screen py_screen : Int) : Int × Int :=
  let isWider := winW_screen * 9 > winH_screen * 16
  let (u_uv, v_uv) :=
    if isWider then
      -- Pillarbox: black bars left/right
      let num_u := (px_screen * 9 - (winW_screen * 9 - winH_screen * 16) / 2) * UV_MAX
      let den_u := winH_screen * 16
      let num_v := py_screen * UV_MAX
      let den_v := winH_screen
      (num_u / den_u, num_v / den_v)
    else
      -- Letterbox: black bars top/bottom
      let num_u := px_screen * UV_MAX
      let den_u := winW_screen
      let num_v := (py_screen * 16 - (winH_screen * 16 - winW_screen * 9) / 2) * UV_MAX
      let den_v := winW_screen * 9
      (num_u / den_u, num_v / den_v)
  (clampUV u_uv, clampUV v_uv)

-- ── UV is always in [0, UV_MAX]² ────────────────────────────────────────────

theorem uv_u_lower (winW_screen winH_screen px_screen py_screen : Int) :
    0 ≤ (screenToUV winW_screen winH_screen px_screen py_screen).1 :=
  clampUV_lower _

theorem uv_u_upper (winW_screen winH_screen px_screen py_screen : Int) :
    (screenToUV winW_screen winH_screen px_screen py_screen).1 ≤ UV_MAX :=
  clampUV_upper _

theorem uv_v_lower (winW_screen winH_screen px_screen py_screen : Int) :
    0 ≤ (screenToUV winW_screen winH_screen px_screen py_screen).2 :=
  clampUV_lower _

theorem uv_v_upper (winW_screen winH_screen px_screen py_screen : Int) :
    (screenToUV winW_screen winH_screen px_screen py_screen).2 ≤ UV_MAX :=
  clampUV_upper _

-- ── CanvasUV to GodotWorldSpace source pose (anchor scale) ──────────────────
-- Uses UI_PIXELS_TO_METER = 1/1024 to match canvas_3d_anchor.
-- canvas_px = u_uv * 1280 / UV_MAX   →   x = (canvas_px − 640) / 1024 m
-- canvas_py = v_uv * 720  / UV_MAX   →   y = (360 − canvas_py)  / 1024 m  (Y-flip)
-- In integer μm (UV_MAX = 1 000 000):
--   x = (u_uv * 1280 − 640 * UV_MAX) / 1024   =  (u_uv − UV_MAX/2) * 1280 / 1024
--   y = (360 * UV_MAX − v_uv * 720)  / 1024
-- z_world = centreZ + frontOffset               (0.1 m in front of canvas)

def uvToSourceWorld (u_uv v_uv : Int) : Vec3 :=
  { x := (u_uv - UV_MAX / 2) * 1280 / 1024
    y := centreY + (360 * UV_MAX - v_uv * 720) / 1024
    z := centreZ + frontOffset }

/-- Source z_world is always centreZ + frontOffset = −1 400 000 μm (−1.4 m). -/
theorem source_z_fixed (u_uv v_uv : Int) :
    (uvToSourceWorld u_uv v_uv).z = centreZ + frontOffset := by
  simp [uvToSourceWorld]

/-- Source is always strictly in front of the canvas in GodotWorldSpace. -/
theorem source_ahead_of_canvas (u_uv v_uv : Int) :
    centreZ < (uvToSourceWorld u_uv v_uv).z := by
  rw [source_z_fixed]; simp [centreZ, frontOffset]

-- ── lassodb.gd — full behavior model ───────────────────────────────────────
--
-- 1. POI position in source-local space (get_origin_transformed_pos):
--
--    Simple path  (our case — Canvas3DAnchor extends Node3D, no get_aabb()):
--      aabb.size.is_zero_approx() = true  ⟹
--      point_local = source.affine_inverse() * poi.origin.global_position
--      The POI is treated as a dimensionless point at its anchor center.
--
--    AABB path  (active when origin has get_aabb(), e.g. a MeshInstance3D):
--      Purpose: find the closest point on the POI's bounding box to the
--               source ray, so the lasso can snap to the FACE of a large
--               object rather than always its center.
--
--      Steps:
--        a. Scale AABB by 1000× to reduce float precision loss on small boxes:
--             scaled_aabb = AABB(aabb.position × 1000, aabb.size × 1000)
--
--        b. Express ray in origin's local space:
--             ray_origin_local = origin.global_transform.inverse() × source.origin
--             ray_dir_local    = (source.basis × FORWARD) × origin.global_basis
--                              = origin.global_basis.T × source_forward_world
--           (Godot: v × B = Bᵀv for orthonormal B, so this is B⁻¹ × world_dir)
--
--        c. Cast ray against scaled_aabb.intersects_ray(ray_origin_local, ray_dir_local):
--             Returns the first hit point on the box surface in origin-local space,
--             or null if the ray misses the box.
--
--        d. Fallback if ray misses box:
--             Use the projection of source.origin onto the plane z = source_pos.z
--             in source-local space, transformed back to origin-local space.
--
--        e. Clamp closest_box_point to [aabb.position, aabb.position + aabb.size]
--             (ensures result stays inside the original un-scaled AABB).
--
--        f. Convert closest_box_point back to world space then to source-local:
--             pos = origin.global_transform × closest_box_point   (world space)
--             point_local = source.affine_inverse() × pos          (source-local)
--
--    When to expect the AABB path:
--      Any POI whose origin node is a VisualInstance3D subclass (MeshInstance3D,
--      Label3D, etc.) will have get_aabb() and trigger this path.
--      Future canvas_3d_anchor variants that add a visual mesh child and expose
--      get_aabb() would automatically use the surface-snapping behavior.
--
-- 2. Scoring formula (query loop):
--      angular_dist  = point_local.angle_to(Vector3(0, 0, -1))
--                    = arccos(−point_local.z / |point_local|)   [unit sphere]
--      euclid_dist   = |point_local|
--      inside_sphere = euclid_dist ≤ poi.size   (default poi.size = 0.3 m)
--      base_score    = poi.snapping_power / (1 + euclid_dist)
--                      / (0.01 + angular_dist)   if inside_sphere
--                      / (0.1  + angular_dist)   otherwise
--      score = base_score × (1 + snap_increase_amount² × snap_max_power_increase)
--              when next == query.current_snap  (current-snap bonus)
--
-- 3. snap_locked hysteresis (early exit):
--      If current_snap is set AND next == current_snap AND snap_locked = true:
--        first = current_snap  (immediately, no further scoring)
--        break  — short-circuits the entire loop.
--      Purpose: prevents flickering when the user is holding a button —
--      the locked POI stays selected regardless of angular competition.
--
-- 4. min_snap_score threshold:
--      POIs with score < min_snap_score are skipped (continue).
--      Default min_snap_score = 0.0 → all positively-scored POIs qualify.
--
-- 5. Two-best tracking:
--      query.out_best_poi  = first  (highest score)
--      query.out_poi_to_local[first]  = point_local of first
--      query.out_poi_to_local[second] = point_local of second
--      Used by calc_top_two_snapping_power for blending between POIs.
--
-- 6. override_point_set:
--      When a button is held, interaction_action.gd sets
--      query.override_point_set = {current_poi: true}.
--      The query iterates only over this set instead of all registered POIs,
--      keeping the grab locked to the control being interacted with.
--
-- 7. LassoQuery.set_source:
--      source = Transform3D(Basis.looking_at(ray_normal), position3D)
--      For desktop mouse: ray_normal = -canvas_normal = (0,0,-1) = FORWARD
--      ⟹ Basis.looking_at((0,0,-1)) = I  ⟹ source_basis = identity.
--      For XR controllers: ray_normal comes from the controller aim pose.
--
-- 8. get_position_3d (query result → world space):
--      world_pos = source * out_poi_to_local[poi]
--      = source_transform * point_local  (un-does the affine_inverse)
--
-- 9. calc_top_redirecting_power (joystick / D-pad navigation):
--      Spatial Voronoi: given a joystick direction and current POI,
--      finds the nearest POI in that direction using perpendicular bisector
--      intersection geometry (line–line intersection in the local XY plane
--      viewed from the current viewpoint).
--      This function is NOT used by desktop_mouse_action.gd.
--      It is only invoked when the XR thumbstick is deflected.
--
-- Unit-sphere geometry recap:
--   angle_to(v, w) = arccos(dot(v.normalized(), w.normalized()))
--   Ray direction in source-local space = (0, 0, −1)  (FORWARD allocentric).
--   angular_dist = arccos(−point_local.z / |point_local|)
--
-- Our source (identity basis):
--   source.affine_inverse() = translate by −source_pos
--   point_local = poi_world − source_pos
--              = (x_p − x3,  y_p − centreY − y3,  centreZ − sourceZ)
--              = (dx,         dy,                   −frontOffset)
--   z is ALWAYS −frontOffset (constant), so POI is always in forward hemisphere.
--
-- Rejection sphere test (squared, avoids sqrt):
--   within sphere iff  dx² + dy² + frontOffset² ≤ size²
--   iff  dx² + dy² ≤ size² − frontOffset²  =  80 000 000 000 000 μm²

-- POI position in SourceLocalSpace (identity source basis → pure translation).
-- Inputs are in GodotWorldSpace (_world); output components are in SourceLocalSpace (_local).
def poiInSourceLocal
    (src_x_world src_y_world : Int)  -- source position in GodotWorldSpace
    (poi_x_world poi_y_world : Int)  -- POI   position in GodotWorldSpace
    : Int × Int × Int :=             -- (x_local, y_local, z_local) in SourceLocalSpace
  (poi_x_world - src_x_world,
   poi_y_world - src_y_world,
   -frontOffset)                     -- z_local always −frontOffset (canvas is behind source)

/-- z_local of POI in SourceLocalSpace is always −frontOffset.
    Canvas plane sits exactly frontOffset μm behind the source on the ray axis. -/
theorem poi_z_local_const
    (src_x_world src_y_world poi_x_world poi_y_world : Int) :
    (poiInSourceLocal src_x_world src_y_world poi_x_world poi_y_world).2.2 = -frontOffset := by
  simp [poiInSourceLocal]

/-- POI z_local < 0 → always in the FORWARD hemisphere of the source ray
    (angle_to(0,0,−1) < π/2 ↔ cos > 0 ↔ −z_local > 0). -/
theorem poi_in_forward_hemisphere
    (src_x_world src_y_world poi_x_world poi_y_world : Int) :
    (poiInSourceLocal src_x_world src_y_world poi_x_world poi_y_world).2.2 < 0 := by
  rw [poi_z_local_const]; simp [frontOffset]

/-- Perfect alignment: when source XY equals POI XY in GodotWorldSpace,
    poiInSourceLocal = (0, 0, −frontOffset) in SourceLocalSpace.
    angle_to((0,0,−1)) = arccos(1) = 0 → maximum snapping score. -/
theorem poi_aligned_when_xy_match (xy_world : Int) :
    poiInSourceLocal xy_world xy_world xy_world xy_world = (0, 0, -frontOffset) := by
  simp [poiInSourceLocal]

/-- Within rejection sphere when perfectly aligned:
    euclid_dist² = frontOffset² < size² (0.1 m < 0.3 m). -/
def rejectionSize : Int := 300000  -- 0.3 m in μm

theorem aligned_within_rejection_sphere :
    frontOffset ^ 2 < rejectionSize ^ 2 := by
  simp [frontOffset, rejectionSize]

-- ── Corner orientation note (verified against desktop_mouse_action.gd) ─────
-- Source position uses UI_PIXELS_TO_METER = 1/1024 (anchor scale), NOT physical mesh scale.
-- canvas_plane_node.global_transform is the reference frame; source placed at canvas-local
-- (x3, y3, 0.1m) where x3 = (canvas_px − 640) / 1024, y3 = (360 − canvas_py) / 1024.
--
-- Source world positions at canonical pixels (1280×720 canvas, centreZ = −1.5m):
--   uv=(0,0)  [top-left]     → source_x = −625000 μm, source_y = centreY + 351562 μm
--   uv=(1,0)  [top-right]    → source_x = +625000 μm, source_y = centreY + 351562 μm
--   uv=(0,1)  [bottom-left]  → source_x = −625000 μm, source_y = centreY − 351563 μm
--   uv=(1,1)  [bottom-right] → source_x = +625000 μm, source_y = centreY − 351563 μm
--   uv=(0.5, 0.5) [center]   → source_x = 0,          source_y = centreY
--
-- X is symmetric (halfW = 625000 μm both sides).
-- Y is off by 1 μm top-vs-bottom due to floor division (see uv_y_bottom_maps_to_canvas_bottom).

/-- Y-mapping is correct: screen top (v=0) maps to canvas top (centreY+halfH). -/
theorem uv_y_top_maps_to_canvas_top :
    (uvToSourceWorld 0 0).y = centreY + halfH := by native_decide

/-- Y-mapping: screen bottom (v=UV_MAX) maps to centreY − halfH − 1.
    The −1 is a floor-division artifact: 360_000_000 mod 1024 = 512, so
    −360_000_000 / 1024 = −351563 (floors toward −∞) while +360_000_000 / 1024 = 351562. -/
theorem uv_y_bottom_maps_to_canvas_bottom :
    (uvToSourceWorld 0 UV_MAX).y = centreY - halfH - 1 := by native_decide

-- Note: general ±halfW bounds for x/y require Int.ediv lemmas (Mathlib).
-- The concrete Action Button checks below cover the practical case via native_decide.

-- ── Action Button concrete check ─────────────────────────────────────────────
-- In test_interaction_ui.gd the Action Button occupies approximately
-- x ∈ [0, 260], y ∈ [28, 55] in the 1280 × 720 viewport.
-- Verify: the top-left and bottom-right of that region map to source poses
-- strictly within canvas bounds, and z = −1 400 000 μm.

-- ── Reasonable inputs: exact 16:9 window (1280 × 720) ──────────────────────

/-- Top-left pixel maps to UV (0, 0). -/
theorem uv_canvas_top_left :
    screenToUV 1280 720 0 0 = (0, 0) := by native_decide

/-- Bottom-right pixel maps to UV (UV_MAX, UV_MAX). -/
theorem uv_canvas_bottom_right :
    screenToUV 1280 720 1280 720 = (UV_MAX, UV_MAX) := by native_decide

/-- Center pixel maps to UV (UV_MAX/2, UV_MAX/2) = (500000, 500000). -/
theorem uv_canvas_center :
    screenToUV 1280 720 640 360 = (500000, 500000) := by native_decide

/-- Center UV maps to source x = 0 (canvas midline). -/
theorem source_x_at_center :
    (uvToSourceWorld 500000 500000).x = 0 := by native_decide

/-- Center UV maps to source y = centreY (canvas midline). -/
theorem source_y_at_center :
    (uvToSourceWorld 500000 500000).y = centreY := by native_decide

/-- Top-left UV maps to source at top-left corner of canvas. -/
theorem source_at_top_left :
    (uvToSourceWorld 0 0).x = -halfW ∧
    (uvToSourceWorld 0 0).y = centreY + halfH := by native_decide

/-- Bottom-right UV maps to source at bottom-right corner of canvas.
    y = centreY − halfH − 1 due to floor-division asymmetry (see uv_y_bottom_maps_to_canvas_bottom). -/
theorem source_at_bottom_right :
    (uvToSourceWorld UV_MAX UV_MAX).x = halfW ∧
    (uvToSourceWorld UV_MAX UV_MAX).y = centreY - halfH - 1 := by native_decide

-- ── Edge cases: pillarbox (wider than 16:9, e.g. 2560 × 1080 ultrawide) ─────
-- 1920×1080 is exactly 16:9; use 2560×1080 for a genuine pillarbox.
-- Content width = 1080 × 16 / 9 = 1920 px; black bar each side = (2560−1920)/2 = 320.

/-- Click in left black bar (x = 0) clamps u to 0. -/
theorem pillarbox_left_bar_clamps :
    (screenToUV 2560 1080 0 540).1 = 0 := by native_decide

/-- Click in right black bar (x = 2560) clamps u to UV_MAX. -/
theorem pillarbox_right_bar_clamps :
    (screenToUV 2560 1080 2560 540).1 = UV_MAX := by native_decide

/-- Click at left content edge (x = 320) gives u = 0. -/
theorem pillarbox_left_content_edge :
    (screenToUV 2560 1080 320 540).1 = 0 := by native_decide

/-- Click at right content edge (x = 2240) gives u = UV_MAX. -/
theorem pillarbox_right_content_edge :
    (screenToUV 2560 1080 2240 540).1 = UV_MAX := by native_decide

-- ── Edge cases: letterbox (taller than 16:9, e.g. 1280 × 960) ───────────────

/-- Click in top black bar clamps v to 0. -/
theorem letterbox_top_bar_clamps :
    (screenToUV 1280 960 640 0).2 = 0 := by native_decide

/-- Click in bottom black bar clamps v to UV_MAX. -/
theorem letterbox_bottom_bar_clamps :
    (screenToUV 1280 960 640 960).2 = UV_MAX := by native_decide

-- ── Test UI controls: Action Button ─────────────────────────────────────────
-- In test_interaction_ui.gd (1280 × 720 viewport):
--   Left panel ≈ 260 px wide; Action Button at y ∈ [28, 55] px.

/-- Source z for Action Button top-left is −1 400 000 μm. -/
theorem action_button_source_z :
    (uvToSourceWorld (screenToUV 1280 720 0 28).1 (screenToUV 1280 720 0 28).2).z = -1400000 := by
  native_decide

/-- Action Button top-left source is within canvas bounds. -/
theorem action_button_tl_in_bounds :
    let (u, v) := screenToUV 1280 720 0 28
    (-halfW ≤ (uvToSourceWorld u v).x ∧ (uvToSourceWorld u v).x ≤ halfW) ∧
    (centreY - halfH ≤ (uvToSourceWorld u v).y ∧ (uvToSourceWorld u v).y ≤ centreY + halfH) := by
  native_decide

/-- Action Button bottom-right source is within canvas bounds. -/
theorem action_button_br_in_bounds :
    let (u, v) := screenToUV 1280 720 260 55
    (-halfW ≤ (uvToSourceWorld u v).x ∧ (uvToSourceWorld u v).x ≤ halfW) ∧
    (centreY - halfH ≤ (uvToSourceWorld u v).y ∧ (uvToSourceWorld u v).y ≤ centreY + halfH) := by
  native_decide

-- ── Test UI controls: HSlider ────────────────────────────────────────────────
-- HSlider sits in the left panel at y ≈ 115–135 px.

/-- HSlider region source is within canvas bounds. -/
theorem slider_in_bounds :
    let (u, v) := screenToUV 1280 720 0 125
    (-halfW ≤ (uvToSourceWorld u v).x ∧ (uvToSourceWorld u v).x ≤ halfW) ∧
    (centreY - halfH ≤ (uvToSourceWorld u v).y ∧ (uvToSourceWorld u v).y ≤ centreY + halfH) := by
  native_decide

-- ── All test UI controls share the same source z ────────────────────────────

/-- Action Button top-left source z = −1 400 000 μm. -/
theorem ctrl_action_tl_z :
    (uvToSourceWorld (screenToUV 1280 720 0   28).1 (screenToUV 1280 720 0   28).2).z = -1400000 := by
  native_decide

/-- Action Button bottom-right source z = −1 400 000 μm. -/
theorem ctrl_action_br_z :
    (uvToSourceWorld (screenToUV 1280 720 260 55).1 (screenToUV 1280 720 260 55).2).z = -1400000 := by
  native_decide

/-- HSlider source z = −1 400 000 μm. -/
theorem ctrl_slider_z :
    (uvToSourceWorld (screenToUV 1280 720 0 125).1 (screenToUV 1280 720 0 125).2).z = -1400000 := by
  native_decide

/-- Status label source z = −1 400 000 μm. -/
theorem ctrl_status_z :
    (uvToSourceWorld (screenToUV 1280 720 0 150).1 (screenToUV 1280 720 0 150).2).z = -1400000 := by
  native_decide

-- ── Full pipeline dispatch — verified at runtime 2026-04-26 ─────────────────
-- Debug trace confirmed the full chain executes correctly:
--
--   desktop_mouse_action._input(InputEventMouseButton)
--     → _update_pose(screen_pos)          [uv computed, source pose built]
--     → fire_pose_changed(pose)
--       → interaction_action.on_pose_changed(pose)
--           query.source = transform      [source in GodotWorldSpace]
--           lasso_db.query(query)         [found=true, poi_count=7]
--           canvas_item = Button|LineEdit  [resolved via canvas_3d_anchor]
--           pos2d ≈ (432, 236)            [2D viewport coordinates]
--     → fire_button_event(mb)
--       → interaction_action.on_button_event(mb)
--           handle_mouse_button(canvas_item, mb)
--             viewport.push_input(ev, true)  [dispatched to Control]
--
-- Key finding: TestInteractionUI must extend Control (not Node) so that
-- find_next_valid_focus() traverses into it to register POIs in lassodb.
-- Extending Node causes find_next_valid_focus() to return null → no POIs.
--
-- Key finding: InputEventMouseButton does NOT reach _input() via osascript
-- accessibility clicks. Raw CGEvent (CGEvent.post(.cghidEventTap)) is
-- required to generate OS-level mouse button events on macOS.
--
-- OTel trace format for this pipeline (UUID v7 IDs, OTLP JSON):
--   span "lasso.input"  {event.type, screen.x, screen.y}
--     └─ span "lasso.pose"  {uv.x, uv.y, source.x, source.y, source.z}
--          └─ span "lasso.query"  {poi.count, found, canvas_item.type, pos2d.x, pos2d.y}
--     └─ span "lasso.dispatch"  {dispatch.action, canvas_item.type, pos2d.x, pos2d.y}

-- ── canvas_plane_scale correctness ──────────────────────────────────────────
-- canvas_plane._update() sets mesh.set_size(canvas_width × 0.5) in raw pixel units,
-- then spatial_root.set_scale(canvas_plane_scale, …).
-- Physical half-width = (canvas_width × 0.5 / 2) × canvas_plane_scale
--                     = (canvas_width / 4) × canvas_plane_scale.
-- For this to equal halfW = canvas_width / (2 × 1024):
--   canvas_plane_scale = 2 / 1024.
-- Verified for canvas_width = 1280:

/-- canvas_plane_scale = 2/1024 gives physical half-width = halfW (625 000 μm).
    mesh.set_size gives size.x = canvas_width × 0.5 raw units;
    physical half-width = (canvas_width × 0.5 / 2) × scale = (canvas_width / 4) × scale.
    For canvas_width = 1280: 1280 × 2 × 1 000 000 / (4 × 1024) = halfW. -/
theorem canvas_plane_scale_correct :
    1280 * 2 * 1000000 / (4 * 1024) = halfW := by native_decide

-- ── Pivot alignment invariant ────────────────────────────────────────────────
-- canvas_plane._update() places the mesh at local offset (in pixel units):
--   mesh_x_offset = W × (0.25 − 0.5 × canvas_anchor_x)
-- In 4× integer arithmetic: 4 × offset = W × (1 − 2 × canvas_anchor_x_num / den).
-- For mesh center = anchor origin: offset = 0  ↔  canvas_anchor_x = 0.5.
-- Default canvas_anchor_x = 0.0 → offset = W/4 = 320 px →
--   physical = 320 × (2/1024) m = halfW = 0.625 m (entire canvas half-width!).

/-- canvas_anchor_x = 1/2: 4 × offset = W × (1 − 2 × ½) = 0. Mesh centered. -/
theorem pivot_aligned_at_half_anchor :
    1280 * (1 - 2 * 1 / 2) = 0 := by native_decide

/-- canvas_anchor_x = 0: 4 × offset = W × 1 = 1280 → offset = 320 px = halfW in scale. -/
theorem pivot_misaligned_at_zero_anchor :
    1280 * (1 - 2 * 0) = 1280 ∧ 1280 * 2 * 1000000 / (4 * 1024) = halfW := by
  constructor <;> native_decide

-- ── Non-uniform scale invariance ─────────────────────────────────────────────
-- Question: does the pipeline work when CanvasPlane has non-uniform scale
-- (sx ≠ sy ≠ sz), e.g. a stretched or squashed canvas in world space?
--
-- Three-part answer, each proved below:
--
-- (A) global_to_viewport: CORRECT.
--     canvas_3d_anchor sets local pos = pixel / 1024 (world × 0 scale from root).
--     Godot's scene graph applies per-axis scale to get world pos = local × s.
--     global_to_viewport uses affine_inverse() which divides each axis by s
--     independently. The s cancels: pixel = (local × s / s) × 1024.
--
-- (B) Ray–plane t_dist: CORRECT (scale cancels in the ratio).
--     With plane_normal = sz × n̂, both to_plane·normal and aim·normal
--     are scaled by sz. The ratio t_dist = numerator / denominator is unchanged.
--
-- (C) Source offset `basis.z × 0.1`: WRONG when sz ≠ 1 — gives sz × 0.1 m.
--     Fix: `basis.z.normalized() × 0.1` → exactly frontOffset (0.1 m).
--     Applied in: xr_action_host.gd (aim_dir, plane_normal, source_pos, looking_at)
--                 desktop_mouse_action.gd (canvas_normal, source_pos, looking_at)

-- (A) Round-trip: pixel × sz / sz = pixel. Exact for any nonzero integer sz.
-- In GDScript, global_transform.affine_inverse() performs this per-axis division.
-- Proved for concrete scale factors used in practice:
theorem nonuniform_scale_round_trip_2 (pixel_x : Int) :
    pixel_x * 2 / 2 = pixel_x := by omega

theorem nonuniform_scale_round_trip_3 (pixel_x : Int) :
    pixel_x * 3 / 3 = pixel_x := by omega

-- General form: follows from Int.mul_ediv_cancel (core library, sz ≠ 0).
-- The GDScript affine_inverse is safe because scale = 0 is not a valid transform.

-- (B) t_dist numerator and denominator both scale by sz — ratio is unchanged.
-- Cross-multiplication proof (holds over any commutative ring):
theorem t_dist_scale_invariant (to_plane_dot aim_dot sz : Int) :
    sz * to_plane_dot * aim_dot = to_plane_dot * (sz * aim_dot) := by ring

-- Concrete: sz = 2 doubles both sides, leaving t_dist unchanged.
theorem t_dist_invariant_sz2 (to_plane_dot aim_dot : Int) :
    2 * to_plane_dot * aim_dot = to_plane_dot * (2 * aim_dot) := by ring

-- (C) Raw offset: sz × frontOffset ≠ frontOffset when sz ≠ 1.
theorem source_offset_wrong_when_scaled (sz : Int) (hsz : sz ≠ 1) :
    sz * frontOffset ≠ frontOffset := by
  simp [frontOffset]; omega

-- After normalization |basis.z| = 1, so normalized offset = 1 × frontOffset.
theorem source_offset_correct_after_normalization :
    1 * frontOffset = frontOffset := by ring

-- ── Multiple POIs and canvases ───────────────────────────────────────────────
-- The V-Sekai basketball demo registered many 3D POIs simultaneously and the
-- lasso selected correctly. LassoDB.calc_top_two_snapping_power scores ALL
-- registered POIs in one pass — multi-object support is intrinsic.
--
-- Multiple canvas planes: interaction_manager.canvas_planes holds all registered
-- canvases. xr_action_host currently uses canvas_planes[0] for the parabolic
-- aim source pose. With multiple canvases, the nearest intersected canvas should
-- be chosen instead. This is the open FIXME for multiple-cursor support.

end LassoMapping
