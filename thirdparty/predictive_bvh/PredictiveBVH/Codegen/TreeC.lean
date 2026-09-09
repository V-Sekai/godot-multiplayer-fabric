-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee
--
-- TreeC.lean — emits the pbvh_tree_* block into predictive_bvh.h.
--
-- The hand-written predictive_bvh_tree.h scaffold lives at
-- thirdparty/predictive_bvh/predictive_bvh_tree.h and is deleted once this
-- emitted block passes the existing 23-case doctest suite. Algorithm and
-- field layout mirror Spatial/Tree.lean verbatim so the proofs there hold
-- for the code emitted here.

namespace PredictiveBVH.Codegen.TreeC

def treeBanner : String :=
  "/* ══════════════════════════════════════════════════════════════════════════\n" ++
  "   PBVH TREE (Hilbert-radix nested-set BVH; emitted from Spatial/Tree.lean)\n" ++
  "   ══════════════════════════════════════════════════════════════════════════ */\n\n"

def treeBody : String := "typedef uint32_t pbvh_eclass_id_t;
typedef uint32_t pbvh_node_id_t;
typedef uint32_t pbvh_internal_id_t;

#define PBVH_NULL_NODE ((pbvh_node_id_t)0xFFFFFFFFu)

template <typename T>
struct pbvh_node {
\tAabbT<T> bounds; /* 96 B (T × 6) */
\tpbvh_eclass_id_t eclass;
\tpbvh_node_id_t next_free; /* PBVH_NULL_NODE when live */
\tuint32_t is_leaf;
\tuint32_t hilbert; /* 30-bit Hilbert code; sort key */
};
using pbvh_node_t = pbvh_node<int64_t>;

/* Hilbert-radix internal node over sorted[]. Stored in pre-order DFS, so the
 * array itself is a nested set: the subtree rooted at internals[i] occupies
 * contiguous indices [i, skip). On each node, (offset, span) is the
 * corresponding range inside t->sorted[] — the leaf-side nested set. */
template <typename T>
struct pbvh_internal {
\tAabbT<T> bounds; /* union of every leaf AABB in [offset, offset+span) */
\tuint32_t offset; /* start index into t->sorted[] */
\tuint32_t span; /* leaf count in this subtree */
\tpbvh_internal_id_t skip; /* next DFS index after this subtree ends */
\tpbvh_internal_id_t left; /* PBVH_NULL_NODE when this is a leaf-range node */
\tpbvh_internal_id_t right; /* PBVH_NULL_NODE when this is a leaf-range node */
};
using pbvh_internal_t = pbvh_internal<int64_t>;

typedef struct pbvh_dirty_leaf {
\tpbvh_node_id_t leaf_id;
\tuint32_t old_hilbert;
} pbvh_dirty_leaf_t;

template <typename T>
struct pbvh_tree {
\tpbvh_node<T> *nodes;
\tuint32_t capacity;
\tuint32_t count;
\tpbvh_node_id_t root;
\tpbvh_node_id_t free_head;
\t/* Sorted-by-hilbert permutation of live leaf ids. Caller-owned, size capacity. */
\tpbvh_node_id_t *sorted;
\tuint32_t sorted_count;
\tuint32_t last_visits; /* debug: # of leaves AABB-tested in the last query */
\t/* Hilbert-radix internal tree over sorted[]. Mandatory for _n and _b
\t * queries. Caller-owned; size at least 2*capacity covers any split shape. */
\tpbvh_internal<T> *internals;
\tuint32_t internal_capacity;
\tuint32_t internal_count;
\tpbvh_internal_id_t internal_root;
\t/* Optional bucket directory: bucket_dir[p] is the half-open range
\t * [lo, hi) of sorted[] indices whose Hilbert code has prefix p at
\t * bucket_bits. Size must be 1u << bucket_bits; two uint32 per entry
\t * laid out flat as [lo0, hi0, lo1, hi1, …]. Set bucket_bits=0 to skip. */
\tuint32_t *bucket_dir;
\tuint32_t bucket_bits;
\t/* Optional incremental-refit sidecar (eclass-keyed, no parent pointers
\t * inside pbvh_internal_t). When all three are non-NULL, pbvh_tree_tick
\t * restricts its refit to the ancestor set of dirty leaves — O(K log N)
\t * touches instead of O(internal_count). leaf_to_internal[id] = the
\t * enclosing leaf-range internal id for leaf node id; parent_of_internal[i]
\t * = the immediately enclosing internal id (PBVH_NULL_NODE at root);
\t * touched_bits = internal_capacity-bit scratch, cleared each tick. */
\tuint32_t *parent_of_internal; /* size internal_capacity, caller-owned */
\tuint32_t *leaf_to_internal; /* size capacity, caller-owned, indexed by node id */
\tuint64_t *touched_bits; /* size (internal_capacity + 63) / 64, caller-owned */
\t/* Meta-bitmap over touched_bits: bit i in touched_meta_bits is set iff
\t * touched_bits[i] has any set bits. Lets the refit scan skip empty
\t * words in O(1) via __builtin_clzll instead of iterating the whole
\t * [min_word..max_word] range. Kills the N/64 term in the scan phase,
\t * leaving a strict O(K + n_marked) refit bound. */
\tuint64_t *touched_meta_bits; /* size ((internal_capacity + 63)/64 + 63)/64, caller-owned */
};
using pbvh_tree_t = pbvh_tree<int64_t>;

/* ============================================================================
 * BUCKET AUTO-TUNE (Phase 2e)
 *
 * Target max entities per bucket. Controls the constant-time upper bound on
 * pbvh_tree_aabb_query_b's per-bucket scan: a bucket cannot exceed
 * ceil(N / (1 << bucket_bits)) entities, so bucket_bits = ceil(log2(N/K))
 * gives at most K entities per bucket under uniform Hilbert distribution.
 * Empirical max/mean on uniform random inputs is ~1.06-1.30x (sub-Poisson),
 * so a K_TARGET of 32 yields worst-case ~40-entity scans at any N.
 * ========================================================================= */
#ifndef PBVH_BUCKET_K_TARGET
#define PBVH_BUCKET_K_TARGET 32u
#endif
"

def treeC : String := treeBanner ++ treeBody

end PredictiveBVH.Codegen.TreeC
