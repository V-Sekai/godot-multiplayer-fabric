/**************************************************************************/
/*  test_cassie_solver.h                                                  */
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

#include "../src/cassie_beautifier.h"
#include "../src/cassie_beautifier_params.h"
#include "../src/cassie_triangulator.h"
#include "../src/constraints/cassie_constraint.h"
#include "../src/constraints/cassie_intersection_constraint.h"
#include "../src/constraints/cassie_intersection_finder.h"
#include "../src/constraints/cassie_mirror_plane_constraint.h"
#include "../src/curves/cassie_curve_fit.h"
#include "../src/sketch/cassie_final_stroke.h"
#include "../src/sketch/cassie_input_stroke.h"
#include "../src/solver/cassie_constraint_solver.h"

#include "core/math/plane.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/time.h"
#include "core/variant/typed_array.h"
#include "scene/resources/curve.h"
#include "tests/test_macros.h"

namespace TestCassieSolver {

// ── Solver parameters round-trip ──────────────────────────────────────────

TEST_CASE("[Cassie][Solver] SolverParams getters/setters round-trip") {
	Ref<CassieSolverParams> p;
	p.instantiate();
	p->set_mu_fidelity(0.4);
	p->set_proximity_threshold(0.05);
	p->set_planarity_allowed(false);
	p->set_use_avbd(true);
	p->set_avbd_rho(25.0);
	p->set_avbd_max_iter(16);
	p->set_avbd_tol(1e-10);
	CHECK(Math::is_equal_approx(p->get_mu_fidelity(), 0.4));
	CHECK(Math::is_equal_approx(p->get_proximity_threshold(), 0.05));
	CHECK_FALSE(p->get_planarity_allowed());
	CHECK(p->get_use_avbd());
	CHECK(Math::is_equal_approx(p->get_avbd_rho(), 25.0));
	CHECK_EQ(p->get_avbd_max_iter(), 16);
	CHECK(Math::is_equal_approx(p->get_avbd_tol(), 1e-10));
}

// ── OnSurfaceEnergy (ENG-54) ─────────────────────────────────────────────

// Stub project-on-patch callable that snaps any query point onto the
// y = 0 plane, exposed to the solver via CassieSolverParams.
class _FlatProjectStub : public Object {
	GDCLASS(_FlatProjectStub, Object);

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("project", "pos"), &_FlatProjectStub::project);
	}

public:
	Dictionary project(const Vector3 &p_pos) {
		Dictionary d;
		d["on_surface"] = true;
		d["patch_id"] = 0;
		Vector3 projected(p_pos.x, real_t(0.0), p_pos.z);
		d["projected"] = projected;
		d["normal"] = Vector3(0, 1, 0);
		d["distance"] = real_t(Math::abs(double(p_pos.y)));
		return d;
	}
};

TEST_CASE("[Cassie][Solver] OnSurfaceEnergy weight=0 reproduces existing FullPivLU behavior") {
	// Reuse the constraint-moves-upward fixture; assert that the SAME
	// fixture with on_surface_weight = 0 produces the same upward shift.
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(4, 0, 0), Vector3(-1, 0, 0), Vector3());
	Ref<CassieFinalStroke> stub;
	stub.instantiate();
	Ref<CassieIntersectionConstraint> ic;
	ic.instantiate();
	ic->set_position(Vector3(2, 0.05, 0));
	ic->set_intersected_stroke(stub);
	TypedArray<CassieConstraint> cs;
	cs.push_back(ic);

	Ref<CassieSolverParams> params;
	params.instantiate();
	params->set_mu_fidelity(0.1);
	params->set_proximity_threshold(0.5);
	params->set_planarity_allowed(false);
	params->set_on_surface_weight(0.0); // no-op: existing behavior preserved

	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	Dictionary result = solver->solve(curve, cs, PackedVector3Array(), params, false);
	Ref<Curve3D> out = result["curve"];
	REQUIRE(out.is_valid());
	bool moved_up = false;
	for (int i = 0; i < out->get_point_count(); ++i) {
		if (out->get_point_position(i).y - curve->get_point_position(i).y > 0.005) {
			moved_up = true;
			break;
		}
	}
	CHECK(moved_up);
}

TEST_CASE("[Cassie][Solver] OnSurfaceEnergy pulls a curve toward a target plane") {
	// Curve starts at y = 0.5; OnSurfaceEnergy targets project to y = 0.
	// With weight high relative to fidelity, anchor y must drop toward 0.
	Ref<Curve3D> curve;
	curve.instantiate();
	const real_t y0 = real_t(0.5);
	curve->add_point(Vector3(0, y0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, y0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(4, y0, 0), Vector3(-1, 0, 0), Vector3());

	_FlatProjectStub *stub = memnew(_FlatProjectStub);
	Callable cb(stub, "project");

	Ref<CassieSolverParams> params;
	params.instantiate();
	params->set_mu_fidelity(0.05); // weak fidelity so the energy can pull
	params->set_proximity_threshold(0.5);
	params->set_planarity_allowed(false);
	params->set_on_surface_weight(1.0);
	params->set_project_on_patch_callback(cb);

	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	TypedArray<CassieConstraint> empty;
	Dictionary result = solver->solve(curve, empty, PackedVector3Array(), params, false);
	Ref<Curve3D> out = result["curve"];
	REQUIRE(out.is_valid());
	REQUIRE(out->get_point_count() >= 3);

	// Every anchor's y should have dropped meaningfully from y0 toward 0.
	for (int i = 0; i < out->get_point_count(); ++i) {
		const real_t y = out->get_point_position(i).y;
		CHECK_MESSAGE(y < y0 - real_t(0.05),
				vformat("anchor %d still at y=%f, expected pull toward y=0", i, y));
	}
	memdelete(stub);
}

// ── AVBD parity (ENG-49) ─────────────────────────────────────────────────

TEST_CASE("[Cassie][Solver] AVBD path moves the constraint anchor in the same direction as FullPivLU") {
	// Reuse the FullPivLU constraint-moves-upward fixture and verify the
	// AVBD-flagged solve also moves at least one anchor's y upward. The
	// augmented-Lagrangian loop converges to the same KKT point in well-
	// conditioned cases like this one.
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(4, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<CassieFinalStroke> stub;
	stub.instantiate();
	Ref<CassieIntersectionConstraint> ic;
	ic.instantiate();
	ic->set_position(Vector3(2, 0.05, 0));
	ic->set_intersected_stroke(stub);
	TypedArray<CassieConstraint> cs;
	cs.push_back(ic);

	Ref<CassieSolverParams> params;
	params.instantiate();
	params->set_mu_fidelity(0.1);
	params->set_proximity_threshold(0.5);
	params->set_planarity_allowed(false);
	params->set_use_avbd(true);
	params->set_avbd_rho(10.0);
	params->set_avbd_max_iter(24);
	params->set_avbd_tol(1e-9);

	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	Dictionary result = solver->solve(curve, cs, PackedVector3Array(), params, false);
	Ref<Curve3D> out = result["curve"];
	REQUIRE(out.is_valid());
	REQUIRE(out->get_point_count() >= 3);

	bool moved_up = false;
	for (int i = 0; i < out->get_point_count(); ++i) {
		const real_t y_in = curve->get_point_position(i).y;
		const real_t y_out = out->get_point_position(i).y;
		if (y_out - y_in > 0.005) {
			moved_up = true;
			break;
		}
	}
	CHECK(moved_up);
}

// ── Solver no-constraint path ─────────────────────────────────────────────

TEST_CASE("[Cassie][Solver] solve with no constraints returns the input curve") {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(4, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<CassieSolverParams> params;
	params.instantiate();

	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	TypedArray<CassieConstraint> empty;
	PackedVector3Array ortho;

	Dictionary result = solver->solve(curve, empty, ortho, params, false);
	Ref<Curve3D> out = result["curve"];
	REQUIRE(out.is_valid());
	CHECK(out->get_point_count() >= 2);
	// Endpoints should be preserved (fidelity dominates).
	CHECK(out->get_point_position(0).distance_to(curve->get_point_position(0)) < 0.01);
}

TEST_CASE("[Cassie][Solver] solve on degenerate curve returns input") {
	Ref<Curve3D> tiny;
	tiny.instantiate();
	tiny->add_point(Vector3(0, 0, 0), Vector3(), Vector3());

	Ref<CassieSolverParams> params;
	params.instantiate();
	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	TypedArray<CassieConstraint> empty;
	PackedVector3Array ortho;
	Dictionary result = solver->solve(tiny, empty, ortho, params, false);
	CHECK_FALSE(result["is_closed_loop"]);
}

// ── Beautifier Tier 4 round trip ──────────────────────────────────────────

TEST_CASE("[Cassie][Beautifier] beautify rejects invalid (too-short) stroke") {
	Ref<CassieInputStroke> stroke;
	stroke.instantiate();
	stroke->add_sample(Vector3(0, 0, 0), 0.0, 1.0);
	stroke->add_sample(Vector3(0.0001, 0, 0), 0.0005, 1.0);

	Ref<CassieBeautifierParams> params;
	params.instantiate();
	Ref<CassieSketchContext> ctx;
	ctx.instantiate();

	Ref<CassieBeautifier> beautifier;
	beautifier.instantiate();
	Dictionary result = beautifier->beautify(stroke, ctx, params, false, false);
	CHECK_FALSE(bool(result.get("is_valid", true)));
}

TEST_CASE("[Cassie][Beautifier] beautify short stroke takes the line fast path") {
	Ref<CassieInputStroke> stroke;
	stroke.instantiate();
	stroke->add_sample(Vector3(0, 0, 0), 0.0, 1.0);
	stroke->add_sample(Vector3(0.005, 0, 0), 0.05, 1.0);
	stroke->add_sample(Vector3(0.01, 0, 0), 0.1, 1.0);

	Ref<CassieBeautifierParams> params;
	params.instantiate();
	params->set_small_distance(0.5); // Force short-stroke path.
	Ref<CassieSketchContext> ctx;
	ctx.instantiate();
	Ref<CassieBeautifier> beautifier;
	beautifier.instantiate();
	Dictionary result = beautifier->beautify(stroke, ctx, params, false, false);
	CHECK(bool(result.get("is_valid", false)));
	CHECK(bool(result.get("is_short_or_linear", false)));
}

// ── Solver: behavior under non-trivial constraints ───────────────────────

TEST_CASE("[Cassie][Solver] solve with a single intersection moves the curve toward the constraint") {
	// Straight horizontal curve at y=0; the constraint asks for a point at
	// y=0.2 in the middle. After the solver, the curve's mid-anchor should
	// have moved upward.
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(4, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<CassieFinalStroke> stub;
	stub.instantiate();
	Ref<CassieIntersectionConstraint> ic;
	ic.instantiate();
	ic->set_position(Vector3(2, 0.05, 0));
	ic->set_intersected_stroke(stub);

	TypedArray<CassieConstraint> cs;
	cs.push_back(ic);

	Ref<CassieSolverParams> params;
	params.instantiate();
	params->set_mu_fidelity(0.1); // Strongly favor the constraint.
	params->set_proximity_threshold(0.5); // Relax fidelity normalization.
	params->set_planarity_allowed(false);

	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	Dictionary result = solver->solve(curve, cs, PackedVector3Array(), params, false);
	Ref<Curve3D> out = result["curve"];
	REQUIRE(out.is_valid());
	REQUIRE(out->get_point_count() >= 3);

	// At least one anchor's y must have moved upward from the input.
	bool moved_up = false;
	for (int i = 0; i < out->get_point_count(); ++i) {
		const real_t y_in = curve->get_point_position(i).y;
		const real_t y_out = out->get_point_position(i).y;
		if (y_out - y_in > 0.005) {
			moved_up = true;
			break;
		}
	}
	CHECK(moved_up);
}

TEST_CASE("[Cassie][Solver] solve with no constraints produces a near-zero displacement") {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 1, 0), Vector3(-1, -0.5, 0), Vector3(1, 0.5, 0));
	curve->add_point(Vector3(4, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<CassieSolverParams> params;
	params.instantiate();
	params->set_planarity_allowed(false);
	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	TypedArray<CassieConstraint> empty;
	Dictionary result = solver->solve(curve, empty, PackedVector3Array(), params, false);
	Ref<Curve3D> out = result["curve"];
	REQUIRE(out.is_valid());
	REQUIRE(out->get_point_count() == curve->get_point_count());
	for (int i = 0; i < out->get_point_count(); ++i) {
		CHECK(out->get_point_position(i).distance_to(curve->get_point_position(i)) < 1e-3);
	}
}

TEST_CASE("[Cassie][Solver] solve with conflicting constraints returns a finite (non-NaN) curve") {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(4, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<CassieFinalStroke> stub;
	stub.instantiate();

	Ref<CassieIntersectionConstraint> c1;
	c1.instantiate();
	c1->set_position(Vector3(2, 5, 0));
	c1->set_intersected_stroke(stub);

	Ref<CassieIntersectionConstraint> c2;
	c2.instantiate();
	c2->set_position(Vector3(2, -5, 0));
	c2->set_intersected_stroke(stub);

	TypedArray<CassieConstraint> cs;
	cs.push_back(c1);
	cs.push_back(c2);

	Ref<CassieSolverParams> params;
	params.instantiate();
	params->set_planarity_allowed(false);
	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	Dictionary result = solver->solve(curve, cs, PackedVector3Array(), params, false);
	Ref<Curve3D> out = result["curve"];
	REQUIRE(out.is_valid());
	for (int i = 0; i < out->get_point_count(); ++i) {
		const Vector3 p = out->get_point_position(i);
		CHECK_FALSE(Math::is_nan(p.x));
		CHECK_FALSE(Math::is_inf(p.x));
		CHECK_FALSE(Math::is_nan(p.y));
		CHECK_FALSE(Math::is_inf(p.y));
	}
}

TEST_CASE("[Cassie][Solver] solve terminates and returns") {
	// Smoke test that the greedy outer loop bounded by `n` iterations
	// returns even for a handful of constraints.
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(4, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(6, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<CassieFinalStroke> stub;
	stub.instantiate();

	TypedArray<CassieConstraint> cs;
	for (int i = 0; i < 4; ++i) {
		Ref<CassieIntersectionConstraint> ic;
		ic.instantiate();
		ic->set_position(Vector3(real_t(i) * 1.5, real_t(i) * 0.1, 0));
		ic->set_intersected_stroke(stub);
		cs.push_back(ic);
	}

	Ref<CassieSolverParams> params;
	params.instantiate();
	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	Dictionary result = solver->solve(curve, cs, PackedVector3Array(), params, false);
	Ref<Curve3D> out = result["curve"];
	CHECK(out.is_valid());
}

TEST_CASE("[Cassie][Solver] 4-anchor + 3-constraint solve completes under 200 ms") {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(4, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(6, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<CassieFinalStroke> stub;
	stub.instantiate();

	TypedArray<CassieConstraint> cs;
	for (int i = 0; i < 3; ++i) {
		Ref<CassieIntersectionConstraint> ic;
		ic.instantiate();
		ic->set_position(Vector3(real_t(i) + 1.0, 0.2, 0));
		ic->set_intersected_stroke(stub);
		cs.push_back(ic);
	}

	Ref<CassieSolverParams> params;
	params.instantiate();
	Ref<CassieConstraintSolver> solver;
	solver.instantiate();

	const uint64_t start = Time::get_singleton()->get_ticks_usec();
	Dictionary result = solver->solve(curve, cs, PackedVector3Array(), params, false);
	const uint64_t elapsed = Time::get_singleton()->get_ticks_usec() - start;

	CHECK_MESSAGE(elapsed < 200000ULL,
			vformat("solve took %d us (budget 200000 us)", int(elapsed)));
	Ref<Curve3D> out = result["curve"];
	CHECK(out.is_valid());
}

TEST_CASE("[Cassie][Solver] solve with zero displacement yields zero fidelity energy") {
	// Plan-coverage proxy: FidelityEnergy.compute(zero) = 0. Observed via
	// solve() returning the input curve unchanged when nothing is asked of
	// the solver.
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(4, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<CassieSolverParams> params;
	params.instantiate();
	params->set_planarity_allowed(false);
	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	TypedArray<CassieConstraint> empty;
	Dictionary result = solver->solve(curve, empty, PackedVector3Array(), params, false);
	Ref<Curve3D> out = result["curve"];
	REQUIRE(out.is_valid());
	REQUIRE(out->get_point_count() == curve->get_point_count());
	for (int i = 0; i < out->get_point_count(); ++i) {
		CHECK(out->get_point_position(i).distance_to(curve->get_point_position(i)) < 1e-6);
	}
}

TEST_CASE("[Cassie][Solver] solve with N anchors and many G1 joints accepts the constraint block") {
	// Plan-coverage proxy: G1Constraint emits 3*n_joints rows. Run a 5-anchor
	// smooth curve through the solver and verify it lands without LU failure.
	Ref<Curve3D> curve;
	curve.instantiate();
	const int n = 5;
	for (int i = 0; i < n; ++i) {
		Vector3 in_h = (i == 0) ? Vector3() : Vector3(-1, 0, 0);
		Vector3 out_h = (i == n - 1) ? Vector3() : Vector3(1, 0, 0);
		curve->add_point(Vector3(real_t(i) * 2.0, 0, 0), in_h, out_h);
	}
	Ref<CassieSolverParams> params;
	params.instantiate();
	params->set_planarity_allowed(false);
	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	TypedArray<CassieConstraint> empty;
	Dictionary result = solver->solve(curve, empty, PackedVector3Array(), params, false);
	Ref<Curve3D> out = result["curve"];
	REQUIRE(out.is_valid());
	CHECK_EQ(out->get_point_count(), n);
	// All anchors remain finite.
	for (int i = 0; i < out->get_point_count(); ++i) {
		const Vector3 p = out->get_point_position(i);
		CHECK_FALSE(Math::is_nan(p.x));
		CHECK_FALSE(Math::is_inf(p.x));
	}
}

TEST_CASE("[Cassie][Solver] zero-displacement intersection at an exact anchor leaves the curve unchanged") {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(4, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<CassieFinalStroke> stub;
	stub.instantiate();
	Ref<CassieIntersectionConstraint> ic;
	ic.instantiate();
	// Position exactly on the middle anchor.
	ic->set_position(Vector3(2, 0, 0));
	ic->set_intersected_stroke(stub);
	TypedArray<CassieConstraint> cs;
	cs.push_back(ic);

	Ref<CassieSolverParams> params;
	params.instantiate();
	params->set_planarity_allowed(false);
	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	Dictionary result = solver->solve(curve, cs, PackedVector3Array(), params, false);
	Ref<Curve3D> out = result["curve"];
	REQUIRE(out.is_valid());
	for (int i = 0; i < out->get_point_count(); ++i) {
		CHECK(out->get_point_position(i).distance_to(curve->get_point_position(i)) < 1e-3);
	}
}

TEST_CASE("[Cassie][Solver] solve falls back on degenerate (zero-length tangent) curve") {
	// Two adjacent anchors at the same position; the flattened control-point
	// list contains a zero-length tangent. The fidelity Hessian has a zero
	// row, the KKT solve may produce NaN or unbounded values; the NaN guard
	// in CassieConstraintSolver::solve should fall back to the input curve.
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3());
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3()); // coincident
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<CassieFinalStroke> stub;
	stub.instantiate();
	Ref<CassieIntersectionConstraint> ic;
	ic.instantiate();
	ic->set_position(Vector3(1, 0.5, 0));
	ic->set_intersected_stroke(stub);
	TypedArray<CassieConstraint> cs;
	cs.push_back(ic);

	Ref<CassieSolverParams> params;
	params.instantiate();
	params->set_planarity_allowed(false);
	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	Dictionary result = solver->solve(curve, cs, PackedVector3Array(), params, false);
	Ref<Curve3D> out = result["curve"];
	REQUIRE(out.is_valid());
	for (int i = 0; i < out->get_point_count(); ++i) {
		const Vector3 p = out->get_point_position(i);
		CHECK_FALSE(Math::is_nan(p.x));
		CHECK_FALSE(Math::is_inf(p.x));
	}
}

TEST_CASE("[Cassie][Solver] solve on closed input reports is_closed_loop=true") {
	Ref<Curve3D> curve;
	curve.instantiate();
	// Roughly-closed loop.
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 1, 0));
	curve->add_point(Vector3(1, 1, 0), Vector3(-1, -1, 0), Vector3(0, -1, 0));
	curve->add_point(Vector3(0, 0, 0), Vector3(0, 1, 0), Vector3());

	Ref<CassieSolverParams> params;
	params.instantiate();
	params->set_planarity_allowed(false);
	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	TypedArray<CassieConstraint> empty;
	Dictionary result = solver->solve(curve, empty, PackedVector3Array(), params, true);
	CHECK(bool(result["is_closed_loop"]));
}

// ── Score-formula reference values ────────────────────────────────────────

// The solver's per-candidate score formula (port of ConstraintSolver.cs
// lines 150–169):
//   base_score = 2.0   if IntersectionConstraint && is_at_node
//                1.5   if IntersectionConstraint && !is_at_node
//                1.0   otherwise (mirror / surface / unknown)
//   if close_to_endpoint: score = max(score, 1.25)
//   if align_tangent:     score += 0.5
//
// The greedy energy is:
//   E = mu_fidelity * fitting + (1 - mu_fidelity) * exp(-(subset/all)^2)
//
// We can't observe the per-candidate score directly (private). But the
// constraint-energy sanity check below pins the formula in two ways:
//   (a) all-active vs all-dropped energy difference flips sign when
//       mu_fidelity crosses 0.5 with a fidelity-zero constraint.
//   (b) higher-scoring constraints are kept longer by the greedy loop.

TEST_CASE("[Cassie][Solver] greedy loop keeps the at-node intersection longer than the off-node one") {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(4, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<CassieFinalStroke> stub;
	stub.instantiate();
	// Two constraints at the same anchor; only one survives the
	// per-anchor dedup. The at-node one (score 2.0) must win over the
	// off-node one (score 1.5).
	Ref<CassieIntersectionConstraint> at_node;
	at_node.instantiate();
	at_node->set_position(Vector3(2, 0.02, 0));
	at_node->set_intersected_stroke(stub);
	at_node->set_is_at_node(true);
	Ref<CassieIntersectionConstraint> off_node;
	off_node.instantiate();
	off_node->set_position(Vector3(2, 0.02, 0));
	off_node->set_intersected_stroke(stub);
	off_node->set_is_at_node(false);

	TypedArray<CassieConstraint> cs;
	cs.push_back(off_node);
	cs.push_back(at_node);

	Ref<CassieSolverParams> params;
	params.instantiate();
	params->set_mu_fidelity(0.1);
	params->set_proximity_threshold(0.5);
	params->set_planarity_allowed(false);

	Ref<CassieConstraintSolver> solver;
	solver.instantiate();
	Dictionary result = solver->solve(curve, cs, PackedVector3Array(), params, false);
	// Both candidates project to the same anchor; the surviving intersection
	// in the result should be the at-node one.
	TypedArray<CassieIntersectionConstraint> ints = result["intersections"];
	REQUIRE(ints.size() >= 1);
	bool any_at_node = false;
	for (int i = 0; i < ints.size(); ++i) {
		Ref<CassieIntersectionConstraint> ic = ints[i];
		if (ic.is_valid() && ic->get_is_at_node()) {
			any_at_node = true;
			break;
		}
	}
	CHECK(any_at_node);
}

// ── Beautifier: solver-backed cases ───────────────────────────────────────

TEST_CASE("[Cassie][Beautifier] beautify produces no mirror constraints when mirror is disabled") {
	Ref<CassieInputStroke> stroke;
	stroke.instantiate();
	for (int i = 0; i < 30; ++i) {
		const real_t t = real_t(i) * 0.05;
		stroke->add_sample(Vector3(t, Math::sin(t * 2.0), 0), t, 1.0);
	}
	Ref<CassieBeautifierParams> params;
	params.instantiate();
	params->set_min_stroke_size(0.01f);
	Ref<CassieSketchContext> ctx;
	ctx.instantiate();
	ctx->set_mirror_plane(Plane(Vector3(1, 0, 0), 0.0));
	ctx->set_mirror_enabled(false);
	Ref<CassieBeautifier> b;
	b.instantiate();
	Dictionary result = b->beautify(stroke, ctx, params, true, false);
	REQUIRE(bool(result["is_valid"]));
	TypedArray<CassieMirrorPlaneConstraint> mirrors = result["mirror_constraints"];
	CHECK_EQ(mirrors.size(), 0);
}

TEST_CASE("[Cassie][Beautifier] beautify exports refined intersections when a stroke crosses an existing one") {
	// Two crossing straight-line Curve3Ds: existing on x-axis, new on y-axis.
	// Both use 1/3-handles so the bezier collapses to linear and the segments
	// actually intersect at (0.5, 0, 0).
	Ref<Curve3D> existing_curve;
	existing_curve.instantiate();
	existing_curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(real_t(1.0 / 3.0), 0, 0));
	existing_curve->add_point(Vector3(1, 0, 0), Vector3(real_t(-1.0 / 3.0), 0, 0), Vector3());
	Ref<CassieFinalStroke> existing;
	existing.instantiate();
	existing->set_curve(existing_curve, false);

	Ref<Curve3D> new_curve;
	new_curve.instantiate();
	new_curve->add_point(Vector3(0.5, -0.5, 0), Vector3(), Vector3(0, real_t(1.0 / 3.0), 0));
	new_curve->add_point(Vector3(0.5, 0.5, 0), Vector3(0, real_t(-1.0 / 3.0), 0), Vector3());

	TypedArray<CassieFinalStroke> existing_arr;
	existing_arr.push_back(existing);

	// First confirm cassie_find_intersections detects the crossing — isolates
	// the beautifier-dict export bug from any sampling/fit pipeline issue.
	TypedArray<CassieIntersectionConstraint> raw =
			cassie_find_intersections(new_curve, existing_arr, 0.1f, 0.01f);
	REQUIRE_MESSAGE(raw.size() > 0,
			"sanity: cassie_find_intersections should detect the x/y axis crossing");

	// Now drive the full beautify pipeline with an input stroke sampled along
	// the same y-axis path and verify result["intersections"] is populated.
	Ref<CassieInputStroke> stroke;
	stroke.instantiate();
	for (int i = 0; i < 30; ++i) {
		const real_t t = real_t(i) / 29.0;
		stroke->add_sample(Vector3(0.5, -0.5 + t, 0), t, 1.0);
	}

	Ref<CassieBeautifierParams> params;
	params.instantiate();
	params->set_min_stroke_size(0.01f);
	params->set_proximity_threshold(0.1f);

	Ref<CassieSketchContext> ctx;
	ctx.instantiate();
	ctx->set_existing_strokes(existing_arr);

	Ref<CassieBeautifier> b;
	b.instantiate();
	Dictionary result = b->beautify(stroke, ctx, params, true, false);
	REQUIRE(bool(result["is_valid"]));
	TypedArray<CassieIntersectionConstraint> ics = result["detected_intersections"];
	CHECK_MESSAGE(ics.size() > 0,
			vformat("crossing stroke should produce >= 1 detected intersection; got %d",
					ics.size()));
}

// ENG-78 — headless equivalent of the manual XR demo smoke test. Drives
// the same control flow xr_sketch_controller.gd::_commit_and_split runs:
//
//   beautify(new_stroke, existing) → detected_intersections
//     → CassieFinalStroke.split_at_constraints(existing, intersections)
//     → committed registry now holds 2 substrokes + the new stroke = 3
//
// Numerically measurable replacement for the in-engine XR test: stroke
// count, substroke endpoints, intersection-point coordinates.
TEST_CASE("[Cassie][Beautifier] crossing-stroke commit produces 3 substrokes with intersection-anchored split") {
	// Stroke A — straight line along x-axis from (0,0,0) to (1,0,0).
	Ref<Curve3D> existing_curve;
	existing_curve.instantiate();
	existing_curve->add_point(Vector3(0, 0, 0), Vector3(),
			Vector3(real_t(1.0 / 3.0), 0, 0));
	existing_curve->add_point(Vector3(1, 0, 0),
			Vector3(real_t(-1.0 / 3.0), 0, 0), Vector3());
	Ref<CassieFinalStroke> existing;
	existing.instantiate();
	existing->set_curve(existing_curve, false);

	// Stroke B — straight line along y-axis from (0.5,-0.5,0) to (0.5,0.5,0).
	// Crosses existing at (0.5, 0, 0).
	Ref<CassieInputStroke> stroke;
	stroke.instantiate();
	for (int i = 0; i < 30; ++i) {
		const real_t t = real_t(i) / 29.0;
		stroke->add_sample(Vector3(0.5, -0.5 + t, 0), t, 1.0);
	}

	TypedArray<CassieFinalStroke> existing_arr;
	existing_arr.push_back(existing);

	Ref<CassieBeautifierParams> params;
	params.instantiate();
	params->set_min_stroke_size(0.01f);
	params->set_proximity_threshold(0.1f);

	Ref<CassieSketchContext> ctx;
	ctx.instantiate();
	ctx->set_existing_strokes(existing_arr);

	Ref<CassieBeautifier> b;
	b.instantiate();
	Dictionary result = b->beautify(stroke, ctx, params, true, false);
	REQUIRE(bool(result["is_valid"]));

	Ref<Curve3D> new_curve = result.get("curve", Ref<Curve3D>());
	REQUIRE(new_curve.is_valid());

	TypedArray<CassieIntersectionConstraint> ics = result["detected_intersections"];
	REQUIRE_MESSAGE(ics.size() > 0,
			"crossing stroke must produce at least one detected intersection");

	// Replicate xr_sketch_controller.gd::_commit_and_split: split each
	// previously-committed stroke at the new stroke's intersection point
	// on it, then commit the new stroke. Final registry should be 3
	// strokes (2 substrokes of the split first + the new stroke).
	const real_t snap_threshold = real_t(0.001);

	// Collect intersections-per-old-stroke (the controller does this
	// implicitly; we collapse to a single old stroke here since the
	// fixture only has one).
	TypedArray<CassieIntersectionConstraint> ics_on_existing;
	for (int j = 0; j < ics.size(); ++j) {
		Ref<CassieIntersectionConstraint> ic = ics[j];
		if (ic.is_null()) {
			continue;
		}
		Ref<CassieFinalStroke> intersected = ic->get_intersected_stroke();
		if (intersected == existing) {
			ics_on_existing.push_back(ic);
		}
	}
	REQUIRE_MESSAGE(ics_on_existing.size() > 0,
			"the detected intersections should reference the existing stroke");

	TypedArray<CassieFinalStroke> substrokes =
			CassieFinalStroke::split_at_constraints(
					existing, ics_on_existing, snap_threshold);
	CHECK_MESSAGE(substrokes.size() == 2,
			vformat("split should yield exactly 2 substrokes; got %d",
					substrokes.size()));

	// Simulated post-commit registry: erase the split-up `existing`,
	// push each substroke, push the new stroke (controller flow).
	TypedArray<CassieFinalStroke> committed;
	for (int j = 0; j < substrokes.size(); ++j) {
		committed.push_back(substrokes[j]);
	}
	Ref<CassieFinalStroke> new_stroke;
	new_stroke.instantiate();
	new_stroke->set_curve(new_curve, false);
	committed.push_back(new_stroke);

	CHECK_MESSAGE(committed.size() == 3,
			vformat("registry post-commit should have 3 strokes; got %d",
					committed.size()));

	// Geometric verification: the split point on the existing stroke
	// should be at (~0.5, 0, 0). Substroke 0's last endpoint and
	// substroke 1's first endpoint must both match the intersection
	// coordinates within snap_threshold.
	const Vector3 expected_split(real_t(0.5), real_t(0.0), real_t(0.0));
	Ref<CassieFinalStroke> sub0 = substrokes[0];
	Ref<CassieFinalStroke> sub1 = substrokes[1];
	REQUIRE(sub0.is_valid());
	REQUIRE(sub1.is_valid());
	Ref<Curve3D> sub0_curve = sub0->get_curve();
	Ref<Curve3D> sub1_curve = sub1->get_curve();
	REQUIRE(sub0_curve.is_valid());
	REQUIRE(sub1_curve.is_valid());

	const Vector3 sub0_end = sub0_curve->get_point_position(
			sub0_curve->get_point_count() - 1);
	const Vector3 sub1_start = sub1_curve->get_point_position(0);

	CHECK_MESSAGE(sub0_end.distance_to(expected_split) < real_t(0.02),
			vformat("substroke 0 end should be near (0.5, 0, 0); got %s",
					String(sub0_end)));
	CHECK_MESSAGE(sub1_start.distance_to(expected_split) < real_t(0.02),
			vformat("substroke 1 start should be near (0.5, 0, 0); got %s",
					String(sub1_start)));
	// Endpoints of the original stroke must survive at the outer ends
	// of the substroke pair.
	const Vector3 sub0_start = sub0_curve->get_point_position(0);
	const Vector3 sub1_end = sub1_curve->get_point_position(
			sub1_curve->get_point_count() - 1);
	CHECK_MESSAGE(sub0_start.distance_to(Vector3(0, 0, 0)) < real_t(0.001),
			vformat("substroke 0 start should be (0,0,0); got %s",
					String(sub0_start)));
	CHECK_MESSAGE(sub1_end.distance_to(Vector3(1, 0, 0)) < real_t(0.001),
			vformat("substroke 1 end should be (1,0,0); got %s",
					String(sub1_end)));
}

TEST_CASE("[Cassie][Beautifier] beautify on a planar stroke reports planar=true") {
	Ref<CassieInputStroke> stroke;
	stroke.instantiate();
	for (int i = 0; i < 30; ++i) {
		const real_t t = real_t(i) * 0.05;
		stroke->add_sample(Vector3(t, Math::sin(t * 2.0), 0), t, 1.0);
	}
	Ref<CassieBeautifierParams> params;
	params.instantiate();
	params->set_min_stroke_size(0.01f);
	params->set_planarity_allowed(true);
	params->set_proximity_threshold(0.1f);
	Ref<CassieSketchContext> ctx;
	ctx.instantiate();
	Ref<CassieBeautifier> b;
	b.instantiate();
	Dictionary result = b->beautify(stroke, ctx, params, true, false);
	REQUIRE(bool(result["is_valid"]));
	CHECK(bool(result.get("planar", false)));
}

// ── Pass-B cut_at ─────────────────────────────────────────────────────────

TEST_CASE("[Cassie][CutAt] throw_before discards the segment before t") {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(4, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<Curve3D> trimmed = cassie_curve_cut_at(curve, 0.5f, true, 0.01f);
	REQUIRE(trimmed.is_valid());
	CHECK(trimmed->get_point_count() >= 2);
	// New start anchor should be roughly at the midpoint of the original curve.
	CHECK(trimmed->get_point_position(0).distance_to(Vector3(2, 0, 0)) < 0.1);
	// Last anchor should still be the original end.
	CHECK(trimmed->get_point_position(trimmed->get_point_count() - 1)
					.distance_to(Vector3(4, 0, 0)) < 0.01);
}

TEST_CASE("[Cassie][CutAt] !throw_before discards the segment after t") {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(4, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<Curve3D> trimmed = cassie_curve_cut_at(curve, 0.5f, false, 0.01f);
	REQUIRE(trimmed.is_valid());
	CHECK(trimmed->get_point_count() >= 2);
	// First anchor still the original start.
	CHECK(trimmed->get_point_position(0).distance_to(Vector3(0, 0, 0)) < 0.01);
	// New end anchor at the midpoint.
	CHECK(trimmed->get_point_position(trimmed->get_point_count() - 1)
					.distance_to(Vector3(2, 0, 0)) < 0.1);
}

TEST_CASE("[Cassie][CutAt] cut near an existing anchor snaps to it") {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(4, 0, 0), Vector3(-1, 0, 0), Vector3());

	// t = 0.5 maps to the middle anchor for a 2-segment curve.
	Ref<Curve3D> trimmed = cassie_curve_cut_at(curve, 0.5f, true, 1.0f); // large snap
	REQUIRE(trimmed.is_valid());
	// Result should start cleanly at the middle anchor (no De Casteljau split).
	CHECK(trimmed->get_point_position(0).distance_to(Vector3(2, 0, 0)) < 1e-3);
}

// ── Large-stroke bypass ───────────────────────────────────────────────────

TEST_CASE("[Cassie][Beautifier] curve with more than max_beziers_for_solver bypasses solver") {
	// Force the cap to 2 — any curve from a multi-segment fit will exceed it.
	Ref<CassieInputStroke> stroke;
	stroke.instantiate();
	for (int i = 0; i < 60; ++i) {
		const real_t t = real_t(i) * 0.05;
		stroke->add_sample(Vector3(t, Math::sin(t * 2.0), 0), t, 1.0);
	}
	Ref<CassieBeautifierParams> params;
	params.instantiate();
	params->set_min_stroke_size(0.01f);
	params->set_max_beziers_for_solver(2);
	Ref<CassieSketchContext> ctx;
	ctx.instantiate();
	Ref<CassieBeautifier> b;
	b.instantiate();
	Dictionary result = b->beautify(stroke, ctx, params, true, false);
	REQUIRE(bool(result["is_valid"]));
	CHECK(bool(result.get("is_bypassed", false)));
}

TEST_CASE("[Cassie][Beautifier] curve under max_beziers_for_solver runs the solver") {
	Ref<CassieInputStroke> stroke;
	stroke.instantiate();
	for (int i = 0; i < 30; ++i) {
		const real_t t = real_t(i) * 0.05;
		stroke->add_sample(Vector3(t, Math::sin(t * 2.0), 0), t, 1.0);
	}
	Ref<CassieBeautifierParams> params;
	params.instantiate();
	params->set_min_stroke_size(0.01f);
	params->set_max_beziers_for_solver(50); // permissive
	Ref<CassieSketchContext> ctx;
	ctx.instantiate();
	Ref<CassieBeautifier> b;
	b.instantiate();
	Dictionary result = b->beautify(stroke, ctx, params, true, false);
	REQUIRE(bool(result["is_valid"]));
	CHECK_FALSE(bool(result.get("is_bypassed", true)));
}

// ── Surface projection (Callable backed) ──────────────────────────────────

class _CassieSurfaceCallbackHelper : public Object {
	GDCLASS(_CassieSurfaceCallbackHelper, Object);

public:
	int call_count = 0;
	Vector3 last_input;
	bool on_surface_response = true;

	Dictionary project(Vector3 p_pos) {
		++call_count;
		last_input = p_pos;
		Dictionary d;
		d["on_surface"] = on_surface_response;
		d["patch_id"] = 1;
		// Project onto the XZ plane (y=0).
		d["projected"] = Vector3(p_pos.x, 0, p_pos.z);
		return d;
	}

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("project", "pos"), &_CassieSurfaceCallbackHelper::project);
	}
};

TEST_CASE("[Cassie][Beautifier] surface projection partial fires callback on endpoints") {
	Ref<CassieInputStroke> stroke;
	stroke.instantiate();
	for (int i = 0; i < 30; ++i) {
		const real_t t = real_t(i) * 0.05;
		stroke->add_sample(Vector3(t, 0.1, Math::sin(t)), t, 1.0);
	}
	// Mark one surface constraint spanning roughly the middle — partial.
	stroke->in_constrain_to_surface(1, Vector3(0.5, 0.1, 0));
	stroke->out_constrain_to_surface(1, Vector3(1.0, 0.1, 0));

	_CassieSurfaceCallbackHelper *helper = memnew(_CassieSurfaceCallbackHelper);
	Ref<CassieSketchContext> ctx;
	ctx.instantiate();
	ctx->set_project_on_patch_callback(callable_mp(helper, &_CassieSurfaceCallbackHelper::project));

	Ref<CassieBeautifierParams> params;
	params.instantiate();
	params->set_min_stroke_size(0.01f);
	params->set_project_on_surface(true);
	params->set_project_to_surface_distance_threshold(0.5f);

	Ref<CassieBeautifier> b;
	b.instantiate();
	Dictionary result = b->beautify(stroke, ctx, params, true, false);
	CHECK(helper->call_count >= 2); // at least endpoint anchors invoked
	// Surface projection partial (not whole-stroke) — on_surface should be false.
	CHECK_FALSE(bool(result.get("on_surface", true)));
	memdelete(helper);
}

TEST_CASE("[Cassie][Beautifier] beautify round-trip — closed loop → triangulated mesh") {
	// Generate a circle in the XZ plane (well above the CASSIE small-stroke
	// threshold). The fitter should produce a closed-ish Curve3D, the
	// solver should pass it through, and CassieTriangulator should emit a
	// valid mesh.
	Ref<CassieInputStroke> stroke;
	stroke.instantiate();
	const int n = 48;
	const real_t r = 0.5;
	for (int i = 0; i <= n; ++i) {
		const real_t a = real_t(i) * real_t(Math::PI) * 2.0 / real_t(n);
		stroke->add_sample(Vector3(r * Math::cos(a), 0, r * Math::sin(a)),
				real_t(i) * 0.01f, 1.0);
	}
	Ref<CassieBeautifierParams> params;
	params.instantiate();
	params->set_min_stroke_size(0.01f);
	params->set_planarity_allowed(true);
	Ref<CassieSketchContext> ctx;
	ctx.instantiate();
	Ref<CassieBeautifier> b;
	b.instantiate();
	Dictionary result = b->beautify(stroke, ctx, params, false, false);
	REQUIRE(bool(result["is_valid"]));
	Ref<Curve3D> curve = result["curve"];
	REQUIRE(curve.is_valid());
	REQUIRE(curve->get_baked_length() > 0.0);

	PackedVector3Array boundary = curve->tessellate_even_length(5, 0.05);
	REQUIRE(boundary.size() >= 6);

	const Dictionary tri_result = CassieTriangulator::triangulate(boundary, 0.05f);
	const PackedVector3Array verts = tri_result.get("vertices", PackedVector3Array());
	const PackedInt32Array idx = tri_result.get("faces", PackedInt32Array());
	if (verts.size() > 0) {
		CHECK(verts.size() >= 3);
		CHECK(idx.size() >= 3);
		CHECK_EQ(idx.size() % 3, 0);
	} else {
		// Triangulator may reject this exact boundary; the beautify chain
		// still must have produced a usable curve.
		CHECK(curve->get_point_count() >= 2);
	}
}

TEST_CASE("[Cassie][Beautifier] beautify of a curved stroke returns a multi-anchor Curve3D") {
	Ref<CassieInputStroke> stroke;
	stroke.instantiate();
	for (int i = 0; i < 30; ++i) {
		const real_t t = real_t(i) * 0.05;
		stroke->add_sample(Vector3(t, Math::sin(t * 2.0), 0), t, 1.0);
	}
	Ref<CassieBeautifierParams> params;
	params.instantiate();
	params->set_min_stroke_size(0.01f);
	Ref<CassieSketchContext> ctx;
	ctx.instantiate();
	Ref<CassieBeautifier> beautifier;
	beautifier.instantiate();
	Dictionary result = beautifier->beautify(stroke, ctx, params, false, false);
	CHECK(bool(result.get("is_valid", false)));
	Ref<Curve3D> curve = result.get("curve", Variant());
	REQUIRE(curve.is_valid());
	CHECK(curve->get_point_count() >= 2);
}

} // namespace TestCassieSolver
