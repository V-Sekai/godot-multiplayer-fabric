/**************************************************************************/
/*  fabric_zone_types.h                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/math/math_defs.h"

// Plain-old-data types shared by FabricZone and FabricZoneJournal.
// Kept in a separate header to break the circular include between
// fabric_zone.h (which defines FabricZone) and fabric_zone_journal.h
// (which is a member of FabricZone but needs to reference these types).

// ── Entity (real_t, meters) ────────────────────────────────────────
struct FabricEntity {
	real_t cx = 0.0, cy = 0.0, cz = 0.0;
	real_t vx = 0.0, vy = 0.0, vz = 0.0;
	real_t ax = 0.0, ay = 0.0, az = 0.0;
	int global_id = 0;
	// CH_INTEREST wire entry — 100 bytes:
	//   4(gid) + 3×8(cx/cy/cz f64) + 6×2(vx/vy/vz/ax/ay/az int16) + 4(hlc u32) + 14×4(payload)
	//   Offset 28: velocity/accel int16, scale V_SCALE / A_SCALE.
	//   Offset 40: hlc = tick(24b) | counter(8b) — hybrid logical clock.
	//   Offset 44: payload[14]. Unused words are zero.
	// Interpretation is entity-type specific.
	//
	// ── Generic layout (applicable to all games) ───────────────────────
	//   payload[0] = entity_class(8b) | owner_id(16b) | state_flags(8b)
	//             entity_class: game-defined type tag (0=NPC, 1=player-owned, 2=prop, 3=effect…)
	//             owner_id:     player or server that spawned this entity
	//             state_flags:  8 game-specific 1-bit flags (active, visible, interactable…)
	//   payload[1] = subtype_a(16b) | subtype_b(16b)   — game-specific secondary classification
	//   payload[2..3] = spatial_extra — game-specific packed positional/orientation data
	//   payload[4..7] = game-mode payload — health/ammo, animation state, item IDs, blendshapes…
	//
	// ── Abyssal VR Grid (CONCEPT.md) concrete layout ───────────────────
	//   concert (global_id 0–255):
	//     payload[0] = class=0 | owner_id=server | beat_phase(8b)
	//     payload[1] = kick_timer(16b) | reserved
	//   choke_point (global_id 256–399):
	//     payload[0] = class=1 | owner_id=server | crossing_flags(8b)
	//     payload[1] = group_id(4b) | waypoint_idx(6b) | reserved
	//   convoy (global_id 400–511):
	//     payload[0] = class=2 | owner_id=server | is_lead_cabin(1b)|vehicle_id(7b)
	//     payload[1] = slot_idx(4b) | reserved
	//   pen stroke knots (global_id >= STROKE_ENTITY_BASE):
	//     payload[0] = class=3 | owner_id=player_id | reserved
	//     payload[1] = stroke_id (u32)
	//     payload[2] = stroke_color RGBA8888 (u32)
	//     payload[3] = anchor_cx int16 (lo) | anchor_cy int16 (hi)  ±SIM_BOUND→±32767
	//     payload[4] = anchor_cz int16 (lo) | reserved (hi)
	//
	// ── V-Sekai VR social (extension example) ──────────────────────────
	//   55 humanoid bones per player (godot-humanoid-project BoneCount).
	//   Rotation stored as 3-axis muscle triplet (bone_swing_twists Vector3 ±1),
	//   NOT as a quaternion — calculate_humanoid_rotation() reconstructs at runtime.
	//     payload[0] = class=4 | player_id(16b) | bone_index(6b)|dof_mask(2b)
	//     payload[1] = axis_x int16 (lo) | axis_y int16 (hi)   — swing ±1→±32767
	//     payload[2] = axis_z int16 (lo) | reserved (hi)        — twist axis
	//     payload[3] = held_item_left(16b) | held_item_right(16b)
	//     payload[4] = expression_preset(8b)|voice_active(1b)|group_id(7b)|anim_state(16b)
	//     payload[5..7] = blendshapes / game-mode specific
	//     payload[8..13] = reserved / future extension
	//
	uint32_t payload[14] = {};
};

// ── Ghost snap (real_t, meters; proved computations use R128 at call site) ─
struct GhostSnap {
	real_t cx = 0.0, cy = 0.0, cz = 0.0;
	real_t vx = 0.0, vy = 0.0, vz = 0.0;
	real_t max_ahx = 0.0, max_ahy = 0.0, max_ahz = 0.0;
	uint32_t per_delta = 1;
	uint32_t ticks_since_snap = 0;
	real_t ghost_ey = 0.0;
	uint32_t ghost_hilbert = 0;
};

// ── Entity slot (migration state machine) ──────────────────────────
// Migration state machine (matches Lean: Fabric.lean MigrationState):
//   OWNED:    active=true,  is_staging=false, is_incoming=false  (normal)
//   STAGING:  active=true,  is_staging=true,  is_incoming=false  (zone A, sent intent)
//   INCOMING: active=true,  is_staging=false, is_incoming=true   (zone B, received intent)
// Transitions:
//   OWNED → STAGING (zone A): when entity crosses Hilbert boundary after hysteresis
//   STAGING → OWNED (zone A): on ACK from zone B, or rollback on timeout
//   INCOMING → OWNED (zone B): one tick after receipt; sends ACK to zone A
struct EntitySlot {
	FabricEntity entity;
	GhostSnap snap;
	uint32_t hysteresis = 0;
	bool active = false;
	bool is_staging = false;
	uint32_t staging_send_tick = 0;
	int migration_target_zone = -1;
	bool is_incoming = false;
	int incoming_from_zone = -1;
	bool is_player_slot = false;
	uint32_t last_update_tick = 0;
};
