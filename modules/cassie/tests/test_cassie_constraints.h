/**************************************************************************/
/*  test_cassie_constraints.h                                             */
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

#include "../src/constraints/cassie_constraint.h"
#include "../src/constraints/cassie_intersection_constraint.h"
#include "../src/constraints/cassie_intersection_finder.h"
#include "../src/constraints/cassie_mirror_plane_constraint.h"
#include "../src/constraints/cassie_surface_constraint.h"
#include "../src/sketch/cassie_final_stroke.h"
#include "../src/sketch/cassie_input_stroke.h"
#include "test_cassie_curves.h" // reuses TestCassieCurves::make_unit_line_curve()

#include "core/math/plane.h"
#include "core/object/ref_counted.h"
#include "core/variant/typed_array.h"
#include "scene/resources/curve.h"
#include "tests/test_macros.h"

namespace TestCassieConstraints {

// ── CassieConstraint base ─────────────────────────────────────────────────

TEST_CASE("[Cassie][Constraint] project_on populates new curve data") {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<CassieConstraint> c;
	c.instantiate();
	c->set_position(Vector3(1, 0.5, 0));
	c->project_on(curve);

	CHECK(c->get_new_curve_position().distance_to(Vector3(1, 0, 0)) < 0.1);
	CHECK(c->get_new_curve_tangent().length() > 0.0);
}

TEST_CASE("[Cassie][Constraint] project_on_anchor snaps to the requested anchor") {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3(1, 0, 0));
	curve->add_point(Vector3(4, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<CassieConstraint> c;
	c.instantiate();
	c->project_on_anchor(curve, 1);
	CHECK_EQ(c->get_new_curve_position(), Vector3(2, 0, 0));
}

// ── CassieIntersectionConstraint ──────────────────────────────────────────

TEST_CASE("[Cassie][Intersection] setters / getters round-trip") {
	Ref<CassieIntersectionConstraint> ic;
	ic.instantiate();
	ic->set_position(Vector3(1, 2, 3));
	ic->set_old_curve_position(Vector3(1, 2, 0));
	ic->set_old_curve_tangent(Vector3(0, 0, 1));
	ic->set_old_curve_offset(0.5);
	ic->set_is_at_node(true);

	CHECK_EQ(ic->get_position(), Vector3(1, 2, 3));
	CHECK_EQ(ic->get_old_curve_position(), Vector3(1, 2, 0));
	CHECK_EQ(ic->get_old_curve_tangent(), Vector3(0, 0, 1));
	CHECK(Math::is_equal_approx(ic->get_old_curve_offset(), real_t(0.5)));
	CHECK(ic->get_is_at_node());
}

// ── CassieMirrorPlaneConstraint ───────────────────────────────────────────

TEST_CASE("[Cassie][Mirror] plane normal round-trip") {
	Ref<CassieMirrorPlaneConstraint> mc;
	mc.instantiate();
	mc->set_position(Vector3(0, 0, 0));
	mc->set_plane_normal(Vector3(0, 1, 0));
	CHECK_EQ(mc->get_plane_normal(), Vector3(0, 1, 0));
}

// ── CassieSurfaceConstraint ───────────────────────────────────────────────

TEST_CASE("[Cassie][SurfaceConstraint] leave records exit position") {
	Ref<CassieSurfaceConstraint> sc;
	sc.instantiate();
	sc->set_patch_id(7);
	sc->set_start_position(Vector3(1, 0, 0));
	CHECK_FALSE(sc->has_left_mid_stroke());

	sc->leave(Vector3(2, 0, 0));
	CHECK(sc->has_left_mid_stroke());
	CHECK_EQ(sc->get_end_position(), Vector3(2, 0, 0));
}

// ── CassieFinalStroke ─────────────────────────────────────────────────────

TEST_CASE("[Cassie][FinalStroke] get_constraint without curve fails loudly") {
	Ref<CassieFinalStroke> fs;
	fs.instantiate();
	ERR_PRINT_OFF;
	Ref<CassieIntersectionConstraint> c = fs->get_constraint(Vector3(0, 0, 0), 0.1f);
	ERR_PRINT_ON;
	// Method returns an empty constraint resource on failure (rather than null)
	// to keep the GDScript path simple; the ERR_FAIL_COND_V_MSG fires above.
	REQUIRE(c.is_valid());
}

TEST_CASE("[Cassie][FinalStroke] get_constraint returns position on the curve") {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	curve->add_point(Vector3(2, 0, 0), Vector3(-1, 0, 0), Vector3());

	Ref<CassieFinalStroke> fs;
	fs.instantiate();
	fs->set_curve(curve);

	Ref<CassieIntersectionConstraint> c = fs->get_constraint(Vector3(1, 1, 0), 0.05f);
	REQUIRE(c.is_valid());
	CHECK(c->get_position().distance_to(Vector3(1, 0, 0)) < 0.1);
}

// ── InputStroke priority-dedup ────────────────────────────────────────────

TEST_CASE("[Cassie][Dedup] curve/curve intersection blocks weaker candidate at same spot") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	const real_t prox = 0.1;

	Ref<CassieIntersectionConstraint> inter;
	inter.instantiate();
	inter->set_position(Vector3(0, 0, 0));
	inter->set_is_at_node(false);
	s->add_constraint(inter, prox);

	Ref<CassieMirrorPlaneConstraint> mirror;
	mirror.instantiate();
	mirror->set_position(Vector3(0.01, 0, 0));
	s->add_constraint(mirror, prox);

	// The intersection should win; the mirror constraint at nearly the
	// same position must be dropped.
	CHECK_EQ(s->get_constraint_count(), 1);
}

TEST_CASE("[Cassie][Dedup] at-node intersection beats off-node intersection nearby") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	const real_t prox = 0.1;

	Ref<CassieIntersectionConstraint> at_node;
	at_node.instantiate();
	at_node->set_position(Vector3(0, 0, 0));
	at_node->set_is_at_node(true);
	s->add_constraint(at_node, prox);

	Ref<CassieIntersectionConstraint> off_node;
	off_node.instantiate();
	off_node->set_position(Vector3(0.05, 0, 0));
	off_node->set_is_at_node(false);
	s->add_constraint(off_node, prox);

	CHECK_EQ(s->get_constraint_count(), 1);
}

TEST_CASE("[Cassie][Dedup] off-node intersection replaces older non-intersection candidate") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	const real_t prox = 0.1;

	Ref<CassieMirrorPlaneConstraint> mirror;
	mirror.instantiate();
	mirror->set_position(Vector3(0, 0, 0));
	s->add_constraint(mirror, prox);

	Ref<CassieIntersectionConstraint> off_node;
	off_node.instantiate();
	off_node->set_position(Vector3(0.05, 0, 0));
	s->add_constraint(off_node, prox);

	REQUIRE(s->get_constraint_count() == 1);
	Ref<CassieIntersectionConstraint> only = s->get_constraints()[0];
	CHECK(only.is_valid());
}

TEST_CASE("[Cassie][Dedup] consecutive mirror constraints within 2x threshold collapse") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	const real_t prox = 0.1;

	Ref<CassieMirrorPlaneConstraint> m1;
	m1.instantiate();
	m1->set_position(Vector3(0, 0, 0));
	s->add_constraint(m1, prox);

	Ref<CassieMirrorPlaneConstraint> m2;
	m2.instantiate();
	m2->set_position(Vector3(0.15, 0, 0));
	s->add_constraint(m2, prox);

	CHECK_EQ(s->get_constraint_count(), 1);
}

TEST_CASE("[Cassie][Dedup] far-apart constraints are both kept") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	const real_t prox = 0.1;

	Ref<CassieMirrorPlaneConstraint> m1;
	m1.instantiate();
	m1->set_position(Vector3(0, 0, 0));
	s->add_constraint(m1, prox);

	Ref<CassieMirrorPlaneConstraint> m2;
	m2.instantiate();
	m2->set_position(Vector3(2, 0, 0));
	s->add_constraint(m2, prox);

	CHECK_EQ(s->get_constraint_count(), 2);
}

// ── Surface enter / leave tracking ────────────────────────────────────────

TEST_CASE("[Cassie][Surface] in_constrain_to_surface dedups consecutive same-patch entries") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	s->in_constrain_to_surface(3, Vector3(0, 0, 0));
	s->in_constrain_to_surface(3, Vector3(0.1, 0, 0));
	CHECK_EQ(s->get_surface_constraints().size(), 1);
}

TEST_CASE("[Cassie][Surface] different patches get separate constraints") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	s->in_constrain_to_surface(3, Vector3(0, 0, 0));
	s->in_constrain_to_surface(4, Vector3(0.5, 0, 0));
	CHECK_EQ(s->get_surface_constraints().size(), 2);
}

TEST_CASE("[Cassie][Surface] out_constrain marks the active constraint as left") {
	Ref<CassieInputStroke> s;
	s.instantiate();
	s->in_constrain_to_surface(3, Vector3(0, 0, 0));
	s->out_constrain_to_surface(3, Vector3(1, 0, 0));
	REQUIRE(s->get_surface_constraints().size() == 1);
	Ref<CassieSurfaceConstraint> sc = s->get_surface_constraints()[0];
	CHECK(sc->has_left_mid_stroke());
	CHECK_EQ(sc->get_end_position(), Vector3(1, 0, 0));
}

// ── IntersectionFinder ────────────────────────────────────────────────────

TEST_CASE("[Cassie][Finder] empty existing list returns nothing") {
	Ref<Curve3D> new_curve;
	new_curve.instantiate();
	new_curve->add_point(Vector3(0, 0, 0), Vector3(), Vector3());
	new_curve->add_point(Vector3(1, 0, 0), Vector3(), Vector3());

	TypedArray<CassieFinalStroke> existing;
	TypedArray<CassieIntersectionConstraint> out =
			cassie_find_intersections(new_curve, existing, 0.1f, 0.05f);
	CHECK_EQ(out.size(), 0);
}

TEST_CASE("[Cassie][Finder] crossing strokes produce one constraint") {
	Ref<Curve3D> existing_curve;
	existing_curve.instantiate();
	existing_curve->add_point(Vector3(-1, 0, 0), Vector3(), Vector3());
	existing_curve->add_point(Vector3(1, 0, 0), Vector3(), Vector3());
	Ref<CassieFinalStroke> existing_stroke;
	existing_stroke.instantiate();
	existing_stroke->set_curve(existing_curve);

	TypedArray<CassieFinalStroke> existing;
	existing.push_back(existing_stroke);

	Ref<Curve3D> new_curve;
	new_curve.instantiate();
	new_curve->add_point(Vector3(0, -1, 0), Vector3(), Vector3());
	new_curve->add_point(Vector3(0, 1, 0), Vector3(), Vector3());

	TypedArray<CassieIntersectionConstraint> out =
			cassie_find_intersections(new_curve, existing, 0.1f, 0.05f);
	REQUIRE(out.size() == 1);
	Ref<CassieIntersectionConstraint> c = out[0];
	REQUIRE(c.is_valid());
	CHECK(c->get_position().distance_to(Vector3(0, 0, 0)) < 0.1);
}

TEST_CASE("[Cassie][Finder] mirror plane crossing detected") {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(-1, 0, 0), Vector3(), Vector3());
	curve->add_point(Vector3(1, 0, 0), Vector3(), Vector3());

	Plane mirror(Vector3(1, 0, 0), 0.0);
	Ref<CassieMirrorPlaneConstraint> c =
			cassie_detect_mirror_plane_intersection(curve, mirror, 0.05f);
	REQUIRE(c.is_valid());
	CHECK(c->get_position().distance_to(Vector3(0, 0, 0)) < 0.05);
}

TEST_CASE("[Cassie][Finder] mirror plane not crossed returns null") {
	Ref<Curve3D> curve;
	curve.instantiate();
	curve->add_point(Vector3(1, 0, 0), Vector3(), Vector3());
	curve->add_point(Vector3(2, 0, 0), Vector3(), Vector3());

	Plane mirror(Vector3(1, 0, 0), 0.0);
	Ref<CassieMirrorPlaneConstraint> c =
			cassie_detect_mirror_plane_intersection(curve, mirror, 0.05f);
	CHECK_FALSE(c.is_valid());
}

// ── cassie_split_stroke_at_constraints (Tier 4 slice) ─────────────────────

TEST_CASE("[Cassie][Finder] split_stroke: 2 constraints produce 3 substrokes") {
	// Straight unit line with 1/3-handles so chord-uniform t maps to position
	// linearly. Baked length = 1.0, so old_curve_offset == t directly.
	Ref<Curve3D> curve = TestCassieCurves::make_unit_line_curve();

	Ref<CassieFinalStroke> stroke;
	stroke.instantiate();
	stroke->set_curve(curve, false);

	TypedArray<CassieIntersectionConstraint> constraints;
	Ref<CassieIntersectionConstraint> c1;
	c1.instantiate();
	c1->set_old_curve_offset(real_t(1.0 / 3.0));
	constraints.push_back(c1);
	Ref<CassieIntersectionConstraint> c2;
	c2.instantiate();
	c2->set_old_curve_offset(real_t(2.0 / 3.0));
	constraints.push_back(c2);

	TypedArray<CassieFinalStroke> pieces =
			cassie_split_stroke_at_constraints(stroke, constraints, 1e-6f);
	REQUIRE(pieces.size() == 3);
	for (int i = 0; i < 3; ++i) {
		Ref<CassieFinalStroke> p = pieces[i];
		REQUIRE(p.is_valid());
		REQUIRE(p->get_curve().is_valid());
		REQUIRE(p->get_curve()->get_point_count() >= 2);
	}
	// First substroke ends at the first cut (1/3, 0, 0); last substroke
	// starts at the second cut (2/3, 0, 0). The cut-point invariants are
	// what Tier 4 graph wiring will rely on to register intersection knots.
	Ref<CassieFinalStroke> first = pieces[0];
	Ref<CassieFinalStroke> last = pieces[2];
	const int first_end = first->get_curve()->get_point_count() - 1;
	const Vector3 first_cut = first->get_curve()->get_point_position(first_end);
	const Vector3 last_start = last->get_curve()->get_point_position(0);
	CHECK(first_cut.distance_to(Vector3(real_t(1.0 / 3.0), 0, 0)) < 1e-4);
	CHECK(last_start.distance_to(Vector3(real_t(2.0 / 3.0), 0, 0)) < 1e-4);
}

TEST_CASE("[Cassie][Finder] split_stroke: empty constraints yields one piece wrapping the same curve") {
	Ref<Curve3D> curve = TestCassieCurves::make_unit_line_curve();
	Ref<CassieFinalStroke> stroke;
	stroke.instantiate();
	stroke->set_curve(curve, false);

	TypedArray<CassieIntersectionConstraint> empty;
	TypedArray<CassieFinalStroke> pieces =
			cassie_split_stroke_at_constraints(stroke, empty, 1e-6f);
	REQUIRE(pieces.size() == 1);
	Ref<CassieFinalStroke> p = pieces[0];
	REQUIRE(p.is_valid());
	// Substroke wraps the input curve unchanged; the wrapper itself is fresh
	// (Tier 4 graph layer owns stroke identity).
	CHECK(p->get_curve() == curve);
}

} // namespace TestCassieConstraints
