// SPDX-License-Identifier: MIT
// Copyright (c) 2026-present K. S. Ernest (iFire) Lee
//
// predictive_bvh_adapter.rs — Rust adapter for the PredictiveBVH tree engine.
// Feature parity with core/math/predictive_bvh_adapter.h.
//
// Assumes the generated `predictive_bvh.rs` is compiled into the same crate.
// Use `use super::*;` or `use crate::predictive_bvh::*;` at the call site.

// ─── Re-exports from generated code ─────────────────────────────────────────
// Callers should bring in scope: Aabb, hilbert_of_aabb, per_entity_delta_poly,
// clz30, hilbert3d, hilbert3d_inverse_bridge, hilbert_cell_of_bridge.

pub const PBVH_NULL_NODE: u32 = 0xFFFF_FFFFu32;
pub const PBVH_BUCKET_K_TARGET: u32 = 32;

// ─── Data structures ─────────────────────────────────────────────────────────

/// Mirror of pbvh_node<R128> / pbvh_node_t.
/// `bounds` uses i64 micrometres, matching the generated Rust `Aabb`.
#[derive(Clone, Debug, Default)]
pub struct PbvhNode {
    pub bounds: crate::Aabb,
    pub eclass: u32,
    pub next_free: u32, // PBVH_NULL_NODE when live
    pub is_leaf: bool,
    pub hilbert: u32, // 30-bit Hilbert code; sort key
}

/// Mirror of pbvh_internal<R128> / pbvh_internal_t.
#[derive(Clone, Debug, Default)]
pub struct PbvhInternal {
    pub bounds: crate::Aabb,
    pub offset: u32,
    pub span: u32,
    pub skip: u32,  // next DFS index after this subtree ends
    pub left: u32,  // PBVH_NULL_NODE when this is a leaf-range node
    pub right: u32, // PBVH_NULL_NODE when this is a leaf-range node
}

/// Mirror of pbvh_dirty_leaf_t.
#[derive(Clone, Copy, Debug, Default)]
pub struct PbvhDirtyLeaf {
    pub leaf_id: u32,
    pub old_hilbert: u32,
}

/// Mirror of pbvh_plane_t.
/// Oriented half-space {p : normal · p + d >= 0}. Kept side is positive.
/// Coordinates in micrometres (i64), same scale as Aabb.
#[derive(Clone, Copy, Debug, Default)]
pub struct PbvhPlane {
    pub nx: i64,
    pub ny: i64,
    pub nz: i64,
    pub d: i64,
}

// ─── Bucket utilities ────────────────────────────────────────────────────────

/// ceil(log2(n)) clamped to [0, 30]. n == 0 or 1 maps to 0.
pub fn pbvh_ceil_log2(n: u32) -> u32 {
    if n <= 1 {
        return 0;
    }
    let mut v = n - 1;
    let mut r = 0u32;
    while v > 0 {
        v >>= 1;
        r += 1;
    }
    r.min(30)
}

/// Ideal bucket_bits for N leaves: ceil(log2(N / K_TARGET)), clamped [0, 30].
pub fn pbvh_bucket_bits_for(n: u32) -> u32 {
    if n <= PBVH_BUCKET_K_TARGET {
        return 0;
    }
    pbvh_ceil_log2((n + PBVH_BUCKET_K_TARGET - 1) / PBVH_BUCKET_K_TARGET)
}

/// Required uint32 element count for bucket_dir given N leaves.
pub fn pbvh_bucket_dir_size(n: u32) -> u32 {
    2u32 * (1u32 << pbvh_bucket_bits_for(n))
}

// ─── PbvhTree ────────────────────────────────────────────────────────────────

/// Owned-storage BVH tree. Equivalent to pbvh_tree_t + its LocalVector sidecars
/// from class PredictiveBVH.
pub struct PbvhTree {
    pub nodes: Vec<PbvhNode>,
    pub count: u32,
    pub root: u32,
    pub free_head: u32,
    pub sorted: Vec<u32>,
    pub sorted_count: u32,
    pub last_visits: u32,
    pub internals: Vec<PbvhInternal>,
    pub internal_count: u32,
    pub internal_root: u32,
    pub bucket_dir: Vec<u32>,
    pub bucket_bits: u32,
    // Incremental-refit sidecars (Phase 2c)
    pub parent_of_internal: Vec<u32>,
    pub leaf_to_internal: Vec<u32>,
    pub touched_bits: Vec<u64>,
    pub touched_meta_bits: Vec<u64>,
}

impl PbvhTree {
    pub fn new(capacity: usize) -> Self {
        let cap = capacity.max(4);
        let internal_cap = cap * 2;
        let touched_words = (internal_cap + 63) / 64;
        let touched_meta = (touched_words + 63) / 64;
        let bd_size = pbvh_bucket_dir_size(cap as u32) as usize + 2;
        Self {
            nodes: vec![PbvhNode::default(); cap],
            count: 0,
            root: PBVH_NULL_NODE,
            free_head: PBVH_NULL_NODE,
            sorted: vec![0u32; cap],
            sorted_count: 0,
            last_visits: 0,
            internals: vec![PbvhInternal::default(); internal_cap],
            internal_count: 0,
            internal_root: PBVH_NULL_NODE,
            bucket_dir: vec![0u32; bd_size],
            bucket_bits: 0,
            parent_of_internal: vec![PBVH_NULL_NODE; internal_cap],
            leaf_to_internal: vec![PBVH_NULL_NODE; cap],
            touched_bits: vec![0u64; touched_words.max(1)],
            touched_meta_bits: vec![0u64; touched_meta.max(1)],
        }
    }

    pub fn ensure_capacity(&mut self, need: usize) {
        if need <= self.nodes.len() {
            return;
        }
        let mut new_cap = 16usize;
        while new_cap < need {
            new_cap *= 2;
        }
        let internal_cap = new_cap * 2;
        let touched_words = (internal_cap + 63) / 64;
        let touched_meta = (touched_words + 63) / 64;
        self.nodes.resize(new_cap, PbvhNode::default());
        self.sorted.resize(new_cap, 0u32);
        self.leaf_to_internal.resize(new_cap, PBVH_NULL_NODE);
        self.internals.resize(internal_cap, PbvhInternal::default());
        self.parent_of_internal.resize(internal_cap, PBVH_NULL_NODE);
        self.touched_bits.resize(touched_words.max(1), 0u64);
        self.touched_meta_bits.resize(touched_meta.max(1), 0u64);
        let bd_cap = pbvh_bucket_dir_size(new_cap as u32) as usize + 2;
        if self.bucket_dir.len() < bd_cap {
            self.bucket_dir.resize(bd_cap, 0u32);
        }
    }

    // ── Insert / remove / update ────────────────────────────────────────────

    pub fn insert_h(&mut self, eclass: u32, bounds: crate::Aabb, hilbert: u32) -> u32 {
        let id = if self.free_head != PBVH_NULL_NODE {
            let id = self.free_head;
            self.free_head = self.nodes[id as usize].next_free;
            id
        } else {
            let id = self.count;
            self.count += 1;
            id
        };
        let n = &mut self.nodes[id as usize];
        n.bounds = bounds;
        n.eclass = eclass;
        n.next_free = PBVH_NULL_NODE;
        n.is_leaf = true;
        n.hilbert = hilbert;
        id
    }

    #[inline]
    pub fn insert(&mut self, eclass: u32, bounds: crate::Aabb) -> u32 {
        self.insert_h(eclass, bounds, 0)
    }

    pub fn remove(&mut self, id: u32) {
        let n = &mut self.nodes[id as usize];
        n.is_leaf = false;
        n.next_free = self.free_head;
        self.free_head = id;
    }

    #[inline]
    pub fn update(&mut self, id: u32, bounds: crate::Aabb) {
        self.nodes[id as usize].bounds = bounds;
    }

    pub fn update_h(&mut self, id: u32, bounds: crate::Aabb, hilbert: u32) {
        let n = &mut self.nodes[id as usize];
        n.bounds = bounds;
        n.hilbert = hilbert;
    }

    // ── Build helpers ────────────────────────────────────────────────────────

    fn build_internal_with_parent(&mut self, lo: u32, hi: u32, parent: u32) -> u32 {
        if lo >= hi {
            return PBVH_NULL_NODE;
        }
        if self.internal_count as usize >= self.internals.len() {
            return PBVH_NULL_NODE;
        }
        let id = self.internal_count;
        self.internal_count += 1;
        if parent != PBVH_NULL_NODE {
            self.parent_of_internal[id as usize] = parent;
        }
        // Compute union bounds over sorted[lo..hi]
        let bounds = {
            let mut b = self.nodes[self.sorted[lo as usize] as usize].bounds;
            for i in (lo + 1)..hi {
                let nb = self.nodes[self.sorted[i as usize] as usize].bounds;
                b = b.union(&nb);
            }
            b
        };
        {
            let n = &mut self.internals[id as usize];
            n.offset = lo;
            n.span = hi - lo;
            n.bounds = bounds;
        }
        if hi - lo <= 1 {
            let n = &mut self.internals[id as usize];
            n.left = PBVH_NULL_NODE;
            n.right = PBVH_NULL_NODE;
            n.skip = self.internal_count;
            return id;
        }
        let h_lo = self.nodes[self.sorted[lo as usize] as usize].hilbert;
        let h_hi = self.nodes[self.sorted[(hi - 1) as usize] as usize].hilbert;
        let diff = h_lo ^ h_hi;
        let mut split = lo + (hi - lo) / 2;
        if diff != 0 {
            let bit = 31 - diff.leading_zeros();
            let mask = 1u32 << bit;
            for i in lo..hi {
                if (self.nodes[self.sorted[i as usize] as usize].hilbert & mask) != 0 {
                    if i > lo && i < hi {
                        split = i;
                    }
                    break;
                }
            }
        }
        let l = self.build_internal_with_parent(lo, split, id);
        let r = self.build_internal_with_parent(split, hi, id);
        let n = &mut self.internals[id as usize];
        n.left = l;
        n.right = r;
        n.skip = self.internal_count;
        id
    }

    fn build_bucket_dir(&mut self) {
        if self.bucket_bits == 0 || self.bucket_bits > 30 {
            return;
        }
        let b_count = 1u32 << self.bucket_bits;
        for i in 0..(2 * b_count as usize) {
            self.bucket_dir[i] = 0;
        }
        let shift = 30u32 - self.bucket_bits;
        let mut j = 0u32;
        for b in 0..b_count {
            self.bucket_dir[2 * b as usize] = j;
            while j < self.sorted_count
                && (self.nodes[self.sorted[j as usize] as usize].hilbert >> shift) == b
            {
                j += 1;
            }
            self.bucket_dir[2 * b as usize + 1] = j;
        }
    }

    // ── Refit ────────────────────────────────────────────────────────────────

    /// O(internal_count) bottom-up refit. Re-unions all bounds sequentially.
    pub fn refit(&mut self) {
        if self.internal_count == 0 {
            return;
        }
        let mut idx = self.internal_count;
        while idx > 0 {
            idx -= 1;
            let (left, right, offset, span) = {
                let n = &self.internals[idx as usize];
                (n.left, n.right, n.offset, n.span)
            };
            let new_bounds = if left == PBVH_NULL_NODE && right == PBVH_NULL_NODE {
                if span == 0 {
                    continue;
                }
                let mut acc = self.nodes[self.sorted[offset as usize] as usize].bounds;
                for j in (offset + 1)..(offset + span) {
                    let nb = self.nodes[self.sorted[j as usize] as usize].bounds;
                    acc = acc.union(&nb);
                }
                acc
            } else if left != PBVH_NULL_NODE && right != PBVH_NULL_NODE {
                let lb = self.internals[left as usize].bounds;
                let rb = self.internals[right as usize].bounds;
                lb.union(&rb)
            } else {
                let only = if left != PBVH_NULL_NODE { left } else { right };
                self.internals[only as usize].bounds
            };
            self.internals[idx as usize].bounds = new_bounds;
        }
    }

    /// Incremental refit: touches only ancestor chains of dirty leaves.
    /// Falls back to refit() when no sidecar data is available.
    pub fn refit_incremental(&mut self, dirty: &[PbvhDirtyLeaf]) {
        if self.internal_count == 0 {
            return;
        }
        if dirty.is_empty() {
            self.refit();
            return;
        }
        // Mark phase
        let mut min_meta = u32::MAX;
        let mut max_meta = 0u32;
        for d in dirty {
            if d.leaf_id >= self.count {
                continue;
            }
            let new_leaf = self.nodes[d.leaf_id as usize].bounds;
            let mut i = self.leaf_to_internal[d.leaf_id as usize];
            while i != PBVH_NULL_NODE && i < self.internal_count {
                if self.internals[i as usize].bounds.contains(&new_leaf) {
                    break;
                }
                let w = (i >> 6) as usize;
                let mask = 1u64 << (i & 63);
                self.touched_bits[w] |= mask;
                let mw = (w >> 6) as u32;
                self.touched_meta_bits[mw as usize] |= 1u64 << (w & 63);
                if mw < min_meta {
                    min_meta = mw;
                }
                if mw > max_meta {
                    max_meta = mw;
                }
                i = self.parent_of_internal[i as usize];
            }
        }
        if min_meta > max_meta {
            return;
        }
        // Refit phase: descending order via meta-bitmap → touched_bits → internal ids
        let mut mw = max_meta + 1;
        while mw > min_meta {
            mw -= 1;
            let mut meta = self.touched_meta_bits[mw as usize];
            while meta != 0 {
                let mb = 63 - meta.leading_zeros();
                meta &= !(1u64 << mb);
                let w = (mw << 6) | mb;
                let mut bits = self.touched_bits[w as usize];
                while bits != 0 {
                    let b = 63 - bits.leading_zeros();
                    bits &= !(1u64 << b);
                    let idx = (w << 6) | b;
                    let (left, right, offset, span) = {
                        let n = &self.internals[idx as usize];
                        (n.left, n.right, n.offset, n.span)
                    };
                    let new_bounds =
                        if left == PBVH_NULL_NODE && right == PBVH_NULL_NODE {
                            if span == 0 {
                                continue;
                            }
                            let mut acc =
                                self.nodes[self.sorted[offset as usize] as usize].bounds;
                            for j in (offset + 1)..(offset + span) {
                                let nb =
                                    self.nodes[self.sorted[j as usize] as usize].bounds;
                                acc = acc.union(&nb);
                            }
                            acc
                        } else if left != PBVH_NULL_NODE && right != PBVH_NULL_NODE {
                            let lb = self.internals[left as usize].bounds;
                            let rb = self.internals[right as usize].bounds;
                            lb.union(&rb)
                        } else {
                            let only = if left != PBVH_NULL_NODE { left } else { right };
                            self.internals[only as usize].bounds
                        };
                    self.internals[idx as usize].bounds = new_bounds;
                }
                self.touched_bits[w as usize] = 0;
            }
            self.touched_meta_bits[mw as usize] = 0;
        }
    }

    // ── Build ────────────────────────────────────────────────────────────────

    /// 4-pass LSD radix sort + nested-set internal tree build. O(N).
    pub fn build(&mut self) {
        // Collect live leaves into sorted[]
        let mut k = 0u32;
        for i in 0..self.count as usize {
            if self.nodes[i].is_leaf {
                self.sorted[k as usize] = i as u32;
                k += 1;
            }
        }
        self.sorted_count = k;

        // 4-pass LSD radix sort on 30-bit Hilbert codes
        if k > 1 {
            let mut src: Vec<u32> = self.sorted[..k as usize].to_vec();
            let mut dst: Vec<u32> = vec![0u32; k as usize];
            for pass in 0..4u32 {
                let shift = pass * 8;
                let mut count_bin = [0u32; 256];
                for i in 0..k as usize {
                    let b = ((self.nodes[src[i] as usize].hilbert >> shift) & 0xFF) as usize;
                    count_bin[b] += 1;
                }
                let mut sum = 0u32;
                for b in 0..256usize {
                    let c = count_bin[b];
                    count_bin[b] = sum;
                    sum += c;
                }
                for i in 0..k as usize {
                    let b = ((self.nodes[src[i] as usize].hilbert >> shift) & 0xFF) as usize;
                    dst[count_bin[b] as usize] = src[i];
                    count_bin[b] += 1;
                }
                std::mem::swap(&mut src, &mut dst);
            }
            // After 4 (even) passes the result is in src
            self.sorted[..k as usize].copy_from_slice(&src);
        }

        // Build nested-set internal tree
        self.internal_count = 0;
        self.internal_root = PBVH_NULL_NODE;
        if !self.internals.is_empty() && k > 0 {
            self.internal_root = self.build_internal_with_parent(0, k, PBVH_NULL_NODE);
        }

        // Auto-tune bucket_bits and rebuild bucket directory
        self.bucket_bits = pbvh_bucket_bits_for(k);
        self.build_bucket_dir();

        // Populate leaf_to_internal[] from leaf-range internals
        for i in 0..self.internal_count as usize {
            let (left, right, offset, span) = {
                let n = &self.internals[i];
                (n.left, n.right, n.offset, n.span)
            };
            if left != PBVH_NULL_NODE || right != PBVH_NULL_NODE {
                continue;
            }
            for j in offset..(offset + span) {
                let node_id = self.sorted[j as usize];
                self.leaf_to_internal[node_id as usize] = i as u32;
            }
        }
    }

    // ── Queries ──────────────────────────────────────────────────────────────

    /// O(N) brute-force leaf scan. Correctness oracle; use aabb_query_n in production.
    pub fn aabb_query<F: FnMut(u32) -> bool>(&mut self, query: &crate::Aabb, mut cb: F) {
        let mut visits = 0u32;
        for i in 0..self.count as usize {
            let (is_leaf, eclass, bounds) = {
                let n = &self.nodes[i];
                (n.is_leaf, n.eclass, n.bounds)
            };
            if !is_leaf {
                continue;
            }
            visits += 1;
            if bounds.overlaps(query) && cb(eclass) {
                self.last_visits = visits;
                return;
            }
        }
        self.last_visits = visits;
    }

    /// Iterative nested-set skip-pointer descent. O(log N + k) average.
    pub fn aabb_query_n<F: FnMut(u32) -> bool>(&mut self, query: &crate::Aabb, mut cb: F) {
        let mut visits = 0u32;
        if self.internal_root == PBVH_NULL_NODE {
            self.last_visits = 0;
            return;
        }
        let mut i = self.internal_root as usize;
        let end = self.internal_count as usize;
        while i < end {
            let (left, right, skip, offset, span, overlaps) = {
                let n = &self.internals[i];
                (n.left, n.right, n.skip as usize, n.offset, n.span, n.bounds.overlaps(query))
            };
            if !overlaps {
                i = skip;
                continue;
            }
            if left == PBVH_NULL_NODE && right == PBVH_NULL_NODE {
                for j in offset..(offset + span) {
                    let (is_leaf, eclass, bounds) = {
                        let leaf = &self.nodes[self.sorted[j as usize] as usize];
                        (leaf.is_leaf, leaf.eclass, leaf.bounds)
                    };
                    if !is_leaf {
                        continue;
                    }
                    visits += 1;
                    if bounds.overlaps(query) && cb(eclass) {
                        self.last_visits = visits;
                        return;
                    }
                }
                i = skip;
                continue;
            }
            i += 1; // descend: next DFS index is the left child
        }
        self.last_visits = visits;
    }

    /// Bucket-directory query. O(1 + k) for queries tagged with a Hilbert code.
    /// Falls back to aabb_query_n if bucket_dir wasn't built.
    pub fn aabb_query_b<F: FnMut(u32) -> bool>(
        &mut self,
        query: &crate::Aabb,
        query_hilbert: u32,
        mut cb: F,
    ) {
        if self.bucket_bits == 0 || self.bucket_dir.is_empty() {
            self.aabb_query_n(query, cb);
            return;
        }
        let shift = 30 - self.bucket_bits;
        let b = query_hilbert >> shift;
        let big_b = 1u32 << self.bucket_bits;
        if b >= big_b {
            self.aabb_query_n(query, cb);
            return;
        }
        let lo = self.bucket_dir[2 * b as usize];
        let hi = self.bucket_dir[2 * b as usize + 1];
        let mut visits = 0u32;
        for j in lo..hi {
            let (is_leaf, eclass, bounds) = {
                let leaf = &self.nodes[self.sorted[j as usize] as usize];
                (leaf.is_leaf, leaf.eclass, leaf.bounds)
            };
            if !is_leaf {
                continue;
            }
            visits += 1;
            if bounds.overlaps(query) && cb(eclass) {
                self.last_visits = visits;
                return;
            }
        }
        self.last_visits = visits;
    }

    /// Eclass self-query: find the leaf for `self_eclass`, then run aabb_query_b
    /// skipping itself. Caller works in eclass IDs end-to-end.
    pub fn query_eclass<F: FnMut(u32) -> bool>(&mut self, self_eclass: u32, mut cb: F) {
        // Extract bounds + hilbert first to avoid nested borrow
        let mut found: Option<(crate::Aabb, u32)> = None;
        for i in 0..self.count as usize {
            let n = &self.nodes[i];
            if n.is_leaf && n.eclass == self_eclass {
                found = Some((n.bounds, n.hilbert));
                break;
            }
        }
        if let Some((bounds, hilbert)) = found {
            self.aabb_query_b(&bounds, hilbert, |other| {
                if other == self_eclass {
                    false
                } else {
                    cb(other)
                }
            });
        }
    }

    /// Enumerate every overlapping (a, b) pair with a.eclass < b.eclass exactly once.
    /// Uses aabb_query_n so ghost AABBs spanning multiple Hilbert cells stay correct.
    pub fn enumerate_pairs<F: FnMut(u32, u32) -> bool>(&mut self, mut cb: F) -> i32 {
        // Collect live leaf data first to avoid nested &mut self borrow
        let leaves: Vec<(crate::Aabb, u32)> = (0..self.count as usize)
            .filter_map(|i| {
                let n = &self.nodes[i];
                if n.is_leaf {
                    Some((n.bounds, n.eclass))
                } else {
                    None
                }
            })
            .collect();
        let mut pairs = 0i32;
        for (bounds, self_ec) in &leaves {
            let bounds = *bounds;
            let self_ec = *self_ec;
            let mut count = 0i32;
            self.aabb_query_n(&bounds, |other| {
                if other <= self_ec {
                    return false;
                }
                count += 1;
                cb(self_ec, other)
            });
            pairs += count;
        }
        pairs
    }

    // ── Ray / convex queries ─────────────────────────────────────────────────

    fn segment_aabb(ox: i64, oy: i64, oz: i64, tx: i64, ty: i64, tz: i64) -> crate::Aabb {
        crate::Aabb {
            min_x: ox.min(tx),
            max_x: ox.max(tx),
            min_y: oy.min(ty),
            max_y: oy.max(ty),
            min_z: oz.min(tz),
            max_z: oz.max(tz),
        }
    }

    /// AABB-broadphase ray segment query.
    pub fn ray_query<F: FnMut(u32) -> bool>(
        &mut self,
        ox: i64,
        oy: i64,
        oz: i64,
        tx: i64,
        ty: i64,
        tz: i64,
        mut cb: F,
    ) {
        if self.internal_root == PBVH_NULL_NODE {
            return;
        }
        let seg = Self::segment_aabb(ox, oy, oz, tx, ty, tz);
        let mut i = self.internal_root as usize;
        let end = self.internal_count as usize;
        while i < end {
            let (left, right, skip, offset, span, overlaps) = {
                let n = &self.internals[i];
                (n.left, n.right, n.skip as usize, n.offset, n.span, n.bounds.overlaps(&seg))
            };
            if !overlaps {
                i = skip;
                continue;
            }
            if left == PBVH_NULL_NODE && right == PBVH_NULL_NODE {
                for j in offset..(offset + span) {
                    let (is_leaf, eclass, bounds) = {
                        let leaf = &self.nodes[self.sorted[j as usize] as usize];
                        (leaf.is_leaf, leaf.eclass, leaf.bounds)
                    };
                    if !is_leaf {
                        continue;
                    }
                    if bounds.overlaps(&seg) && cb(eclass) {
                        return;
                    }
                }
                i = skip;
                continue;
            }
            i += 1;
        }
    }

    /// Half-space test: does any corner of `b` satisfy normal·c + d >= 0?
    /// Uses i64 saturating arithmetic (same scale as plane normals & Aabb).
    fn half_space_keeps(plane: &PbvhPlane, b: &crate::Aabb) -> bool {
        let xs = [b.min_x, b.max_x];
        let ys = [b.min_y, b.max_y];
        let zs = [b.min_z, b.max_z];
        for &x in &xs {
            for &y in &ys {
                for &z in &zs {
                    let val = plane
                        .nx
                        .saturating_mul(x)
                        .saturating_add(plane.ny.saturating_mul(y))
                        .saturating_add(plane.nz.saturating_mul(z))
                        .saturating_add(plane.d);
                    if val >= 0 {
                        return true;
                    }
                }
            }
        }
        false
    }

    fn convex_keeps_box(planes: &[PbvhPlane], b: &crate::Aabb) -> bool {
        planes.iter().all(|p| Self::half_space_keeps(p, b))
    }

    /// Convex-hull broadphase: every live leaf with at least one corner on the
    /// kept side of every plane is passed to `cb`.
    pub fn convex_query<F: FnMut(u32) -> bool>(
        &mut self,
        planes: &[PbvhPlane],
        mut cb: F,
    ) {
        if self.internal_root == PBVH_NULL_NODE || planes.is_empty() {
            return;
        }
        let mut i = self.internal_root as usize;
        let end = self.internal_count as usize;
        while i < end {
            let (left, right, skip, offset, span, keeps) = {
                let n = &self.internals[i];
                let keeps = Self::convex_keeps_box(planes, &n.bounds);
                (n.left, n.right, n.skip as usize, n.offset, n.span, keeps)
            };
            if !keeps {
                i = skip;
                continue;
            }
            if left == PBVH_NULL_NODE && right == PBVH_NULL_NODE {
                for j in offset..(offset + span) {
                    let (is_leaf, eclass, bounds) = {
                        let leaf = &self.nodes[self.sorted[j as usize] as usize];
                        (leaf.is_leaf, leaf.eclass, leaf.bounds)
                    };
                    if !is_leaf {
                        continue;
                    }
                    if Self::convex_keeps_box(planes, &bounds) && cb(eclass) {
                        return;
                    }
                }
                i = skip;
                continue;
            }
            i += 1;
        }
    }

    // ── Maintenance ──────────────────────────────────────────────────────────

    /// Reset to empty. Preserves allocated buffer capacities.
    pub fn clear(&mut self) {
        self.count = 0;
        self.sorted_count = 0;
        self.internal_count = 0;
        self.root = PBVH_NULL_NODE;
        self.free_head = PBVH_NULL_NODE;
        self.internal_root = PBVH_NULL_NODE;
        self.last_visits = 0;
    }

    pub fn is_empty(&self) -> bool {
        self.nodes[..self.count as usize].iter().all(|n| !n.is_leaf)
    }

    /// Per-frame rebalance. Routes to incremental refit or full build based on
    /// whether the tree structure is still valid for the dirty set.
    pub fn tick(&mut self, dirty: &[PbvhDirtyLeaf]) {
        if dirty.is_empty() || self.bucket_bits == 0 || self.bucket_bits > 30 {
            self.build();
            return;
        }
        if self.count > self.sorted_count {
            self.build();
            return;
        }
        if self.internal_count == 0 || self.internal_root == PBVH_NULL_NODE {
            self.build();
            return;
        }
        self.refit_incremental(dirty);
    }

    /// DynamicBVH-parity wrapper: ignore passes, route to tick with empty dirty list.
    pub fn optimize_incremental(&mut self, _passes: i32) {
        self.tick(&[]);
    }
}

// ─── PredictiveBvh ──────────────────────────────────────────────────────────
//
// High-level wrapper matching the C++ `class PredictiveBVH` interface.
// Owns the tree storage; coordinates are i64 micrometres.

pub struct PredictiveBvh {
    pub tree: PbvhTree,
    /// Scene bounding box used for Hilbert code normalisation.
    /// Default covers ±4.6 × 10¹⁸ μm (effectively unbounded for real scenes).
    scene_aabb: crate::Aabb,
    index_slot: u32,
    dirty: bool,
}

impl PredictiveBvh {
    const DEFAULT_CAPACITY: usize = 256;

    pub fn new() -> Self {
        let half = i64::MAX / 2;
        let scene_aabb = crate::Aabb {
            min_x: -half,
            max_x: half,
            min_y: -half,
            max_y: half,
            min_z: -half,
            max_z: half,
        };
        Self {
            tree: PbvhTree::new(Self::DEFAULT_CAPACITY),
            scene_aabb,
            index_slot: 0,
            dirty: false,
        }
    }

    /// Override the scene AABB used for Hilbert code normalisation.
    /// Must be set before the first insert for codes to be meaningful.
    pub fn set_scene_aabb(&mut self, aabb: crate::Aabb) {
        self.scene_aabb = aabb;
    }

    pub fn is_empty(&self) -> bool {
        self.tree.is_empty()
    }

    pub fn clear(&mut self) {
        self.tree.clear();
        self.dirty = false;
    }

    /// Insert a leaf. Returns a stable node ID (valid until `remove`).
    /// `userdata` is stored as the eclass and can be any u32 tag.
    pub fn insert(&mut self, bounds: crate::Aabb, userdata: u32) -> u32 {
        let need = (self.tree.count + 1) as usize;
        self.tree.ensure_capacity(need);
        let hilbert = crate::hilbert_of_aabb(&bounds, &self.scene_aabb);
        let id = self.tree.insert_h(userdata, bounds, hilbert);
        // Rewrite eclass to the stable node id so queries return node ids.
        self.tree.nodes[id as usize].eclass = id;
        self.dirty = true;
        id
    }

    /// Update the AABB of an existing leaf; recomputes its Hilbert code.
    pub fn update(&mut self, id: u32, bounds: crate::Aabb) -> bool {
        if id >= self.tree.count {
            return false;
        }
        let hilbert = crate::hilbert_of_aabb(&bounds, &self.scene_aabb);
        self.tree.update_h(id, bounds, hilbert);
        self.dirty = true;
        true
    }

    pub fn remove(&mut self, id: u32) {
        if id >= self.tree.count {
            return;
        }
        self.tree.remove(id);
        self.dirty = true;
    }

    fn maybe_build(&mut self) {
        if self.dirty {
            self.tree.build();
            self.dirty = false;
        }
    }

    /// AABB query via nested-set skip-pointer descent.
    /// `cb(node_id)` is called for every overlapping live leaf; return `true` to stop early.
    pub fn aabb_query<F: FnMut(u32) -> bool>(&mut self, query: &crate::Aabb, cb: F) {
        self.maybe_build();
        if self.tree.internal_root == PBVH_NULL_NODE {
            return;
        }
        self.tree.aabb_query_n(query, cb);
    }

    /// Ray segment AABB-broadphase query.
    pub fn ray_query<F: FnMut(u32) -> bool>(
        &mut self,
        ox: i64,
        oy: i64,
        oz: i64,
        tx: i64,
        ty: i64,
        tz: i64,
        cb: F,
    ) {
        self.maybe_build();
        if self.tree.internal_root == PBVH_NULL_NODE {
            return;
        }
        self.tree.ray_query(ox, oy, oz, tx, ty, tz, cb);
    }

    /// Convex-hull broadphase query.
    pub fn convex_query<F: FnMut(u32) -> bool>(
        &mut self,
        planes: &[PbvhPlane],
        cb: F,
    ) {
        self.maybe_build();
        if self.tree.internal_root == PBVH_NULL_NODE {
            return;
        }
        self.tree.convex_query(planes, cb);
    }

    /// Per-frame rebalance. Pass dirty leaves (moved since last tick/build).
    pub fn tick(&mut self, dirty_leaves: &[PbvhDirtyLeaf]) {
        self.tree.tick(dirty_leaves);
        self.dirty = false;
    }

    pub fn optimize_incremental(&mut self, passes: i32) {
        if self.dirty {
            self.tree.build();
            self.dirty = false;
        }
        let _ = passes;
    }

    pub fn set_index(&mut self, idx: u32) {
        self.index_slot = idx;
    }
    pub fn get_index(&self) -> u32 {
        self.index_slot
    }
}

impl Default for PredictiveBvh {
    fn default() -> Self {
        Self::new()
    }
}
