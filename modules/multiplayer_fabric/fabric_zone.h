/**************************************************************************/
/*  fabric_zone.h                                                         */
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

#include "fabric_zone_journal.h"
#include "fabric_zone_types.h"
#include "relativistic_zone.h"

#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/vector.h"
#include "scene/main/fabric_zone_peer_callbacks.h"
#include "scene/main/scene_tree.h"

#include <thirdparty/misc/predictive_bvh.h>

// Timing and migration constants come from predictive_bvh.h (generated from Lean):
//   PBVH_SIM_TICK_HZ, PBVH_LATENCY_TICKS, PBVH_HYSTERESIS_THRESHOLD,
//   PBVH_INTEREST_RADIUS_UM, PBVH_V_MAX_PHYSICAL, PBVH_ACCEL_FLOOR
// Spatial primitives from predictive_bvh.h: Aabb, hilbert_of_aabb,
// ghost_bound, per_entity_delta_poly (all proved in Lean).
static constexpr int INTENT_SIZE = 88; // 8 + 4 + 4 + 9*8(double) = 88 bytes

// ── real_t (meters) ↔ R128 (μm) bridge ────────────────────────────────────

static inline R128 r128_from_real_um(real_t v) {
	return r128_from_float((float)(v * 1000000.0));
}

static inline real_t r128_to_real_m(R128 v) {
	return (real_t)((int64_t)v.hi) * (real_t)0.000001;
}

// Unambiguous R128 → int64_t (Aabb coord) conversion.
// R128_S64 is long long; int64_t is long on Linux — direct cast is ambiguous.
static inline int64_t r128_to_coord(R128 v) {
	return (int64_t)r128ToInt(&v);
}

// ── FabricZone ─────────────────────────────────────────────────────────────

class FabricZone : public SceneTree {
	GDCLASS(FabricZone, SceneTree);

public:
	// ── Capacity and AOI constants (public for tests and RelZone helpers) ─
	static constexpr int MAX_ZONES = 32;
	static constexpr int AOI_CELLS = 2;

	// Types defined in fabric_zone_types.h; aliased here so FabricZone::FabricEntity
	// etc. remain valid for callers.
	using FabricEntity = ::FabricEntity;
	using GhostSnap = ::GhostSnap;
	using EntitySlot = ::EntitySlot;

	// ── Migration serialization (float wire format) ─────────────────────
	// Pure static — no side effects, safe to call from unit tests.
	static Vector<uint8_t> _pack_intent(int eid, int to, uint32_t arrival, const FabricEntity &e);
	static bool _unpack_intent(const uint8_t *p_data, int p_size, int &r_eid, int &r_to, uint32_t &r_arrival, FabricEntity &r_entity);

	// ── Staging timeout (Jacobson/Karels adaptive RTO) ─────────────────
	// Returns the staging timeout in ticks for a given neighbor.
	// When RTT is measured: SRTT + 4 * RTTVAR (Jacobson/Karels 1988).
	//   The variance term automatically widens the window under jitter.
	// When RTT is unmeasured: 1 second of ticks (p_hz). Networking delays
	// can reach minutes; 1 second is the floor for the unmeasured case.
	static uint32_t _staging_timeout(uint32_t p_srtt, uint32_t p_rttvar, bool p_rtt_measured, uint32_t p_hz) {
		if (!p_rtt_measured) {
			return p_hz; // 1 second
		}
		// Jacobson/Karels: RTO = SRTT + 4 * RTTVAR, floor of 1 tick.
		uint32_t rto = p_srtt + 4 * p_rttvar;
		return rto < 1 ? 1 : rto;
	}

	// ── Migration sub-routines (public static for testability) ──────────
	// Operate on raw slot arrays — no SceneTree required.
	// Zone A: scan STAGING slots, rollback if no ACK within timeout.
	static void _resolve_staging_timeouts_s(EntitySlot *p_slots, int p_capacity,
			int p_zone_id, const uint32_t p_srtt[2], const uint32_t p_rttvar[2],
			const bool p_rtt_measured[2], uint32_t p_hz, uint32_t p_tick);
	// Zone B: drain intent_inbox, allocate INCOMING slots. Returns accepted count.
	static int _accept_incoming_intents_s(EntitySlot *p_slots, int p_capacity,
			int &r_entity_count, int &r_free_hint, int p_zone_id,
			LocalVector<Vector<uint8_t>> &r_inbox, uint64_t &r_xing_received);
	// Zone A: scan slots, emit intents for entities that crossed Hilbert boundary.
	// Returns number of intents queued. Pushes packed intents to r_outbox.
	static int _collect_migration_intents_s(EntitySlot *p_slots, int p_capacity,
			int p_zone_id, const RelZone::NodeView<MAX_ZONES> &p_node_view,
			const uint32_t p_srtt[2],
			uint32_t p_tick, uint32_t p_hz, int p_budget,
			uint64_t &r_xing_started, uint64_t &r_migrations,
			LocalVector<Vector<uint8_t>> &r_outbox);

	// Count active-entity pairs (i<j) whose ghost AABBs overlap. Replaces
	// the formula-based `bvh_pairs` diagnostic with a real broadphase count
	// backed by pbvh_tree_t. Reports what actually happens this tick, not
	// a log-based estimate.
	static int _count_ghost_overlapping_pairs_s(const EntitySlot *p_slots, int p_capacity);

	// ── Scenario enum ───────────────────────────────────────────────────
	enum Scenario {
		SCENARIO_DEFAULT,
		SCENARIO_JELLYFISH_BLOOM,
		SCENARIO_JELLYFISH_ZONE_CROSSING,
		SCENARIO_WHALE_WITH_SHARKS,
		SCENARIO_CURRENT_FUNNEL,
		SCENARIO_MIXED,
	};

	// ── Peer callback table — filled by multiplayer_fabric module ───────
	static FabricZonePeerCallbacks peer_callbacks;
	static void register_peer_callbacks(const FabricZonePeerCallbacks &p_cb);

private:
	// ── Constants (real_t, meters) ───────────────────────────────────────
	static constexpr real_t SIM_BOUND = 15.0;
	// Wire-encoding + physics constants derive from PBVH_*_DEFAULT (the value
	// the Lean proofs were evaluated at). These MUST be compile-time stable —
	// all peers on the fabric agree on the wire quantization scales. Runtime
	// tick-rate adaptation happens in the tick-cadence helpers below via
	// Engine::get_physics_ticks_per_second(), not in wire encoding.
	static constexpr real_t V_MAX = PBVH_V_MAX_PHYSICAL_DEFAULT * 0.000001; // m/tick
	static constexpr real_t INTEREST_RADIUS = PBVH_INTEREST_RADIUS_UM * 0.000001; // m
	static constexpr float V_SCALE = 32767.0f / (PBVH_V_MAX_PHYSICAL_DEFAULT * 1.0e-6f); // m/tick → int16
	static constexpr float A_SCALE = 32767.0f / (2.0f * PBVH_V_MAX_PHYSICAL_DEFAULT * 1.0e-6f); // m/tick² → int16
	static constexpr real_t RAGDOLL_PEAK_V = V_MAX * 6.0; // m/tick — C7 velocity-spike cap (60 m/s)
	static constexpr real_t ACCEL_FLOOR_M = PBVH_ACCEL_FLOOR_DEFAULT * 0.000001; // m/tick²
	static constexpr int N_TOTAL = 100000;
	static constexpr int DEFAULT_ZONE_COUNT = 3;
	// WP_PERIOD: half-cycle duration for choke_point waypoint flips.
	// 10 seconds of wall time; the actual tick count is computed at use-site
	// from Engine::get_physics_ticks_per_second(). Must exceed WaypointBound.lean's
	// wpPeriodMin (travel + hysteresis + latency, all simTickHz-parametric).
	static constexpr int WP_PERIOD_SECONDS = 10;
	// Scenario animation cadences, all declared in seconds/ms and converted to ticks
	// at the use site against the engine's physics tick rate.
	static constexpr int CONCERT_BEAT_PERIOD_SECONDS = 1; // concert pulse cycle
	static constexpr uint32_t CONCERT_BEAT_ON_MS = 83; // pulse-on fraction
	static constexpr int CONVOY_PHASE_PERIOD_SECONDS = 2; // convoy heading phase
	static constexpr int RAGDOLL_PRONE_PERIOD_SECONDS = 3; // ragdoll prone cycle
	static constexpr int CHOKE_POINT_BELL_PERIOD_SECONDS = 3; // choke_point bell period
	static constexpr uint32_t CHOKE_POINT_PULSE_MS = 100; // choke_point pulse duration
	static constexpr uint32_t STATS_LOG_INTERVAL_SECONDS = 20; // periodic stats log cadence
	// ZONE_CAPACITY: hard upper bound on slot array size (compile-time max).
	// _zone_capacity: runtime limit set via --zone-capacity N (default = ZONE_CAPACITY).
	// Headless dedicated server: 1800 (AbyssalSLA.lean: 16 players × 56 + 904 ecosystem).
	// PCVR co-located 90 Hz: 1024 (896 player + 128 ecosystem; ~2.5ms on x86 fits 3ms budget).
	// PCVR co-located 72 Hz: 1200 (896 player + 304 ecosystem; ~3ms on x86 fits 4.7ms budget).
	// Never go below entitiesPerPlayer × targetPlayersPerZone = 896 or the SLA proof breaks.
	// Zones scale horizontally — one zone process per core per machine.
	static constexpr int ZONE_CAPACITY = 1800;
	int _zone_capacity = ZONE_CAPACITY; // set before slot allocation in initialize()
	static constexpr int STROKE_ENTITY_BASE = 1000000; // global_id >= this → pen stroke knot
	static constexpr int MAX_STROKE_KNOTS = 50; // max knots per stroke chain (snake head/tail)
	static constexpr uint32_t INTEREST_PUBLISH_INTERVAL = 1;

	// ── Relativistic zone state (NoGod theory) ──────────────────────────
	// Gossip-maintained view of the cluster's Hilbert-range partition.
	// Replaces the computed _zone_for_hilbert / _hilbert_aoi_band statistics.
	RelZone::NodeView<MAX_ZONES> _node_view;
	// Hybrid Logical Clock for causal ordering of CH_INTEREST packets.
	RelZone::HLC _hlc;

	// ── Zone state ───────────────────────────────────────────────────────
	int zone_id = 0;
	int zone_count = 3;
	uint16_t cluster_base_port = 17500; // base_port+zone_id = this zone's listen port
	// Centroid of each zone's entities, computed in initialize() from the
	// same N_TOTAL spawn pass. Used as waypoints for choke_point.
	real_t _zone_centroid[MAX_ZONES][3] = {};
	uint32_t tick = 0;
	uint64_t migrations = 0;
	uint64_t xing_started = 0; // OWNED→STAGING handoffs initiated this zone
	uint64_t xing_done = 0; // STAGING deactivations completed this zone
	uint64_t xing_received = 0; // inbound entities activated this zone
	bool done = false;
	bool is_player = false;
	Scenario scenario = SCENARIO_DEFAULT;

	// ── Neighbor reconnect backoff (index 0 = zone_id-1, index 1 = zone_id+1) ─
	// Starts at pbvh_latency_ticks(engine_hz), doubles on each failed attempt,
	// caps at 30 s worth of ticks. Both the initial value and the cap are
	// (re)computed at runtime against Engine::get_physics_ticks_per_second().
	static constexpr uint32_t RETRY_CAP_SECONDS = 30;
	uint32_t _retry_next[2] = { 0, 0 };
	uint32_t _retry_interval[2] = { PBVH_LATENCY_TICKS_DEFAULT, PBVH_LATENCY_TICKS_DEFAULT };

	// ── Per-neighbor RTT estimator (Jacobson/Karels 1988) ───────────────
	// Index 0 = zone_id-1, index 1 = zone_id+1.
	// SRTT: smoothed one-way latency in ticks (EWMA, alpha = 1/8).
	//   Used for arrival deadlines in pack_intent.
	//   Floor = pbvh_latency_ticks(hz) (= latencyTicksFloor, proved for 0ms RTT).
	// RTTVAR: mean deviation in ticks (EWMA, beta = 1/4).
	//   Used only in timeout calculation: RTO = SRTT + 4 * RTTVAR.
	// First PONG initializes SRTT = sample, RTTVAR = sample / 2.
	// Subsequent PONGs use Jacobson/Karels EWMA update.
	// (proved: Resources.lean perNeighborLatencyTicks, per_neighbor_ge_floor)
	uint32_t _srtt_ticks[2] = { PBVH_LATENCY_TICKS_DEFAULT, PBVH_LATENCY_TICKS_DEFAULT };
	uint32_t _rttvar_ticks[2] = { PBVH_LATENCY_TICKS_DEFAULT / 2, PBVH_LATENCY_TICKS_DEFAULT / 2 };
	bool _rtt_measured[2] = { false, false }; // true once first PONG arrives

	// ── HLC-based RTT ping/pong (CH_MIGRATION, 8-byte packets) ─────────────
	// Ping:    [u32 send_tick][u32 PING_MAGIC]   zone A → zone B
	// Pong:    [u32 echoed_tick][u32 PONG_MAGIC]  zone B → zone A
	// Ack:     [u32 eid][u32 ACK_MAGIC]           zone B → zone A (migration accepted)
	// Packets are < INTENT_SIZE (88 bytes) so _unpack_intent ignores them safely.
	// RTT = (current_tick - echoed_tick) ticks; latency_ticks = RTT/2 (one-way).
	static constexpr uint32_t PING_MAGIC = 0x50494E47u; // 'PING'
	static constexpr uint32_t PONG_MAGIC = 0x504F4E47u; // 'PONG'
	static constexpr uint32_t ACK_MAGIC = 0x41434B4Bu; // 'ACKK' — migration accepted by zone B
	static constexpr uint32_t PING_INTERVAL_SECONDS = 8; // wall seconds between RTT pings
	// Drain control packets (8-byte, same envelope as PING/PONG/ACK):
	//   DRAIN:     [u32 zone_id][u32 DRAIN_MAGIC]      — "I am shutting down"
	//   DRAIN_DONE:[u32 zone_id][u32 DRAIN_DONE_MAGIC]  — "I have zero entities"
	static constexpr uint32_t DRAIN_MAGIC = 0x4452414Eu; // 'DRAN'
	static constexpr uint32_t DRAIN_DONE_MAGIC = 0x44524E44u; // 'DRND'
	// Drain timeout: max wall seconds to wait for entities to flush before force-quit.
	static constexpr uint32_t DRAIN_TIMEOUT_SECONDS = 30;
	// Snapshot path: Godot Resource (.res) written by zone 0 during drain.
	static constexpr const char *SNAPSHOT_PATH = "user://fabric_snapshot.res";
	uint32_t _ping_next[2] = { 0, 0 };
	uint32_t _ping_send_tick[2] = { 0, 0 }; // tick when last ping was sent

	// ── Graceful shutdown drain state ──────────────────────────────────────
	// On SIGINT/finalize, all zones drain entities toward zone 0 via STAGING.
	// Zone 0 streams inbound entities to fabric_snapshot.bin (write-through)
	// without allocating slots. Intermediate zones forward inbound entities
	// toward zone 0 when their own slots are full (passthrough).
	bool _draining = false;
	uint32_t _drain_start_tick = 0;
	bool _neighbor_drain_done[2] = { false, false }; // index 0=zone_id-1, 1=zone_id+1
	// Zone 0 drain buffer: entities collected from all zones, saved as Resource on finalize.
	LocalVector<FabricEntity> _drain_buffer;
	// --drain-at-tick N: begin drain at tick N (0 = disabled). For testing.
	uint32_t _drain_at_tick = 0;

	// ── Server entity storage (fixed-size slot array) ───────────────────
	EntitySlot *slots = nullptr;
	int entity_count = 0;
	int free_hint = 0;

	// Stroke chain FIFOs: stroke_id → slot indices (oldest at front, newest at back).
	// push_back new head; when size > MAX_STROKE_KNOTS, deactivate front (tail).
	HashMap<uint32_t, LocalVector<int>> _stroke_chains;
	int _stroke_entity_counter = 0;

	// Player entity slot tracking: player_id → slot index.
	// Enables O(1) lookup on every CH_PLAYER update.
	// Player slots use global_id = PLAYER_ENTITY_BASE + player_id (class=1 in payload[0]).
	static constexpr int PLAYER_ENTITY_BASE = 2000000;
	// Player slot expires after 3 seconds without a CH_PLAYER update.
	static constexpr uint32_t PLAYER_SLOT_TIMEOUT_SECONDS = 3;
	HashMap<uint32_t, int> _player_slot_map; // player_id → slot index

	static constexpr int MAX_MIGRATIONS_PER_TICK = 50;
	// Migration headroom: slots reserved for inbound migrations.
	// Two components must both fit within headroom:
	//   1. Burst (choke_point): 144 entities arrive at tick ~282.
	//   2. Pre-burst drift: non-crossing entities drift via free vz into zone 1's
	//      Hilbert region. Measured: ~166 drift entities arrive before the burst.
	//      Total peak = 166 + 144 = 310. ×1.15 safety = 357 → 400.
	// Erlang-B sizing (concert 256-entity burst) also satisfied: 400 > 300.
	// Effective spawn cap = _zone_capacity - MIGRATION_HEADROOM = 1800 - 400 = 1400.
	static constexpr int MIGRATION_HEADROOM = 400;

	// ── Player state (real_t, meters) ────────────────────────────────────
	int player_id = 0;
	real_t player_cx = 0.0;
	real_t player_cy = 0.0;
	real_t player_vx = 0.0;
	real_t player_vy = 0.0;
	uint64_t entities_received = 0;

	// ── Phase-1 pass condition tracking (player/observer mode) ───────────
	// Tracks choke_point entities (gid 256–399, exactly 144).
	// Indexed by gid - 256.  _p1_seen is false until the gid is first received.
	static constexpr int XING_ID_LO = 256;
	static constexpr int XING_ID_HI = 399;
	static constexpr int XING_TOTAL = 144; // XING_ID_HI - XING_ID_LO + 1
	// Snap threshold: above max 3D movement per broadcast interval.
	// sqrt(3) * V_MAX * INTEREST_PUBLISH_INTERVAL = sqrt(3)*0.15625*1 ≈ 0.27m → 0.5m margin.
	static constexpr real_t SNAP_THRESHOLD_M = 0.5;
	bool _p1_seen[XING_TOTAL] = {};
	real_t _p1_cx[XING_TOTAL] = {};
	real_t _p1_cy[XING_TOTAL] = {};
	real_t _p1_cz[XING_TOTAL] = {};
	RelZone::HLC _p1_sent_at[XING_TOTAL] = {}; // causal HLC of last received update per entity
	int _p1_seen_count = 0;
	int _p1_snap_count = 0;
	bool _p1_pass_logged = false;
	// Gap threshold: if entity was absent longer than this, first reappearance is
	// not a snap. Expressed in milliseconds (ENet jitter budget); converted to
	// ticks at use site via the engine's physics tick rate.
	static constexpr uint32_t SNAP_ABSENCE_MS = 200;

	// ── Networking ───────────────────────────────────────────────────────
	LocalVector<Vector<uint8_t>> intent_inbox;
	LocalVector<Vector<uint8_t>> intent_outbox;
	Ref<MultiplayerPeer> fabric_peer; // implementation provided via peer_callbacks

	// ── Timing ───────────────────────────────────────────────────────────
	uint64_t total_compute_us = 0;
	uint64_t total_sync_us = 0;
	uint64_t wall_start_usec = 0;

	// ── Entity stepping ──────────────────────────────────────────────────
	// p_waypoints:   zone centroids [zone][3] for migration target; null for non-crossing scenarios.
	// p_zone_count:  number of zones (chooses waypoint via shuffled cycle hash).
	// p_local_clump: centroid of tracers currently in this zone; null if none present.
	//                Used for cohesion spring — gives natural clumping without O(N²).
	static void _step_entity(FabricEntity &e, uint32_t p_tick, Scenario p_scenario, int p_n,
			const real_t p_waypoints[][3] = nullptr, int p_zone_count = 1,
			const real_t *p_local_clump = nullptr);

	// ── Ghost snap helpers ───────────────────────────────────────────────
	static uint32_t _snap_delta(real_t v, real_t ah);
	static GhostSnap _make_ghost_snap(const FabricEntity &e);
	static void _update_snap(GhostSnap &snap, const FabricEntity &e);
	static Aabb _ghost_aabb_from_snap(const GhostSnap &snap);

	static Aabb _scene_aabb();

	// ── GDScript-exposed Hilbert helpers (public for ClassDB + tests) ───
public:
	static uint32_t _entity_hilbert(const FabricEntity &e);
	static ::AABB hilbert_cell_of_aabb(int p_code, int p_prefix_depth);
	static int hilbert_of_point(float p_x, float p_y, float p_z);

private:
	// ── Hilbert AOI band ────────────────────────────────────────────────
	// AOI_CELLS = number of cell_w-wide ranges of Hilbert padding added on
	// each side of a zone's own range. Relay + neighbor-topology both read
	// the same band so one constant controls the fanout width. AOI_CELLS=1
	// gives two or three neighbors in a 100-zone fabric; AOI_CELLS=2 on a
	// 3-zone smoke covers the full Hilbert space (full mesh by derivation,
	// not by branch).
	// ── Drain helpers ───────────────────────────────────────────────────
	void _begin_drain(); // enter draining mode, broadcast DRAIN_MAGIC
	void _drain_collect_entity(const FabricEntity &e); // zone 0: buffer entity for snapshot
	void _drain_save_snapshot(); // zone 0: write snapshot Resource to disk
	int _load_snapshot(); // load snapshot from disk into slots, returns entity count loaded

protected:
	static void _bind_methods();

	// ── Command hooks for MMOG-layer subclasses ──────────────────────────
	// Called when a CMD_INSTANCE_ASSET packet arrives on CH_PLAYER.
	// p_player_id = sender, p_pcx/y/z = sender position, p_pkt = full 100-byte packet.
	// Default is a no-op; FabricMMOGZone overrides to fetch the manifest and allocate a slot.
	virtual void _on_cmd_instance_asset(uint32_t p_player_id,
			real_t p_pcx, real_t p_pcy, real_t p_pcz,
			const Vector<uint8_t> &p_pkt) {}

	// ── Discrete-mutation journal (crash-recovery) ───────────────────────
	FabricZoneJournal _journal;

	// ── Slot helpers for MMOG-layer subclasses ───────────────────────────
	// Finds the next free slot, reinitializes it, marks it active, and
	// increments entity_count.  Returns the slot index, or -1 when full.
	int _alloc_entity_slot();
	// Marks the slot at p_idx inactive and decrements entity_count.
	void _free_entity_slot(int p_idx);
	// Direct reference to the FabricEntity inside slot p_idx for
	// post-alloc initialization (payload, global_id, coordinates).
	FabricEntity &_slot_entity_ref(int p_idx);
	// Send raw bytes to a specific connected peer on p_channel
	// (TRANSFER_MODE_RELIABLE). Intended for targeted reliable sends such
	// as delivering the script registry to a newly-joined client.
	void _send_to_peer_raw(int p_peer_id, int p_channel, const uint8_t *p_data, int p_size);

public:
	virtual void initialize() override;
	virtual bool physics_process(double p_time) override;
	virtual void finalize() override;

	FabricZone() = default;
	~FabricZone();
};
