// SPDX-License-Identifier: MIT
// pbvh_test - CLI smoke-test / benchmark for the Rust PredictiveBVH adapter.
//
// Usage:
//   cargo run --release -- [OPTIONS]
//
// Options:
//   --n <N>           Number of entities (default: 4096)
//   --q <Q>           Number of AABB queries (default: 256)
//   --dirty <D>       Dirty fraction per tick 0..100 (default: 20)
//   --ticks <T>       Number of tick iterations (default: 5)
//   --seed <S>        PRNG seed (default: 42)
//   --bench           Print timing summary
//   --hilbert-check   Verify hilbert3d / hilbert3d_inverse round-trips
//
// Exit code 0 = all assertions passed.

#![allow(dead_code, unused_variables)]

use predictive_bvh::{
    Aabb, PredictiveBvh, PbvhDirtyLeaf,
    hilbert3d, hilbert3d_inverse_bridge,
};

// --- Minimal PRNG (xorshift32) -----------------------------------------------

fn xorshift32(state: &mut u32) -> u32 {
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    *state
}

fn rand_i64(state: &mut u32, min: i64, max: i64) -> i64 {
    let r = xorshift32(state) as i64;
    min + r.abs() % (max - min + 1)
}

fn rand_aabb(state: &mut u32, scene_half: i64, max_extent: i64) -> Aabb {
    let cx = rand_i64(state, -scene_half, scene_half);
    let cy = rand_i64(state, -scene_half, scene_half);
    let cz = rand_i64(state, -scene_half, scene_half);
    let ex = rand_i64(state, 1, max_extent);
    let ey = rand_i64(state, 1, max_extent);
    let ez = rand_i64(state, 1, max_extent);
    Aabb {
        min_x: cx - ex, max_x: cx + ex,
        min_y: cy - ey, max_y: cy + ey,
        min_z: cz - ez, max_z: cz + ez,
    }
}

// --- Argument parsing --------------------------------------------------------

struct Args {
    n: usize,
    q: usize,
    dirty_pct: u32,
    ticks: usize,
    seed: u32,
    bench: bool,
    hilbert_check: bool,
}

impl Default for Args {
    fn default() -> Self {
        Self { n: 4096, q: 256, dirty_pct: 20, ticks: 5, seed: 42, bench: false, hilbert_check: false }
    }
}

fn parse_args() -> Args {
    let mut a = Args::default();
    let raw: Vec<String> = std::env::args().collect();
    let mut i = 1;
    while i < raw.len() {
        match raw[i].as_str() {
            "--n"             => { i += 1; a.n = raw[i].parse().expect("--n needs integer"); }
            "--q"             => { i += 1; a.q = raw[i].parse().expect("--q needs integer"); }
            "--dirty"         => { i += 1; a.dirty_pct = raw[i].parse().expect("--dirty 0..100"); }
            "--ticks"         => { i += 1; a.ticks = raw[i].parse().expect("--ticks needs integer"); }
            "--seed"          => { i += 1; a.seed = raw[i].parse().expect("--seed needs u32"); }
            "--bench"         => { a.bench = true; }
            "--hilbert-check" => { a.hilbert_check = true; }
            other             => { eprintln!("Unknown flag: {other}"); std::process::exit(1); }
        }
        i += 1;
    }
    a
}

// --- Hilbert round-trip tests -------------------------------------------------

fn test_hilbert() {
    let cases: &[(u32, u32, u32)] = &[
        (0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1),
        (5, 3, 7), (100, 200, 300), (511, 511, 511),
        (1023, 1023, 1023), (0, 0, 1023), (1023, 0, 0),
    ];
    let mut pass = 0;
    for &(x, y, z) in cases {
        let h = hilbert3d(x, y, z);
        let (ox, oy, oz) = hilbert3d_inverse_bridge(h);
        if ox == x && oy == y && oz == z {
            pass += 1;
        } else {
            eprintln!("FAIL hilbert3d({x},{y},{z}) h={h} -> ({ox},{oy},{oz})");
        }
    }
    println!("hilbert round-trip: {pass}/{} passed", cases.len());
    assert_eq!(pass, cases.len(), "hilbert round-trip failures");
}

// --- Brute-force overlap count -----------------------------------------------

fn brute_count(bvh: &PredictiveBvh, query: &Aabb) -> usize {
    (0..bvh.tree.count as usize)
        .filter(|&i| bvh.tree.nodes[i].is_leaf && bvh.tree.nodes[i].bounds.overlaps(query))
        .count()
}

// --- Timing helper -----------------------------------------------------------

fn now_us() -> u64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_micros() as u64
}

// --- Main --------------------------------------------------------------------

fn main() {
    let args = parse_args();

    if args.hilbert_check {
        test_hilbert();
    }

    let scene_half: i64 = 5_000_000_000;
    let max_extent: i64 = 50_000;
    let scene_aabb = Aabb {
        min_x: -scene_half, max_x: scene_half,
        min_y: -scene_half, max_y: scene_half,
        min_z: -scene_half, max_z: scene_half,
    };

    let mut rng = args.seed;

    let mut bvh = PredictiveBvh::new();
    bvh.set_scene_aabb(scene_aabb);

    let mut ids = Vec::with_capacity(args.n);
    let t0 = now_us();
    for i in 0..args.n {
        let b = rand_aabb(&mut rng, scene_half, max_extent);
        let id = bvh.insert(b, i as u32);
        ids.push(id);
    }
    let insert_us = now_us() - t0;

    let t0 = now_us();
    bvh.tick(&[]);
    let build_us = now_us() - t0;

    // Query correctness: aabb_query_n must never under-report vs brute force
    let mut q_rng = args.seed.wrapping_add(99);
    let mut mismatches = 0usize;
    for _ in 0..args.q {
        let q = rand_aabb(&mut q_rng, scene_half, max_extent * 4);
        let mut hits_n = 0usize;
        bvh.aabb_query(&q, |_id| { hits_n += 1; false });
        let hits_brute = brute_count(&bvh, &q);
        if hits_n < hits_brute {
            mismatches += 1;
            eprintln!("  under-report: aabb_query_n={hits_n} brute={hits_brute}");
        }
    }
    if mismatches == 0 {
        println!("query correctness: OK ({} queries, 0 under-reports)", args.q);
    } else {
        eprintln!("query correctness: FAIL ({mismatches} under-reports)");
        std::process::exit(1);
    }

    // Tick iterations
    let dirty_count = (args.n * args.dirty_pct as usize / 100).max(1);
    let mut total_tick_us = 0u64;

    for tick in 0..args.ticks {
        let mut dirty_leaves = Vec::with_capacity(dirty_count);
        for _ in 0..dirty_count {
            let idx = (xorshift32(&mut rng) as usize) % ids.len();
            let id = ids[idx];
            let old_h = bvh.tree.nodes[id as usize].hilbert;
            let new_b = rand_aabb(&mut rng, scene_half, max_extent);
            bvh.update(id, new_b);
            dirty_leaves.push(PbvhDirtyLeaf { leaf_id: id, old_hilbert: old_h });
        }
        let t0 = now_us();
        bvh.tick(&dirty_leaves);
        total_tick_us += now_us() - t0;

        let mut q2_rng = args.seed.wrapping_add(tick as u32 * 7 + 13);
        for _ in 0..(args.q / args.ticks).max(1) {
            let q = rand_aabb(&mut q2_rng, scene_half, max_extent * 4);
            let mut hits_n = 0usize;
            bvh.aabb_query(&q, |_id| { hits_n += 1; false });
            let hits_brute = brute_count(&bvh, &q);
            if hits_n < hits_brute {
                mismatches += 1;
            }
        }
    }

    if mismatches == 0 {
        println!("tick correctness: OK ({} ticks x ~{} dirty)", args.ticks, dirty_count);
    } else {
        eprintln!("tick correctness: FAIL ({mismatches} total under-reports)");
        std::process::exit(1);
    }

    // Remove round-trip
    let remove_n = ids.len() / 4;
    for i in 0..remove_n {
        bvh.remove(ids[i]);
    }
    bvh.tick(&[]);
    let mut hits_after_remove = 0usize;
    bvh.aabb_query(&scene_aabb, |_| { hits_after_remove += 1; false });
    let expected_after = args.n - remove_n;
    assert_eq!(hits_after_remove, expected_after,
        "after remove: expected {expected_after} hits, got {hits_after_remove}");
    println!("remove round-trip: OK (removed {remove_n}, {hits_after_remove} remain)");

    // Clear + re-insert
    bvh.clear();
    assert!(bvh.is_empty(), "after clear: tree should be empty");
    let small_b = Aabb { min_x: -1, max_x: 1, min_y: -1, max_y: 1, min_z: -1, max_z: 1 };
    bvh.insert(small_b, 0);
    bvh.tick(&[]);
    let mut hits_single = 0usize;
    bvh.aabb_query(&small_b, |_| { hits_single += 1; false });
    assert_eq!(hits_single, 1, "single-insert query should return 1 hit");
    println!("clear + re-insert: OK");

    // Ray query smoke test
    bvh.clear();
    bvh.set_scene_aabb(scene_aabb);
    let target = Aabb { min_x: 1000, max_x: 2000, min_y: 1000, max_y: 2000, min_z: 1000, max_z: 2000 };
    bvh.insert(target, 99);
    bvh.tick(&[]);
    let mut ray_hits = 0usize;
    bvh.ray_query(0, 0, 0, 3_000_000, 3_000_000, 3_000_000, |_| { ray_hits += 1; false });
    assert!(ray_hits >= 1, "ray through target box should get >= 1 hit");
    println!("ray query smoke test: OK ({ray_hits} hit(s))");

    // Enumerate pairs
    bvh.clear();
    bvh.set_scene_aabb(scene_aabb);
    bvh.insert(Aabb { min_x: 0, max_x: 10000, min_y: 0, max_y: 10000, min_z: 0, max_z: 10000 }, 0);
    bvh.insert(Aabb { min_x: 5000, max_x: 15000, min_y: 5000, max_y: 15000, min_z: 5000, max_z: 15000 }, 1);
    bvh.insert(Aabb { min_x: 100000, max_x: 110000, min_y: 100000, max_y: 110000, min_z: 100000, max_z: 110000 }, 2);
    bvh.tick(&[]);
    let pairs = bvh.tree.enumerate_pairs(|_a, _b| false);
    assert_eq!(pairs, 1, "only (0,1) should form a pair, got {pairs}");
    println!("enumerate_pairs: OK ({pairs} pair)");

    // Index slot
    bvh.set_index(7);
    assert_eq!(bvh.get_index(), 7);
    println!("index slot: OK");

    if args.bench {
        println!("\n-- Benchmark (N={}, Q={}) --", args.n, args.q);
        println!("  insert {} entities:        {}us ({:.1}us/entity)",
            args.n, insert_us, insert_us as f64 / args.n as f64);
        println!("  initial build:             {}us", build_us);
        println!("  {} ticks ({} dirty each):  {}us total ({:.1}us/tick)",
            args.ticks, dirty_count, total_tick_us, total_tick_us as f64 / args.ticks as f64);
    }

    println!("\nAll tests passed.");
}
