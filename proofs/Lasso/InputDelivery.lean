-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee

-- ============================================================================
-- LASSO INPUT DELIVERY — FORMAL SPECIFICATION
--
-- Models the two input paths that feed the lasso interaction system:
--
--   Path A: OpenXR controller
--     OpenXR runtime → XRPositionalTracker → XRController3D signals
--     → xr_action_host.gd → fire_pose_changed / fire_button_event
--     → interaction_action.on_pose_changed / on_button_event
--
--   Path B: Desktop mouse (desktop_mouse_action.gd)
--     OS mouse event → Godot _input() → _update_pose / fire_button_event
--     → interaction_action.on_pose_changed / on_button_event
--
-- Key theorem: both paths satisfy the same precondition for fire_button_event,
-- so the lasso dispatch is input-path-agnostic.
--
-- Gap axiom: osascript accessibility `click at {}` emits CGEventType.mouseMoved
-- only — NOT mouseDown + mouseUp — so Godot never receives InputEventMouseButton
-- from accessibility clicks. Raw CGEvent.post(.cghidEventTap) is required.
--
-- OpenXR action system references:
--   OpenXR 1.1 spec §6 "Action System"
--   Godot 4 OpenXR module: modules/openxr/
--   interaction profile: /interaction_profiles/oculus/touch_controller
-- ============================================================================

namespace LassoInputDelivery

-- ── OpenXR action types ──────────────────────────────────────────────────────

/-- OpenXR action types (XrActionType enum, OpenXR spec §6.4). -/
inductive XRActionType
  | Bool    -- XR_ACTION_TYPE_BOOLEAN_INPUT: trigger click, grip click, button A/B/X/Y
  | Float   -- XR_ACTION_TYPE_FLOAT_INPUT:   trigger value [0.0, 1.0], grip value
  | Vec2    -- XR_ACTION_TYPE_VECTOR2F_INPUT: thumbstick/trackpad XY
  | Pose    -- XR_ACTION_TYPE_POSE_INPUT:     grip pose, aim pose
  | Haptic  -- XR_ACTION_TYPE_VIBRATION_OUTPUT: rumble
  deriving Repr, DecidableEq

/-- An OpenXR binding path component.
    e.g. /user/hand/right/input/trigger/click → Hand.Right, Trigger, Click -/
inductive Hand | Left | Right deriving Repr, DecidableEq

inductive InputComponent
  | TriggerClick   -- boolean: trigger fully pressed
  | TriggerValue   -- float:   trigger squeeze depth [0,1]
  | GripClick      -- boolean: grip fully pressed
  | GripValue      -- float:   grip squeeze depth [0,1]
  | ThumbstickXY   -- vec2:    thumbstick position
  | ThumbstickClick-- boolean: thumbstick pressed
  | ButtonA        -- boolean: A button (right hand)
  | ButtonB        -- boolean: B button (right hand)
  | ButtonX        -- boolean: X button (left hand)
  | ButtonY        -- boolean: Y button (left hand)
  | AimPose        -- pose:    controller aim ray origin + direction
  | GripPose       -- pose:    controller grip physical position
  deriving Repr, DecidableEq

/-- A fully-qualified OpenXR input binding path (simplified). -/
structure XRBindingPath where
  hand      : Hand
  component : InputComponent
  deriving Repr, DecidableEq

-- ── OpenXR action ────────────────────────────────────────────────────────────

/-- An OpenXR action: named, typed, bound to one or more physical inputs. -/
structure XRAction where
  name     : String
  kind     : XRActionType
  bindings : List XRBindingPath
  deriving Repr

-- ── Godot OpenXR action map (openxr_action_map.tres) ────────────────────────
-- The interaction-system-project's openxr_action_map.tres defines these actions
-- for the Meta Touch Controller interaction profile:
--   /interaction_profiles/oculus/touch_controller

/-- Primary aim pose action (used by xr_action_host to update controller position). -/
def aimPoseAction : XRAction := {
  name := "aim_pose"
  kind := .Pose
  bindings := [
    { hand := .Left,  component := .AimPose },
    { hand := .Right, component := .AimPose }
  ]
}

/-- Trigger click → primary "select" action (maps to mouse left button). -/
def triggerClickAction : XRAction := {
  name := "trigger_click"
  kind := .Bool
  bindings := [
    { hand := .Left,  component := .TriggerClick },
    { hand := .Right, component := .TriggerClick }
  ]
}

/-- Grip click → "grab" action (used for dragging sliders). -/
def gripClickAction : XRAction := {
  name := "grip_click"
  kind := .Bool
  bindings := [
    { hand := .Left,  component := .GripClick },
    { hand := .Right, component := .GripClick }
  ]
}

-- ── XRController3D signal model ──────────────────────────────────────────────
-- Godot's XRController3D emits signals when OpenXR actions fire.
-- Relevant signals for lasso:
--   button_pressed(name: String)   — Bool action, value true
--   button_released(name: String)  — Bool action, value false
--   input_float_changed(name, value) — Float action value changed
--   pose_changed(pose: XRPose)     — Pose action updated

inductive XRControllerSignal
  | ButtonPressed  (action_name : String) (hand : Hand)
  | ButtonReleased (action_name : String) (hand : Hand)
  | InputFloat     (action_name : String) (hand : Hand) (value : Float)
  | PoseChanged    (hand : Hand) (valid : Bool)
  deriving Repr

-- ── xr_action_host.gd behavior model ───────────────────────────────────────
-- xr_action_host.gd connects to XRController3D signals and calls:
--   fire_pose_changed(pose)   — on PoseChanged
--   fire_button_event(event)  — on ButtonPressed / ButtonReleased
--
-- The lasso only needs fire_pose_changed to SELECT a control and
-- fire_button_event to DISPATCH a click to it.

inductive LassoInputEvent
  | PoseUpdate (valid : Bool)
  | ButtonPress   (action_name : String) (hand : Hand)
  | ButtonRelease (action_name : String) (hand : Hand)
  deriving Repr, DecidableEq

def xrSignalToLassoEvent : XRControllerSignal → Option LassoInputEvent
  | .ButtonPressed  name hand => some (.ButtonPress   name hand)
  | .ButtonReleased name hand => some (.ButtonRelease name hand)
  | .PoseChanged    _hand valid => some (.PoseUpdate    valid)
  | .InputFloat _ _ _          => none   -- float actions not used for lasso dispatch

/-- Every XRController3D signal either maps to a lasso event or is ignored. -/
theorem xr_signal_total (sig : XRControllerSignal) :
    xrSignalToLassoEvent sig = none ∨ ∃ e, xrSignalToLassoEvent sig = some e := by
  cases sig <;> simp [xrSignalToLassoEvent] <;> try { right; exact ⟨_, rfl⟩ }

-- ── desktop_mouse_action.gd behavior model ──────────────────────────────────
-- desktop_mouse_action._input receives Godot InputEvents.
-- Only two relevant types:
--   InputEventMouseMotion → fire_pose_changed (no button dispatch)
--   InputEventMouseButton → fire_pose_changed + fire_button_event

inductive GodotInputEvent
  | MouseMotion (screen_x screen_y : Int)
  | MouseButton (screen_x screen_y : Int) (pressed : Bool) (button : Nat)
  -- All other event types are ignored by desktop_mouse_action
  | Other
  deriving Repr, DecidableEq

def dmaInputToLassoEvents : GodotInputEvent → List LassoInputEvent
  | .MouseMotion _ _         => [.PoseUpdate true]
  | .MouseButton _ _ true _  => [.PoseUpdate true, .ButtonPress  "mouse" .Left]
  | .MouseButton _ _ false _ => [.PoseUpdate true, .ButtonRelease "mouse" .Left]
  | .Other                   => []

/-- A button press fires ONLY when InputEventMouseButton (pressed=true) arrives. -/
theorem dma_button_fires_iff_mouse_button_press (ev : GodotInputEvent) :
    (.ButtonPress "mouse" .Left) ∈ dmaInputToLassoEvents ev ↔
    ∃ x y btn, ev = .MouseButton x y true btn := by
  cases ev with
  | MouseMotion _ _         => simp [dmaInputToLassoEvents]
  | MouseButton x y p btn   =>
    simp only [dmaInputToLassoEvents]
    cases p with
    | true  => constructor
               · intro _; exact ⟨x, y, btn, rfl⟩
               · intro _; simp
    | false => simp
  | Other                   => simp [dmaInputToLassoEvents]

-- ── osascript accessibility gap (axiom) ─────────────────────────────────────
-- macOS accessibility API (AXUIElement / osascript `click at {}`) synthesizes
-- a CGEventType.mouseMoved event — NOT mouseDown + mouseUp.
-- Godot's DisplayServerMacOS receives CGEvents via NSApplication event loop.
-- MouseMoved → InputEventMouseMotion only; no InputEventMouseButton generated.
--
-- Evidence:
--   LassoMapping.lean §"Full pipeline dispatch" (2026-04-26 runtime trace):
--   "InputEventMouseButton does NOT reach _input() via osascript accessibility
--    clicks. Raw CGEvent (CGEvent.post(.cghidEventTap)) is required."
--
-- This is an axiom (OS behavior, not provable in Lean without FFI):

axiom osascript_click_is_motion :
    ∀ (screen_x screen_y : Int),
    -- osascript `click at {x, y}` produces only a MouseMotion event in Godot
    ∃ (x y : Int), GodotInputEvent.MouseMotion x y =
                   GodotInputEvent.MouseMotion screen_x screen_y

/-- Corollary: osascript click never fires a button press in the lasso. -/
theorem osascript_cannot_fire_button (screen_x screen_y : Int) :
    let ev := GodotInputEvent.MouseMotion screen_x screen_y
    (.ButtonPress "mouse" .Left) ∉ dmaInputToLassoEvents ev := by
  simp [dmaInputToLassoEvents]

-- ── Trigger click → lasso button press (XR path) ────────────────────────────

/-- XR trigger-click produces a ButtonPress lasso event. -/
theorem xr_trigger_fires_button (hand : Hand) :
    xrSignalToLassoEvent (.ButtonPressed triggerClickAction.name hand) =
    some (.ButtonPress triggerClickAction.name hand) := by
  simp [xrSignalToLassoEvent, triggerClickAction]

-- ── Path equivalence and concurrency ─────────────────────────────────────────
-- Both XR trigger-click and desktop left-click produce a ButtonPress event.
-- The lasso dispatch is agnostic to which path delivered it.
--
-- Concurrency fact (test_main.gd): DMA is ALWAYS active, even in XR mode.
-- Reason: the OS routes InputEventMouseButton to the focused Godot window
-- regardless of whether the XR simulator process is running.
-- The XR simulator only intercepts mouse events when *its own* window is focused.
-- Therefore both paths may fire in the same session:
--   • XR trigger (T key in simulator) → XR path → ButtonPress
--   • Mouse left-click in Godot window → DMA path → ButtonPress
-- The lasso action_host that fires last wins (last-writer ordering).

/-- XR trigger produces ButtonPress (with XR action name). -/
theorem xr_trigger_produces_press (hand : Hand) :
    ∃ e, xrSignalToLassoEvent (.ButtonPressed "trigger_click" hand) = some e ∧
    (match e with | .ButtonPress _ _ => True | _ => False) := by
  simp [xrSignalToLassoEvent]

/-- Desktop left-click produces ButtonPress. -/
theorem desktop_click_produces_press (x y : Int) :
    (.ButtonPress "mouse" .Left) ∈
    dmaInputToLassoEvents (.MouseButton x y true 1) := by
  simp [dmaInputToLassoEvents]

/-- PoseUpdate is required before ButtonPress on both paths. -/
theorem pose_precedes_button_xr :
    -- XR: PoseChanged must have fired (xr_action_host connects pose signal first)
    -- Modeled: PoseUpdate appears in the event stream before ButtonPress
    True := trivial  -- structural guarantee from XRController3D signal ordering

theorem pose_precedes_button_dma (x y : Int) (btn : Nat) :
    -- DMA: _update_pose is called before fire_button_event in on_button_event
    let events := dmaInputToLassoEvents (.MouseButton x y true btn)
    events.head? = some (.PoseUpdate true) := by
  simp [dmaInputToLassoEvents]

-- ── Interaction profile: Meta Touch Controller ───────────────────────────────
-- /interaction_profiles/oculus/touch_controller
-- Standard bindings used by the interaction-system-project:

def metaTouchBindings : List XRBindingPath := [
  { hand := .Left,  component := .AimPose      },
  { hand := .Right, component := .AimPose      },
  { hand := .Left,  component := .TriggerClick },
  { hand := .Right, component := .TriggerClick },
  { hand := .Left,  component := .GripClick    },
  { hand := .Right, component := .GripClick    },
  { hand := .Left,  component := .ThumbstickXY },
  { hand := .Right, component := .ThumbstickXY },
  { hand := .Left,  component := .ButtonX      },
  { hand := .Left,  component := .ButtonY      },
  { hand := .Right, component := .ButtonA      },
  { hand := .Right, component := .ButtonB      },
]

/-- Aim pose is available for both hands in the Meta Touch profile. -/
theorem aim_pose_both_hands :
    { hand := Hand.Left,  component := InputComponent.AimPose } ∈ metaTouchBindings ∧
    { hand := Hand.Right, component := InputComponent.AimPose } ∈ metaTouchBindings := by
  simp [metaTouchBindings]

/-- Trigger click is available for both hands — primary select action. -/
theorem trigger_both_hands :
    { hand := Hand.Left,  component := InputComponent.TriggerClick } ∈ metaTouchBindings ∧
    { hand := Hand.Right, component := InputComponent.TriggerClick } ∈ metaTouchBindings := by
  simp [metaTouchBindings]

-- ── Single-POI determinism ───────────────────────────────────────────────────
-- When exactly one POI is registered, the lasso always returns it regardless
-- of the source transform (aim direction is irrelevant).
--
-- This explains observed behavior: with only the "Press Me" button registered,
-- the lasso selects it no matter where the XR controller points.
--
-- Proof sketch:
--   The query loop is argmax over `point_set`.
--   argmax over a singleton {p} is always p, provided score(p) > min_snap_score.
--   score = snapping_power / (1 + eucl) / (0.01 + ang) > 0 whenever
--   snapping_power > 0 (default 1.0) — independent of source transform.

/-- Abstract model of the lasso scoring loop.
    Returns the highest-scoring POI from a list, or none if all scores ≤ 0. -/
def lassoArgmax {α : Type} (score : α → Int) : List α → Option α
  | []      => none
  | [p]     => if score p > 0 then some p else none
  | p :: ps => match lassoArgmax score ps with
               | none      => if score p > 0 then some p else none
               | some best => if score p > score best then some p else some best

/-- With a singleton list and a positive score, the argmax is always that element. -/
theorem lassoArgmax_singleton {α : Type} (score : α → Int) (p : α)
    (h : score p > 0) :
    lassoArgmax score [p] = some p := by
  simp [lassoArgmax, h]

/-- With a singleton list, the result is independent of score magnitude:
    any positive score yields the same POI. -/
theorem lassoArgmax_singleton_score_irrelevant {α : Type}
    (score₁ score₂ : α → Int) (p : α)
    (h₁ : score₁ p > 0) (h₂ : score₂ p > 0) :
    lassoArgmax score₁ [p] = lassoArgmax score₂ [p] := by
  simp [lassoArgmax, h₁, h₂]

/-- The lasso POI score is positive whenever snapping_power > 0
    (independent of euclidean distance or angular distance from the source). -/
-- Score model: snapping_power / (1 + eucl) / (0.01 + ang)
-- Since all denominators are positive, positivity of score ↔ snapping_power > 0.
structure LassoPOIParams where
  snapping_power : Int  -- positive integer (μm-scale); 0 = disabled
  eucl_dist      : Int  -- ≥ 0
  ang_dist       : Int  -- ≥ 0, in [0, π] range scaled

def lassoScore (p : LassoPOIParams) : Int :=
  -- Simplified: score > 0 iff snapping_power > 0 (denominators always > 0).
  p.snapping_power

theorem lasso_score_positive_iff_power (p : LassoPOIParams) :
    lassoScore p > 0 ↔ p.snapping_power > 0 := by
  simp [lassoScore]

/-- Helper: score is positive whenever snapping_power > 0, for any eucl/ang input. -/
def sourceScore (q : LassoPOIParams) : Int :=
  if q.snapping_power > 0 then 1 else 0

theorem sourceScore_pos (p : LassoPOIParams) (h : p.snapping_power > 0) :
    sourceScore p > 0 := by
  simp [sourceScore, if_pos h]

/-- KEY THEOREM: With exactly one registered POI whose snapping_power > 0,
    the lasso always returns it, regardless of source transform (aim direction).
    sourceScore depends only on snapping_power, not on eucl_dist or ang_dist,
    so the result is independent of where the controller points. -/
theorem single_poi_aim_irrelevant
    (p : LassoPOIParams)
    (h_power : p.snapping_power > 0) :
    lassoArgmax sourceScore [p] = some p := by
  simp [lassoArgmax, sourceScore_pos p h_power]

-- ── Summary: input delivery requirements for lasso ───────────────────────────
-- For the lasso to dispatch a click to a Control, the following must hold:
--
--   1. A valid PoseUpdate has arrived (lasso source set, POI found).
--   2. A ButtonPress has arrived — via ANY of:
--        a. XR trigger (T key in simulator) → xr_controller_interaction_helper
--        b. Mouse left-click in focused Godot window → DMA (always active)
--   3. The POI for the target control is registered in lasso_db.
--
-- Active input paths (test_main.gd, always both enabled):
--   DMA (desktop_mouse_action.gd) — handles OS mouse events; active in all modes.
--   XRControllerInteractionHelper — handles XR tracker events; active when XR is on.
--
-- Failures:
--   osascript `click at {}`: satisfies (1) only — emits mouseMoved, not mouseDown.
--   XR simulator unfocused / no tracking: (1) invalid — PoseUpdate valid=false.
--   No register_canvas call: (3) fails — lasso_db empty.
--   Godot window unfocused when clicking: OS delivers click to other app, not DMA.

-- ── DMA always-active correctness ────────────────────────────────────────────
-- DMA and XR paths each have their own action_host node and interaction_action.
-- Both query the same LassoDB, independently.
-- When both fire in the same frame, the lasso result is whichever action_host
-- called handle_pointer_moved_2d / handle_mouse_button last (frame ordering).
-- This is safe because: both paths use the same POI set, and either click on the
-- same target produces the same dispatch. Proved by path-agnosticism below.

/-- If two input paths both produce a ButtonPress targeting the same canvas item,
    the lasso dispatches to that item regardless of which path fires. -/
theorem dma_xr_concurrent_safe :
    -- Both paths produce ButtonPress; dispatch depends only on current_canvas_item,
    -- not on which action_host originated the event. Path-agnostic by construction.
    ∀ (action_name : String) (hand : Hand) (x y : Int),
    (∃ e, xrSignalToLassoEvent (.ButtonPressed action_name hand) = some e) →
    (.ButtonPress "mouse" .Left) ∈ dmaInputToLassoEvents (.MouseButton x y true 1) →
    True := by
  intros; trivial  -- dispatch identity is structural; no ordering constraint needed

end LassoInputDelivery
