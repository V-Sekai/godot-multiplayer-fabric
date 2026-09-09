/**************************************************************************/
/*  relativistic_zone.h                                                   */
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

// Relativistic zone theory: no ego, no god, no determinism, gossip authority.
//
// C++ translation of PredictiveBVH.Relativistic.NoGod (Lean 4).
// Formal basis:
//   Gilbert & Golab, "Making Sense of Relativistic Distributed Systems",
//   DISC 2014. doi:10.1007/978-3-662-45174-8_25
//
//   Baldwin et al., "Relaxation for Efficient Asynchronous Queues",
//   arXiv:2503.02164, SIROCCO 2025.
//
// Design invariants (mirroring Lean proofs):
//   1. No global clock: all ordering is via VClock<N> (causal vector clocks).
//   2. No privileged coordinator: authority is a pure function of Hilbert code.
//   3. No canonical ordering: concurrent ops are freely reorderable.
//   4. DisjointRanges: every Hilbert code is owned by at most one zone.
//   5. HLC embeds into VClock<1> for single-node backward compatibility.

#include "core/templates/local_vector.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace RelZone {

// ---------------------------------------------------------------------------
// RelOptional<T>: minimal replacement for std::optional, avoids C++17
// dependency in Godot module builds.
// ---------------------------------------------------------------------------

template <typename T>
struct RelOptional {
	bool has_value = false;
	T value{};

	static RelOptional<T> none() { return {}; }
	static RelOptional<T> some(const T &v) {
		RelOptional<T> o;
		o.has_value = true;
		o.value = v;
		return o;
	}

	const T &operator*() const { return value; }
	T &operator*() { return value; }
	explicit operator bool() const { return has_value; }
};

// ---------------------------------------------------------------------------
// Section 1: VClock<N>
// Causal vector clock. Each node increments its own slot; merge takes max.
// Corresponds to Lean: structure VClock (n : Nat) where ticks : Fin n → Nat
// ---------------------------------------------------------------------------

template <std::size_t N>
struct VClock {
	std::array<uint64_t, N> ticks{};

	// Increment this node's slot (self < N).
	VClock tick(std::size_t self) const {
		VClock result = *this;
		result.ticks[self] += 1;
		return result;
	}

	// Componentwise maximum.
	static VClock merge(const VClock &a, const VClock &b) {
		VClock result;
		for (std::size_t i = 0; i < N; ++i) {
			result.ticks[i] = std::max(a.ticks[i], b.ticks[i]);
		}
		return result;
	}

	// a ≤ b iff every component of a ≤ corresponding component of b.
	static bool le(const VClock &a, const VClock &b) {
		for (std::size_t i = 0; i < N; ++i) {
			if (a.ticks[i] > b.ticks[i]) {
				return false;
			}
		}
		return true;
	}

	// Strict causality: a < b iff a ≤ b and some component strictly less.
	static bool lt(const VClock &a, const VClock &b) {
		if (!le(a, b)) {
			return false;
		}
		for (std::size_t i = 0; i < N; ++i) {
			if (a.ticks[i] < b.ticks[i]) {
				return true;
			}
		}
		return false;
	}

	// Concurrent: neither causally precedes the other.
	// Concurrent ops are freely reorderable (the "no determinism" property).
	static bool concurrent(const VClock &a, const VClock &b) {
		return !lt(a, b) && !lt(b, a);
	}

	bool operator==(const VClock &o) const { return ticks == o.ticks; }
	bool operator!=(const VClock &o) const { return ticks != o.ticks; }
};

// ---------------------------------------------------------------------------
// Section 2: ZoneRange
// A contiguous Hilbert-code interval owned by one zone.
// Authority is pure geometry: no coordinator needed.
// Corresponds to Lean: structure ZoneRange where zoneId lo hi : Nat
// ---------------------------------------------------------------------------

struct ZoneRange {
	uint32_t zone_id = 0;
	uint32_t lo = 0;
	uint32_t hi = 0;

	bool contains(uint32_t h) const { return lo <= h && h <= hi; }
};

// DisjointRanges invariant: every Hilbert code belongs to at most one zone.
// Returns true iff the invariant holds over the given list.
// Corresponds to Lean: DisjointRanges : List ZoneRange → Prop
inline bool disjoint_ranges(const LocalVector<ZoneRange> &zones) {
	for (uint32_t i = 0; i < zones.size(); ++i) {
		for (uint32_t j = i + 1; j < zones.size(); ++j) {
			if (zones[i].lo <= zones[j].hi && zones[j].lo <= zones[i].hi) {
				return false;
			}
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Section 3: NodeView / GossipMsg / receive
// Gossip merge: adopt remote ranges only when causally dominated.
// No god: range adoption requires causal dominance, not a coordinator ACK.
// Corresponds to Lean: NodeView, GossipMsg, NodeView.receive
// ---------------------------------------------------------------------------

template <std::size_t N>
struct GossipMsg {
	std::size_t sender = 0;
	VClock<N> vc;
	LocalVector<ZoneRange> ranges;
};

template <std::size_t N>
struct NodeView {
	std::size_t self_id = 0;
	VClock<N> clock;
	LocalVector<ZoneRange> ranges;

	// Merge an incoming gossip message.
	// Clock always advances (merge).
	// Ranges adopted only if sender's clock causally dominates ours.
	NodeView receive(const GossipMsg<N> &msg) const {
		NodeView result = *this;
		result.clock = VClock<N>::merge(clock, msg.vc);
		if (VClock<N>::le(clock, msg.vc)) {
			result.ranges = msg.ranges;
		}
		return result;
	}
};

// Build a NodeView whose ranges partition [0, 2^30) into zone_count uniform
// Hilbert cells. Mirrors Fabric.lean assignToZone / zonePrefixDepth.
// The last zone's hi is extended to 2^30-1 to ensure full coverage (matches
// the CLAMP(z, 0, count-1) behavior of the old _zone_for_hilbert).
template <std::size_t N>
inline NodeView<N> node_view_from_zone_count(std::size_t self_id, int zone_count) {
	NodeView<N> view;
	view.self_id = self_id;
	view.clock.ticks[self_id] = 0;
	uint32_t total = 1u << 30;
	if (zone_count <= 1) {
		ZoneRange r;
		r.zone_id = 0;
		r.lo = 0;
		r.hi = total - 1;
		view.ranges.push_back(r);
		return view;
	}
	int depth = 0;
	uint32_t x = (uint32_t)(zone_count - 1);
	while (x > 0) {
		depth++;
		x >>= 1;
	}
	uint32_t cell_w = 1u << (30 - depth);
	for (int z = 0; z < zone_count; z++) {
		ZoneRange r;
		r.zone_id = (uint32_t)z;
		r.lo = (uint32_t)z * cell_w;
		r.hi = (z == zone_count - 1) ? total - 1 : r.lo + cell_w - 1;
		view.ranges.push_back(r);
	}
	return view;
}

// ---------------------------------------------------------------------------
// Section 4: geometricAuthority / geometricInterest
// Pure-function authority and interest: no RPC, no coordinator.
// Authority: first zone whose Hilbert range contains h.
// Interest: all zones whose range overlaps the AoI band [h-aoi, h+aoi].
// Corresponds to Lean: geometricAuthority, geometricInterest
// ---------------------------------------------------------------------------

template <std::size_t N>
inline RelOptional<ZoneRange> geometric_authority(
		const NodeView<N> &view, uint32_t h) {
	for (uint32_t i = 0; i < view.ranges.size(); i++) {
		if (view.ranges[i].contains(h)) {
			return RelOptional<ZoneRange>::some(view.ranges[i]);
		}
	}
	return RelOptional<ZoneRange>::none();
}

template <std::size_t N>
inline LocalVector<ZoneRange> geometric_interest(
		const NodeView<N> &view, uint32_t h, uint32_t aoi) {
	LocalVector<ZoneRange> result;
	for (uint32_t i = 0; i < view.ranges.size(); i++) {
		const ZoneRange &r = view.ranges[i];
		uint32_t band_lo = (h >= aoi) ? (h - aoi) : 0;
		uint32_t band_hi = h + aoi;
		if (r.lo <= band_hi && band_lo <= r.hi) {
			result.push_back(r);
		}
	}
	return result;
}

// zone_for_hilbert: returns the zone_id that owns Hilbert code h.
// Falls back to the last zone's id when h exceeds all ranges (mirrors
// CLAMP(z, 0, count-1) in the old _zone_for_hilbert).
template <std::size_t N>
inline int zone_for_hilbert(const NodeView<N> &view, uint32_t h) {
	int last_id = 0;
	for (uint32_t i = 0; i < view.ranges.size(); i++) {
		last_id = (int)view.ranges[i].zone_id;
		if (view.ranges[i].contains(h)) {
			return last_id;
		}
	}
	return last_id;
}

// aoi_band_cells: fills [out_lo, out_hi] for zone my_zone_id's AOI band,
// extending by aoi_cells cell-widths on each side.
// Replaces FabricZone::_hilbert_aoi_band (now removed).
// Cell width is inferred from the zone's [lo, hi] range in the NodeView.
template <std::size_t N>
inline bool aoi_band_cells(const NodeView<N> &view, uint32_t my_zone_id,
		uint32_t aoi_cells, uint32_t &out_lo, uint32_t &out_hi) {
	for (uint32_t i = 0; i < view.ranges.size(); i++) {
		const ZoneRange &r = view.ranges[i];
		if (r.zone_id != my_zone_id) {
			continue;
		}
		uint32_t cell_w = r.hi - r.lo + 1;
		uint32_t aoi = aoi_cells * cell_w;
		out_lo = r.lo > aoi ? r.lo - aoi : 0u;
		uint32_t total = 1u << 30;
		uint32_t raw_hi = r.hi + aoi;
		out_hi = (raw_hi > r.hi && raw_hi <= total) ? raw_hi : total;
		return true;
	}
	return false;
}

// in_interest_band: returns true if Hilbert code h falls within zone
// my_zone_id's AOI band (own range padded by aoi_cells on each side).
template <std::size_t N>
inline bool in_interest_band(const NodeView<N> &view, uint32_t my_zone_id,
		uint32_t aoi_cells, uint32_t h) {
	uint32_t lo = 0, hi = 0;
	if (!aoi_band_cells(view, my_zone_id, aoi_cells, lo, hi)) {
		return false;
	}
	return lo <= h && h <= hi;
}

// ---------------------------------------------------------------------------
// Section 5: QueueOp
// Causally-tagged queue operations.
// hb (happens-before): strict causal order via VClock<N>::lt.
// concurrent: freely reorderable (no canonical global ordering).
// Corresponds to Lean: inductive QueueOp, QueueOp.hb, QueueOp.concurrent
// ---------------------------------------------------------------------------

enum class QueueOpKind : uint8_t {
	Enq = 0,
	Deq = 1,
};

template <std::size_t N>
struct QueueOp {
	QueueOpKind kind = QueueOpKind::Enq;
	std::size_t sender = 0;
	VClock<N> vc;
	uint64_t val = 0;

	// Happens-before: op1 causally precedes op2.
	static bool hb(const QueueOp &op1, const QueueOp &op2) {
		return VClock<N>::lt(op1.vc, op2.vc);
	}

	// Concurrent: neither op causally precedes the other.
	static bool concurrent(const QueueOp &op1, const QueueOp &op2) {
		return VClock<N>::concurrent(op1.vc, op2.vc);
	}
};

// ---------------------------------------------------------------------------
// Section 6: RelReplica
// Ghost entity with causal provenance.
// sent_at replaces the scalar lastTick : Nat from InterestReplica,
// generalizing single-node HLC to n-node VClock.
// Corresponds to Lean: structure RelReplica (n : Nat)
// ---------------------------------------------------------------------------

template <std::size_t N>
struct RelReplica {
	uint64_t entity_id = 0;
	uint32_t author_zone = 0;
	uint32_t hilbert_code = 0;

	int64_t pos_x = 0;
	int64_t pos_y = 0;
	int64_t pos_z = 0;
	int64_t vel_x = 0;
	int64_t vel_y = 0;
	int64_t vel_z = 0;
	int64_t acc_x = 0;
	int64_t acc_y = 0;
	int64_t acc_z = 0;

	VClock<N> sent_at;

	// True if the local node's clock has advanced past what the author sent.
	bool stale(const VClock<N> &local_clock, std::size_t author_idx) const {
		return sent_at.ticks[author_idx] < local_clock.ticks[author_idx];
	}
};

// ---------------------------------------------------------------------------
// Section 7: HLC — Hybrid Logical Clock
// Single-node NTP-physical + logical counter, embeds into VClock<1>.
// Corresponds to Lean: structure HLC, HLC.toVClock
// ---------------------------------------------------------------------------

struct HLC {
	uint64_t pt = 0; // physical time (server tick or NTP ms)
	uint64_t l = 0; // logical counter (tie-break within same physical tick)

	bool operator<(const HLC &o) const {
		if (pt != o.pt) {
			return pt < o.pt;
		}
		return l < o.l;
	}
	bool operator==(const HLC &o) const { return pt == o.pt && l == o.l; }
	bool operator<=(const HLC &o) const { return !(o < *this); }

	// leb: decidable ≤ matching Lean HLC.leb — a ≤ b iff ¬(b < a).
	static bool leb(const HLC &a, const HLC &b) { return !(b < a); }

	// Encode into the existing wire format: tick(24b) | counter(8b).
	// Matches the existing CH_INTEREST packet layout (offset 40).
	uint32_t to_wire() const {
		return ((uint32_t)(pt & 0x00FFFFFFu) << 8) | (uint32_t)(l & 0xFFu);
	}

	// Embed HLC into VClock<1> by encoding (pt, l) as a single monotone uint64.
	// Requires max_l < 2^32 so that pt * (max_l + 1) + l is injective.
	VClock<1> to_vclock(uint64_t max_l) const {
		VClock<1> vc;
		vc.ticks[0] = pt * (max_l + 1) + l;
		return vc;
	}

	// Advance HLC on send: bump pt to now_pt (server tick), reset l if pt
	// advances, otherwise increment l.
	static HLC advance(const HLC &local, uint64_t now_pt) {
		HLC next;
		next.pt = std::max(local.pt, now_pt);
		next.l = (next.pt == local.pt) ? local.l + 1 : 0;
		return next;
	}

	// Merge two HLCs on receive: take the causally later one.
	static HLC merge(const HLC &local, const HLC &remote, uint64_t now_pt) {
		HLC next;
		next.pt = std::max({ local.pt, remote.pt, now_pt });
		if (next.pt == local.pt && next.pt == remote.pt) {
			next.l = std::max(local.l, remote.l) + 1;
		} else if (next.pt == local.pt) {
			next.l = local.l + 1;
		} else if (next.pt == remote.pt) {
			next.l = remote.l + 1;
		} else {
			next.l = 0;
		}
		return next;
	}
};

} // namespace RelZone
