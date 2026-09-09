// Crate root: generated polynomials + spatial primitives, then the adapter.
// Both are included at the same scope so `crate::Aabb`, `crate::hilbert_of_aabb`
// etc. resolve correctly from within predictive_bvh_adapter.rs.

include!("../predictive_bvh.rs");
include!("../predictive_bvh_adapter.rs");
