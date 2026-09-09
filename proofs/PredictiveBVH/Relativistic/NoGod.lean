-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee

-- ============================================================================
-- RELATIVISTIC ZONE THEORY: NO EGO, NO GOD, NO DETERMINISM
--
-- Foundations:
--   Gilbert & Golab, DISC 2014 — relativistic linearizability without global clocks
--   Baldwin et al., SIROCCO 2025 — vector clocks for relaxed async queues
--
-- The existing fabric uses a coordinator-assigned range map and global ticks.
-- This theory replaces those three assumptions:
--
--   God-clock (global tick) → VClock (per-node causal counter)
--   Coordinator-assigned range → geometric containment in Hilbert space
--   Deterministic serialization → causal partial order; concurrent ops freely reordered
--
-- Network authority and interest survive the replacement:
--   Authority = the zone whose Hilbert range contains hilbert3D(entity.pos)
--   Interest  = all zones whose ranges are within AOI_CELLS of that code
-- Both are pure functions of position and the gossip-learned range map.
-- No coordinator decides either one.
--
-- The hybrid logical clock (HLC) used by the existing protocol is compatible:
-- the physical component of an HLC maps to the local component of VClock.selfId,
-- and the logical counter maps to the tick count.  VClock generalizes HLC to
-- n nodes without assuming a synchronized wall clock.
-- ============================================================================

namespace PredictiveBVH.Relativistic

-- ============================================================================
-- 1. VECTOR CLOCK  (replaces the global god-tick)
-- ============================================================================

/-- A vector clock over n nodes.  Node i counts events it has witnessed
    at each peer.  The global tick `simTickHz` is replaced by this partial order;
    causality is the only time that exists. -/
structure VClock (n : Nat) where
  ticks : Fin n → Nat

instance {n : Nat} : Inhabited (VClock n) where
  default := { ticks := fun _ => 0 }

instance {n : Nat} : Repr (VClock n) where
  reprPrec vc p := reprPrec (List.ofFn vc.ticks) p

/-- Zero clock: every component is 0. -/
def VClock.zero (n : Nat) : VClock n := { ticks := fun _ => 0 }

/-- Tick: node `self` observes a local event; only its own component advances. -/
def VClock.tick {n : Nat} (vc : VClock n) (self : Fin n) : VClock n :=
  { ticks := fun i => if i = self then vc.ticks i + 1 else vc.ticks i }

/-- Merge: receive a message; take componentwise max to absorb remote knowledge. -/
def VClock.merge {n : Nat} (a b : VClock n) : VClock n :=
  { ticks := fun i => max (a.ticks i) (b.ticks i) }

/-- Causal ≤: every component of a ≤ the corresponding component of b. -/
def VClock.le {n : Nat} (a b : VClock n) : Prop :=
  ∀ i, a.ticks i ≤ b.ticks i

/-- Strict causal <: a ≤ b component-wise AND at least one is strictly less.
    This is the "happened-before" relation (Lamport 1978, extended to vectors). -/
def VClock.lt {n : Nat} (a b : VClock n) : Prop :=
  VClock.le a b ∧ ∃ i, a.ticks i < b.ticks i

/-- Concurrent: neither happened-before the other.
    Gilbert & Golab 2014: concurrent operations may be serialized in either order. -/
def VClock.concurrent {n : Nat} (a b : VClock n) : Prop :=
  ¬ VClock.lt a b ∧ ¬ VClock.lt b a

/-- Decidable instance: VClock.le is decidable because Fin n is finite
    and Nat.le is decidable. -/
instance {n : Nat} (a b : VClock n) : Decidable (VClock.le a b) := by
  unfold VClock.le
  infer_instance

-- ── Partial-order proofs ──────────────────────────────────────────────────────

theorem VClock.le_refl {n : Nat} (vc : VClock n) : VClock.le vc vc :=
  fun _ => Nat.le_refl _

theorem VClock.le_trans {n : Nat} {a b c : VClock n}
    (hab : VClock.le a b) (hbc : VClock.le b c) : VClock.le a c :=
  fun i => Nat.le_trans (hab i) (hbc i)

theorem VClock.lt_irrefl {n : Nat} (vc : VClock n) : ¬ VClock.lt vc vc :=
  fun ⟨_, _i, hi⟩ => Nat.lt_irrefl _ hi

theorem VClock.lt_trans {n : Nat} {a b c : VClock n}
    (hab : VClock.lt a b) (hbc : VClock.lt b c) : VClock.lt a c :=
  ⟨VClock.le_trans hab.1 hbc.1,
   hab.2.imp fun i hi => Nat.lt_of_lt_of_le hi (hbc.1 i)⟩

/-- merge is an upper bound: each input ≤ the merge. -/
theorem VClock.le_merge_left {n : Nat} (a b : VClock n) : VClock.le a (VClock.merge a b) :=
  fun _ => Nat.le_max_left _ _

theorem VClock.le_merge_right {n : Nat} (a b : VClock n) : VClock.le b (VClock.merge a b) :=
  fun _ => Nat.le_max_right _ _

-- ── Lattice properties of merge (red → green: all three proved below) ──────

/-- merge is commutative: the order of inputs does not matter. -/
theorem VClock.merge_comm {n : Nat} (a b : VClock n) :
    VClock.merge a b = VClock.merge b a := by
  unfold VClock.merge
  congr 1; funext i; exact Nat.max_comm _ _

/-- merge is associative. -/
theorem VClock.merge_assoc {n : Nat} (a b c : VClock n) :
    VClock.merge (VClock.merge a b) c = VClock.merge a (VClock.merge b c) := by
  unfold VClock.merge
  congr 1; funext i; exact Nat.max_assoc _ _ _

/-- merge is idempotent: merging with yourself leaves the clock unchanged. -/
theorem VClock.merge_idem {n : Nat} (a : VClock n) :
    VClock.merge a a = a := by
  unfold VClock.merge
  congr 1; funext i; exact Nat.max_self _

/-- tick strictly advances the issuing node's clock.
    Every other component is unchanged; only `self` increments. -/
theorem VClock.lt_tick {n : Nat} (vc : VClock n) (self : Fin n) :
    VClock.lt vc (VClock.tick vc self) := by
  constructor
  · intro i
    simp only [VClock.tick]
    by_cases h : i = self
    · subst h; simp
    · simp [h]
  · exact ⟨self, by simp [VClock.tick]⟩

-- ============================================================================
-- 2. ZONE RANGE  (gossip-learned; authority is containment, not assignment)
-- ============================================================================

/-- A zone's claim over a contiguous slice of the 30-bit Hilbert code space.
    Zones gossip these claims; no coordinator issues them. -/
structure ZoneRange where
  zoneId : Nat
  lo     : Nat   -- inclusive lower bound (Hilbert code, 30-bit)
  hi     : Nat   -- inclusive upper bound (Hilbert code, 30-bit)
  deriving Repr, Inhabited

def ZoneRange.contains (r : ZoneRange) (h : Nat) : Bool :=
  (r.lo ≤ h) && (h ≤ r.hi)

/-- Non-overlapping coverage invariant.
    Under this invariant every Hilbert code maps to at most one authority zone —
    with no coordinator enforcing uniqueness, the gossip protocol must maintain this. -/
def DisjointRanges (zones : List ZoneRange) : Prop :=
  ∀ r1 r2 h, r1 ∈ zones → r2 ∈ zones →
    r1.contains h = true → r2.contains h = true →
    r1.zoneId = r2.zoneId

/-- Under DisjointRanges, the authority zone for a Hilbert code is unique. -/
theorem authority_unique {zones : List ZoneRange} (hdisj : DisjointRanges zones)
    {r1 r2 : ZoneRange} {h : Nat}
    (hm1 : r1 ∈ zones) (hm2 : r2 ∈ zones)
    (hc1 : r1.contains h = true) (hc2 : r2.contains h = true) :
    r1.zoneId = r2.zoneId :=
  hdisj r1 r2 h hm1 hm2 hc1 hc2

-- ============================================================================
-- 3. NODE VIEW  (gossip state — each node's local picture of the range map)
-- ============================================================================

/-- A node's local view: its causal clock and the range map it last learned
    from gossip.  This replaces the coordinator-maintained shard registry. -/
structure NodeView (n : Nat) where
  selfId : Fin n
  clock  : VClock n
  ranges : List ZoneRange

/-- A gossip message: a peer shares its range map along with its causal clock. -/
structure GossipMsg (n : Nat) where
  sender : Fin n
  vc     : VClock n
  ranges : List ZoneRange

/-- Receive a gossip message.
    Always merge clocks (absorb causal knowledge).
    Adopt the new range map only when the incoming clock is causally ≥ ours
    (Baldwin et al. 2025: vector-clock gating prevents stale map regression). -/
def NodeView.receive {n : Nat} (view : NodeView n) (msg : GossipMsg n) : NodeView n :=
  if VClock.le view.clock msg.vc then
    { view with clock := VClock.merge view.clock msg.vc, ranges := msg.ranges }
  else
    { view with clock := VClock.merge view.clock msg.vc }

/-- Both branches of receive produce the same clock: merge(view.clock, msg.vc). -/
@[simp]
theorem receive_clock_eq {n : Nat} (view : NodeView n) (msg : GossipMsg n) :
    (NodeView.receive view msg).clock = VClock.merge view.clock msg.vc := by
  unfold NodeView.receive
  by_cases h : VClock.le view.clock msg.vc
  · rw [if_pos h]
  · rw [if_neg h]

/-- Receiving any gossip message advances the local clock. -/
theorem receive_advances_clock {n : Nat} (view : NodeView n) (msg : GossipMsg n) :
    VClock.le view.clock (NodeView.receive view msg).clock := by
  simp only [receive_clock_eq]
  exact VClock.le_merge_left _ _

/-- The merged clock subsumes the incoming clock. -/
theorem receive_subsumes_sender {n : Nat} (view : NodeView n) (msg : GossipMsg n) :
    VClock.le msg.vc (NodeView.receive view msg).clock := by
  simp only [receive_clock_eq]
  exact VClock.le_merge_right _ _

/-- Gossip preserves DisjointRanges.
    If the current view's ranges are disjoint AND the incoming message's ranges
    are disjoint, then the post-receive view's ranges are also disjoint.
    This is the core "no god" safety invariant: no coordinator is needed because
    the gossip gating (causal dominance) guarantees the adopted map is from a node
    that itself maintained disjointedness.
    Red → green: proved by case analysis on the if-branch. -/
theorem receive_preserves_disjoint {n : Nat} (view : NodeView n) (msg : GossipMsg n)
    (hview : DisjointRanges view.ranges)
    (hmsg  : DisjointRanges msg.ranges) :
    DisjointRanges (NodeView.receive view msg).ranges := by
  unfold NodeView.receive
  by_cases h : VClock.le view.clock msg.vc
  · simp only [if_pos h]
    exact hmsg
  · simp only [if_neg h]
    exact hview

/-- Gossip is monotone: receiving a causally newer message gives a larger clock. -/
theorem receive_clock_monotone {n : Nat} (view : NodeView n) (msg1 msg2 : GossipMsg n)
    (hord : VClock.le msg1.vc msg2.vc) :
    VClock.le (NodeView.receive view msg1).clock (NodeView.receive view msg2).clock := by
  simp only [receive_clock_eq, VClock.le]
  intro i
  simp only [VClock.merge]
  have := hord i
  omega

-- ============================================================================
-- 4. GEOMETRIC AUTHORITY AND INTEREST  (no coordinator needed)
-- ============================================================================

/-- Authority for an entity at Hilbert code `h`: find the zone whose range
    contains h in the gossip-learned map.  Pure geometry; no message needed. -/
def geometricAuthority {n : Nat} (view : NodeView n) (h : Nat) : Option ZoneRange :=
  view.ranges.find? (fun r => r.contains h)

/-- Interest band: all zones whose ranges overlap [h - aoi, h + aoi].
    Computed locally from the gossip map; no AOI subscription to a coordinator. -/
def geometricInterest {n : Nat} (view : NodeView n) (h : Nat) (aoi : Nat) : List ZoneRange :=
  view.ranges.filter (fun r => (r.lo ≤ h + aoi) && (h ≤ r.hi + aoi))

/-- A zone with authority over h is always in the interest band at aoi = 0.
    Proof: contains h ↔ lo ≤ h ∧ h ≤ hi, which equals lo ≤ h+0 ∧ h ≤ hi+0. -/
theorem authority_in_interest_band {n : Nat} (view : NodeView n) (h : Nat)
    (r : ZoneRange) (hauth : r ∈ view.ranges) (hc : r.contains h = true) :
    r ∈ geometricInterest view h 0 := by
  simp only [geometricInterest, List.mem_filter]
  refine ⟨hauth, ?_⟩
  simp only [ZoneRange.contains, Bool.and_eq_true, decide_eq_true_eq, Nat.add_zero] at hc ⊢
  exact hc

/-- Under disjoint ranges, any two zones that both contain h are the same zone. -/
theorem geometric_authority_unique {n : Nat} (view : NodeView n) (h : Nat)
    (hdisj : DisjointRanges view.ranges)
    {r1 r2 : ZoneRange}
    (hm1 : r1 ∈ view.ranges) (hm2 : r2 ∈ view.ranges)
    (hc1 : r1.contains h = true) (hc2 : r2.contains h = true) :
    r1.zoneId = r2.zoneId :=
  authority_unique hdisj hm1 hm2 hc1 hc2

/-- The geometricInterest condition `(r.lo ≤ h + aoi) && (h ≤ r.hi + aoi)` is
    equivalent to the symmetric overlap of [r.lo, r.hi] with [h - aoi, h + aoi].
    The unsafe-subtraction form `h - aoi` is avoided; instead the check is
    reformulated as `h ≤ r.hi + aoi` which is equivalent under Nat arithmetic.
    Proved via omega on the Nat-subtraction-safe reformulation. -/
theorem geometricInterest_overlap_iff {n : Nat} (view : NodeView n) (h aoi : Nat)
    (r : ZoneRange) (hmem : r ∈ view.ranges) :
    r ∈ geometricInterest view h aoi ↔
    r.lo ≤ h + aoi ∧ (aoi ≤ h → r.hi + 1 > h - aoi) := by
  simp only [geometricInterest, List.mem_filter]
  constructor
  · intro ⟨_, hbool⟩
    simp only [Bool.and_eq_true, decide_eq_true_eq] at hbool
    exact ⟨hbool.1, fun haoi => by omega⟩
  · intro ⟨hlo, hhigh⟩
    refine ⟨hmem, ?_⟩
    simp only [Bool.and_eq_true, decide_eq_true_eq]
    refine ⟨hlo, ?_⟩
    by_cases haoi : aoi ≤ h
    · have := hhigh haoi; omega
    · omega

-- ============================================================================
-- 5. RELATIVISTIC QUEUE  (causal ordering; concurrent ops freely reordered)
-- ============================================================================

/-- A queue operation issued by node `sender` at causal time `vc`.
    The global tick in `InterestReplica.lastTick` is replaced by the vector clock;
    freshness is "happened-before", not wall-clock age. -/
inductive QueueOp (α : Type) (n : Nat) where
  | enq (sender : Fin n) (vc : VClock n) (val : α)
  | deq (sender : Fin n) (vc : VClock n)

def QueueOp.vc {α : Type} {n : Nat} : QueueOp α n → VClock n
  | .enq _ v _ => v
  | .deq _ v   => v

/-- Causal ordering on operations: op1 happened-before op2. -/
def QueueOp.hb {α : Type} {n : Nat} (op1 op2 : QueueOp α n) : Prop :=
  VClock.lt op1.vc op2.vc

/-- Concurrent operations: neither happened-before the other.
    Gilbert & Golab 2014 (Def. 3): a relativistically linearizable history
    is allowed to serialize concurrent operations in any order. -/
def QueueOp.concurrent {α : Type} {n : Nat} (op1 op2 : QueueOp α n) : Prop :=
  VClock.concurrent op1.vc op2.vc

/-- hb is a strict partial order on operations (irreflexive and transitive). -/
theorem QueueOp.hb_irrefl {α : Type} {n : Nat} (op : QueueOp α n) :
    ¬ QueueOp.hb op op :=
  VClock.lt_irrefl op.vc

theorem QueueOp.hb_trans {α : Type} {n : Nat} {op1 op2 op3 : QueueOp α n}
    (h12 : QueueOp.hb op1 op2) (h23 : QueueOp.hb op2 op3) :
    QueueOp.hb op1 op3 :=
  VClock.lt_trans h12 h23

/-- The key relativistic property: concurrent operations carry no ordering
    constraint.  A scheduler may process them in either order and remain valid.
    This is the "no determinism" property: concurrent events have no canonical
    serialization; the system is consistent whichever order it picks. -/
theorem concurrent_is_freely_reorderable {α : Type} {n : Nat}
    (op1 op2 : QueueOp α n) (hcon : QueueOp.concurrent op1 op2) :
    ¬ QueueOp.hb op1 op2 ∧ ¬ QueueOp.hb op2 op1 :=
  hcon

/-- Causally ordered operations must respect that order. -/
theorem causal_order_binding {α : Type} {n : Nat}
    (op1 op2 : QueueOp α n) (h : QueueOp.hb op1 op2) :
    ¬ QueueOp.hb op2 op1 :=
  fun h2 => absurd (VClock.lt_trans h h2) (VClock.lt_irrefl _)

-- ============================================================================
-- 6. REPLICA FRESHNESS VIA CAUSALITY  (replaces lastTick + latency expiry)
-- ============================================================================

/-- A relativistic interest replica: ghost of a remote entity, tagged with
    the causal clock at which the authority zone last sent an update.
    Replaces `InterestReplica.lastTick` (absolute tick) with a vector clock.
    Compatible with the HLC used in the existing protocol: the physical component
    maps to the local VClock component; the logical counter maps to the tick. -/
structure RelReplica (n : Nat) where
  entityId    : Nat
  authorZone  : Nat
  hilbertCode : Nat
  posX        : Int
  posY        : Int
  posZ        : Int
  velX        : Int
  velY        : Int
  velZ        : Int
  accX        : Int
  accY        : Int
  accZ        : Int
  sentAt      : VClock n
  deriving Repr, Inhabited

/-- A replica is stale relative to a local clock if the local clock has advanced
    strictly beyond sentAt on the authority node's component.
    No absolute timeout: "stale" means "we know something newer exists causally". -/
def RelReplica.stale {n : Nat} (rep : RelReplica n) (localClock : VClock n)
    (authorIdx : Fin n) : Bool :=
  rep.sentAt.ticks authorIdx < localClock.ticks authorIdx

/-- A replica that is not stale carries the latest causal knowledge we have. -/
theorem fresh_replica_le_clock {n : Nat} (rep : RelReplica n)
    (clock : VClock n) (authorIdx : Fin n)
    (hfresh : rep.stale clock authorIdx = false) :
    clock.ticks authorIdx ≤ rep.sentAt.ticks authorIdx := by
  simp [RelReplica.stale] at hfresh
  omega

-- ============================================================================
-- 7. HYBRID LOGICAL CLOCK COMPATIBILITY
--    (Kulkarni et al. 2014; the protocol already has one)
-- ============================================================================
--
-- An HLC is a pair (pt, l) where pt = physical time (NTP ticks) and
-- l = logical counter used to break ties when two events share the same pt.
-- The existing protocol's InterestReplica.lastTick is the physical component
-- of an implicit single-node HLC.
--
-- Embedding: HLC(pt, l) → VClock 1 where ticks(0) = pt * maxL + l.
-- This embedding is order-preserving: HLC1 < HLC2 iff the corresponding
-- VClocks satisfy VClock.lt.
--
-- For n > 1 nodes: replace the single NTP pt with n independent physical
-- components (one per zone), removing the global-clock assumption entirely.
-- Causality is then governed by VClock.lt alone — no NTP synchronization needed.

/-- A hybrid logical clock: physical time (in ticks) plus a logical counter.
    The physical component advances with wall time; the logical counter breaks ties
    and is reset when physical time advances past the last observed value. -/
structure HLC where
  pt : Nat   -- physical time (NTP ticks, rounded to simTickHz grid)
  l  : Nat   -- logical counter (resets when pt advances)
  deriving Repr, Inhabited, DecidableEq

/-- HLC causal order: (pt1, l1) < (pt2, l2) iff pt1 < pt2 ∨ (pt1 = pt2 ∧ l1 < l2). -/
def HLC.lt (a b : HLC) : Prop :=
  a.pt < b.pt ∨ (a.pt = b.pt ∧ a.l < b.l)

/-- HLC.lt is a strict partial order. -/
theorem HLC.lt_irrefl (h : HLC) : ¬ HLC.lt h h := by
  simp [HLC.lt]

theorem HLC.lt_trans {a b c : HLC} (hab : HLC.lt a b) (hbc : HLC.lt b c) : HLC.lt a c := by
  simp [HLC.lt] at *
  omega

/-- Advance an HLC to a new physical time `nowPt` (e.g. the server tick counter).
    If nowPt > local.pt, reset the logical counter to 0 and bump pt.
    If nowPt ≤ local.pt (same tick or clock stall), keep pt and bump l.
    Matches the C++ HLC::advance in relativistic_zone.h. -/
def HLC.advance (src : HLC) (nowPt : Nat) : HLC :=
  let pt := max src.pt nowPt
  { pt := pt
    l  := if pt = src.pt then src.l + 1 else 0 }

/-- advance always produces a strictly later HLC. -/
theorem HLC.advance_lt {src : HLC} (nowPt : Nat) :
    HLC.lt src (HLC.advance src nowPt) := by
  unfold HLC.advance HLC.lt
  simp only
  by_cases h : max src.pt nowPt = src.pt
  · right
    simp only [h]
    exact ⟨trivial, Nat.lt_succ_self _⟩
  · left
    exact Nat.lt_of_le_of_ne (Nat.le_max_left _ _) (fun heq => h heq.symm)

/-- Receiving a later physical time does not decrease pt. -/
theorem HLC.advance_pt_ge {src : HLC} (nowPt : Nat) :
    src.pt ≤ (HLC.advance src nowPt).pt := by
  simp [HLC.advance, Nat.le_max_left]

/-- Merge two HLCs on receive: pick the causally later one then advance.
    Invariants proved in HLC.merge_ge_local / HLC.merge_ge_remote via split_ifs + omega. -/
def HLC.merge (src remote : HLC) (nowPt : Nat) : HLC :=
  let pt := max (max src.pt remote.pt) nowPt
  { pt := pt
    l  := if pt = src.pt && pt = remote.pt then max src.l remote.l + 1
          else if pt = src.pt then src.l + 1
          else if pt = remote.pt then remote.l + 1
          else 0 }

/-- merge result is causally ≥ both inputs. -/
theorem HLC.merge_ge_local {src remote : HLC} (nowPt : Nat) :
    ¬ HLC.lt (HLC.merge src remote nowPt) src := by
  simp only [HLC.lt, HLC.merge]
  intro h
  rcases h with h | ⟨hpt, hl⟩
  · have : src.pt ≤ max (max src.pt remote.pt) nowPt :=
      Nat.le_trans (Nat.le_max_left _ _) (Nat.le_max_left _ _)
    omega
  · by_cases h1 : max (max src.pt remote.pt) nowPt = src.pt
    · rw [h1] at hl hpt
      by_cases h2 : src.pt = remote.pt
      · rw [h2] at hl
        simp only [decide_true, Bool.and_true, ite_true] at hl
        omega
      · rw [show (decide (src.pt = remote.pt) : Bool) = false from
              decide_eq_false_iff_not.mpr h2] at hl
        simp only [Bool.and_false, Bool.false_eq_true, ite_false, ite_true] at hl
        omega
    · omega

theorem HLC.merge_ge_remote {src remote : HLC} (nowPt : Nat) :
    ¬ HLC.lt (HLC.merge src remote nowPt) remote := by
  simp only [HLC.lt, HLC.merge]
  intro h
  rcases h with h | ⟨hpt, hl⟩
  · have : remote.pt ≤ max (max src.pt remote.pt) nowPt :=
      Nat.le_trans (Nat.le_max_right _ _) (Nat.le_max_left _ _)
    omega
  · by_cases h1 : max (max src.pt remote.pt) nowPt = remote.pt
    · rw [h1] at hl hpt
      by_cases h2 : remote.pt = src.pt
      · rw [h2] at hl
        simp only [decide_true, Bool.and_true, ite_true] at hl
        omega
      · rw [show (decide (remote.pt = src.pt) : Bool) = false from
              decide_eq_false_iff_not.mpr h2] at hl
        simp only [Bool.false_and, Bool.false_eq_true, ite_false, if_neg h2, ite_true] at hl
        omega
    · omega

instance : DecidableEq HLC := inferInstance

/-- Decidable ≤ on HLCs: a ≤ b iff ¬(b < a).  Used in `if`-guards. -/
def HLC.leb (a b : HLC) : Bool :=
  a.pt < b.pt || (a.pt == b.pt && a.l ≤ b.l)

/-- Embed an HLC into a single-node VClock using a linear encoding.
    maxL bounds the logical counter; the slot width is (maxL + 1) so
    different physical times produce non-overlapping ranges. -/
def HLC.toVClock (maxL : Nat) (h : HLC) : VClock 1 :=
  { ticks := fun _ => h.pt * (maxL + 1) + h.l }

/-- If two HLCs share the same physical time and the logical counter advances,
    the embedding reflects that: the VClock is strictly less. -/
theorem HLC.toVClock_lt_of_same_pt (maxL : Nat) (a b : HLC)
    (hpt : a.pt = b.pt) (hl : a.l < b.l) :
    VClock.lt (a.toVClock maxL) (b.toVClock maxL) := by
  have heq : a.pt * (maxL + 1) = b.pt * (maxL + 1) := by rw [hpt]
  exact ⟨fun _ => by simp only [HLC.toVClock]; omega,
         ⟨⟨0, Nat.one_pos⟩, by simp only [HLC.toVClock]; omega⟩⟩

/-- If physical time advances, the embedding is strictly increasing regardless
    of the logical counter (given the counter is bounded by maxL). -/
theorem HLC.toVClock_lt_of_pt_lt (maxL : Nat) (a b : HLC)
    (ha : a.l ≤ maxL) (hpt : a.pt < b.pt) :
    VClock.lt (a.toVClock maxL) (b.toVClock maxL) := by
  have hstep : a.pt + 1 ≤ b.pt :=  hpt
  have hmul  : (a.pt + 1) * (maxL + 1) ≤ b.pt * (maxL + 1) :=
    Nat.mul_le_mul_right _ hstep
  -- distributive law: (a.pt+1)*(maxL+1) = a.pt*(maxL+1) + (maxL+1)
  have hsucc : (a.pt + 1) * (maxL + 1) = a.pt * (maxL + 1) + (maxL + 1) := by
    rw [Nat.add_mul, Nat.one_mul]
  have hkey  : a.pt * (maxL + 1) + maxL + 1 ≤ b.pt * (maxL + 1) := by omega
  exact ⟨fun _ => by simp only [HLC.toVClock]; omega,
         ⟨⟨0, Nat.one_pos⟩, by simp only [HLC.toVClock]; omega⟩⟩

-- ============================================================================
-- 8. THE PARADIGM IN ONE PLACE
-- ============================================================================
--
-- No ego:   every ZoneRange is a peer claim, gossip-learned; no node is
--           pre-ordained as coordinator.
--
-- No god:   authority = ZoneRange.contains (pure geometry).
--           interest  = geometricInterest (proximity in Hilbert space).
--           Neither requires a central registry.
--
-- No determinism: QueueOp.concurrent ops may be processed in any order
--           (concurrent_is_freely_reorderable).  Causally ordered ops
--           are bound (causal_order_binding).  There is no total order.
--
-- Gossip:   NodeView.receive merges VClocks; range adoption is gated on
--           causal dominance (receive_advances_clock,
--           receive_subsumes_sender).  The map converges when all messages
--           are delivered — no leader election needed.
--
-- HLC:      the existing protocol's single-node HLC (pt, l) embeds into
--           VClock 1 via toVClock.  Order is preserved by
--           HLC.toVClock_lt_of_same_pt and HLC.toVClock_lt_of_pt_lt.
--           VClock n generalizes HLC to n nodes, eliminating the NTP
--           synchronization requirement.
--           Authority and interest remain computable from geometry alone.

-- ============================================================================
-- 9. BRIDGE TO Fabric.lean
-- ============================================================================
--
-- Fabric.lean proves `assignToZone_in_range`, `aoiBand_covers_self`, and
-- `aoiBand_width_bound` for the uniform Hilbert partition.  The C++ side
-- bridges these via `node_view_from_zone_count` (relativistic_zone.h).
-- The two theorems below are the formal statements of that bridge.

/-- The uniform initial partition (one ZoneRange per zone, cell width =
    hilbertSpanWidth (zonePrefixDepth zoneCount)) satisfies DisjointRanges.
    Proved by contradiction: if z1 ≠ z2 then one interval strictly precedes the
    other (via Nat.mul_le_mul_right + omega); both containing h is impossible. -/
theorem uniform_partition_disjoint (_n zoneCount : Nat)
    (_hpos : 0 < zoneCount) (_depth : Nat) (cell_w : Nat) (hcw : 0 < cell_w)
    (ranges : List ZoneRange)
    (hunif : ranges = (List.range zoneCount).map (fun z =>
        { zoneId := z, lo := z * cell_w, hi := z * cell_w + cell_w - 1 })) :
    DisjointRanges ranges := by
  subst hunif
  unfold DisjointRanges
  intro r1 r2 h hm1 hm2 hc1 hc2
  simp only [List.mem_map, List.mem_range] at hm1 hm2
  obtain ⟨z1, _hz1, rfl⟩ := hm1
  obtain ⟨z2, _hz2, rfl⟩ := hm2
  simp only [ZoneRange.contains, Bool.and_eq_true, decide_eq_true_eq] at hc1 hc2
  obtain ⟨hlo1, hhi1⟩ := hc1
  obtain ⟨hlo2, hhi2⟩ := hc2
  by_cases heq : z1 = z2
  · exact heq
  · exfalso
    by_cases hlt : z1 < z2
    · have hstep : z1 * cell_w + cell_w ≤ z2 * cell_w :=
        calc z1 * cell_w + cell_w = (z1 + 1) * cell_w := by rw [Nat.succ_mul]
          _ ≤ z2 * cell_w := Nat.mul_le_mul_right cell_w hlt
      omega
    · have hlt2 : z2 < z1 := Nat.lt_of_le_of_ne (Nat.le_of_not_lt hlt) (Ne.symm heq)
      have hstep : z2 * cell_w + cell_w ≤ z1 * cell_w :=
        calc z2 * cell_w + cell_w = (z2 + 1) * cell_w := by rw [Nat.succ_mul]
          _ ≤ z1 * cell_w := Nat.mul_le_mul_right cell_w hlt2
      omega

/-- geometricAuthority on a view returns a zone id that is a valid index
    into view.ranges, given the invariant that every stored zone id is in-bounds.
    This is the formal counterpart to the C++ `zone_for_hilbert` fallback-clamp.
    The invariant `hvalid` holds for any NodeView produced by
    `node_view_from_zone_count` because zoneId = position in 0..zoneCount-1
    and length = zoneCount. -/
theorem geometric_authority_zoneId_lt_length {n : Nat}
    (view : NodeView n) (h : Nat) (_hlen : 0 < view.ranges.length)
    (hvalid : ∀ r ∈ view.ranges, r.zoneId < view.ranges.length)
    {r : ZoneRange} (hauth : geometricAuthority view h = some r) :
    r.zoneId < view.ranges.length := by
  unfold geometricAuthority at hauth
  exact hvalid r (List.mem_of_find?_eq_some hauth)

end PredictiveBVH.Relativistic
