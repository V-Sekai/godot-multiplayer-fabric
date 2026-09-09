/**************************************************************************/
/*  test_cassie_curvenet.h                                                */
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

#include "../src/sketch/cassie_curvenet.h"
#include "../src/sketch/cassie_curvenet_knot.h"
#include "../src/sketch/cassie_final_stroke.h"

#include "core/math/transform_3d.h"
#include "core/math/vector3.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"
#include "core/variant/variant.h"
#include "scene/resources/curve.h"
#include "tests/test_macros.h"

namespace TestCassieCurvenet {

TEST_CASE("[Cassie][Curvenet] empty curvenet has no curves or knots") {
	Ref<CassieCurvenet> cn;
	cn.instantiate();
	CHECK_EQ(cn->get_curve_count(), 0);
	CHECK_EQ(cn->get_knot_count(), 0);
	CHECK_EQ(int(cn->get_mode()), int(CassieCurvenet::MODE_EDIT));
}

TEST_CASE("[Cassie][Curvenet] mode toggle round-trips") {
	Ref<CassieCurvenet> cn;
	cn.instantiate();
	cn->set_mode(CassieCurvenet::MODE_POSE);
	CHECK_EQ(int(cn->get_mode()), int(CassieCurvenet::MODE_POSE));
	cn->set_mode(CassieCurvenet::MODE_EDIT);
	CHECK_EQ(int(cn->get_mode()), int(CassieCurvenet::MODE_EDIT));
}

TEST_CASE("[Cassie][Curvenet] build_from_graph classifies intersections by valence") {
	// Three curves meeting at one node = degree-3 intersection. The remaining
	// endpoints are degree-1 (not intersections).
	Ref<Curve3D> c0;
	c0.instantiate();
	c0->add_point(Vector3(0, 0, 0), Vector3(), Vector3(1, 0, 0));
	c0->add_point(Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3());
	Ref<Curve3D> c1;
	c1.instantiate();
	c1->add_point(Vector3(0, 0, 0), Vector3(), Vector3(0, 1, 0));
	c1->add_point(Vector3(0, 1, 0), Vector3(0, -1, 0), Vector3());
	Ref<Curve3D> c2;
	c2.instantiate();
	c2->add_point(Vector3(0, 0, 0), Vector3(), Vector3(0, 0, 1));
	c2->add_point(Vector3(0, 0, 1), Vector3(0, 0, -1), Vector3());

	Ref<CassieFinalStroke> s0;
	s0.instantiate();
	s0->set_curve(c0);
	Ref<CassieFinalStroke> s1;
	s1.instantiate();
	s1->set_curve(c1);
	Ref<CassieFinalStroke> s2;
	s2.instantiate();
	s2->set_curve(c2);
	TypedArray<CassieFinalStroke> strokes;
	strokes.push_back(s0);
	strokes.push_back(s1);
	strokes.push_back(s2);

	Array nodes;
	{
		Dictionary node;
		node["id"] = 0;
		node["position"] = Vector3(0, 0, 0);
		PackedInt32Array incident;
		incident.push_back(0);
		incident.push_back(1);
		incident.push_back(2);
		node["incident_curve_ids"] = incident;
		nodes.push_back(node);
	}
	for (int i = 0; i < 3; ++i) {
		Dictionary node;
		node["id"] = i + 1;
		node["position"] = i == 0 ? Vector3(1, 0, 0)
								  : (i == 1 ? Vector3(0, 1, 0) : Vector3(0, 0, 1));
		PackedInt32Array incident;
		incident.push_back(i);
		node["incident_curve_ids"] = incident;
		nodes.push_back(node);
	}

	Dictionary graph;
	graph["curves"] = strokes;
	graph["nodes"] = nodes;

	Ref<CassieCurvenet> cn;
	cn.instantiate();
	cn->build_from_graph(graph);
	REQUIRE(cn->get_knot_count() == 4);
	Ref<CassieCurvenetKnot> center = cn->get_knots()[0];
	REQUIRE(center.is_valid());
	CHECK(center->get_is_intersection());
	CHECK_EQ(center->get_projection_pose_tangents().size(), 3);
	for (int i = 1; i <= 3; ++i) {
		Ref<CassieCurvenetKnot> endpoint = cn->get_knots()[i];
		REQUIRE(endpoint.is_valid());
		CHECK_FALSE(endpoint->get_is_intersection());
	}
}

TEST_CASE("[Cassie][Curvenet] CassieCurvenetKnot transform fields round-trip") {
	Ref<CassieCurvenetKnot> knot;
	knot.instantiate();
	knot->set_graph_node_id(42);
	const Transform3D t = Transform3D().translated(Vector3(1, 2, 3));
	knot->set_projection_pose_transform(t);
	knot->set_rest_pose_transform(t);
	knot->set_is_intersection(true);
	knot->set_needs_setup(true);

	CHECK_EQ(knot->get_graph_node_id(), 42);
	CHECK_EQ(knot->get_projection_pose_transform().origin, Vector3(1, 2, 3));
	CHECK_EQ(knot->get_rest_pose_transform().origin, Vector3(1, 2, 3));
	CHECK(knot->get_is_intersection());
	CHECK(knot->get_needs_setup());

	// solve_world_transform returns the rest pose for Step 1.2.
	CHECK_EQ(knot->solve_world_transform().origin, Vector3(1, 2, 3));
}

TEST_CASE("[Cassie][Curvenet] compute_orientations is callable on an empty curvenet") {
	Ref<CassieCurvenet> cn;
	cn.instantiate();
	cn->compute_orientations(); // must not crash
	CHECK_EQ(cn->get_knot_count(), 0);
}

// ── Step 1.3 (ENG-45) numerically-pinned tests ────────────────────────────

static PackedVector3Array _make_tangents(const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c) {
	PackedVector3Array t;
	t.push_back(p_a);
	t.push_back(p_b);
	t.push_back(p_c);
	return t;
}

TEST_CASE("[Cassie][Curvenet] intersection knot — identity case") {
	const PackedVector3Array projection = _make_tangents(
			Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1));
	const Basis R = CassieCurvenet::wahba_align(projection, projection);
	const Quaternion q = R.get_rotation_quaternion();
	CHECK(Math::abs(q.x) < 1e-6);
	CHECK(Math::abs(q.y) < 1e-6);
	CHECK(Math::abs(q.z) < 1e-6);
	CHECK(Math::abs(Math::abs(q.w) - real_t(1.0)) < 1e-6);
}

TEST_CASE("[Cassie][Curvenet] intersection knot — 90 around Y") {
	const PackedVector3Array projection = _make_tangents(
			Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1));
	const PackedVector3Array rest = _make_tangents(
			Vector3(0, 0, -1), Vector3(0, 1, 0), Vector3(1, 0, 0));
	const Basis R = CassieCurvenet::wahba_align(projection, rest);
	const Quaternion q = R.get_rotation_quaternion();
	const real_t s = real_t(0.7071067811865476);
	CHECK(Math::abs(q.x - real_t(0.0)) < 1e-6);
	CHECK(Math::abs(Math::abs(q.y) - s) < 1e-6);
	CHECK(Math::abs(q.z - real_t(0.0)) < 1e-6);
	CHECK(Math::abs(Math::abs(q.w) - s) < 1e-6);
}

TEST_CASE("[Cassie][Curvenet] intersection knot — arbitrary rotation matches reference") {
	// Build a known rotation R_in (35 deg around (1,2,3) normalized), apply
	// to 4 projection tangents, run wahba_align, recover within 1e-5 rad.
	const Vector3 axis = Vector3(1, 2, 3).normalized();
	const real_t angle = real_t(0.35); // ≈ 20°
	const Basis R_in(Quaternion(axis, angle));
	PackedVector3Array projection;
	projection.push_back(Vector3(1, 0, 0));
	projection.push_back(Vector3(0, 1, 0));
	projection.push_back(Vector3(0, 0, 1));
	projection.push_back(Vector3(1, 1, 1).normalized());
	PackedVector3Array rest;
	for (int i = 0; i < projection.size(); ++i) {
		rest.push_back(R_in.xform(projection[i]));
	}
	const Basis R_out = CassieCurvenet::wahba_align(projection, rest);
	const Quaternion q_in = R_in.get_rotation_quaternion();
	const Quaternion q_out = R_out.get_rotation_quaternion();
	const real_t dot = Math::abs(q_in.x * q_out.x + q_in.y * q_out.y +
			q_in.z * q_out.z + q_in.w * q_out.w);
	const real_t angle_diff = real_t(2.0) * Math::acos(MIN(real_t(1.0), dot));
	CHECK_MESSAGE(angle_diff < 1e-5,
			vformat("Wahba angular error %f rad exceeds 1e-5", double(angle_diff)));
}

TEST_CASE("[Cassie][Curvenet] intersection knot — 2-tangent input falls through to identity") {
	PackedVector3Array projection;
	projection.push_back(Vector3(1, 0, 0));
	projection.push_back(Vector3(0, 1, 0));
	PackedVector3Array rest;
	rest.push_back(Vector3(0, 1, 0));
	rest.push_back(Vector3(-1, 0, 0));
	const Basis R = CassieCurvenet::wahba_align(projection, rest);
	const Quaternion q = R.get_rotation_quaternion();
	// 2-tangent fallback returns identity so the parallel-transport pass
	// can take over.
	CHECK(Math::abs(q.x) < 1e-6);
	CHECK(Math::abs(q.y) < 1e-6);
	CHECK(Math::abs(q.z) < 1e-6);
	CHECK(Math::abs(Math::abs(q.w) - real_t(1.0)) < 1e-6);
}

TEST_CASE("[Cassie][Curvenet] non-intersection knot — midpoint slerp") {
	// Straight curve from (0,0,0) to (1,0,0) with two segments and a
	// midpoint anchor at (0.5,0,0). Intersection at A (t=0) has R = identity;
	// intersection at B (t=1) has R = 90° around Y. Midpoint knot expects
	// slerp at t=0.5 → 45° around Y.
	Ref<Curve3D> c;
	c.instantiate();
	c->add_point(Vector3(0, 0, 0), Vector3(), Vector3(real_t(0.25), 0, 0));
	c->add_point(Vector3(real_t(0.5), 0, 0), Vector3(real_t(-0.25), 0, 0), Vector3(real_t(0.25), 0, 0));
	c->add_point(Vector3(1, 0, 0), Vector3(real_t(-0.25), 0, 0), Vector3());

	const real_t L = c->get_baked_length();
	const Basis R_a = Basis(); // identity at offset 0
	const Basis R_b = Basis(Quaternion(Vector3(0, 1, 0), Math::PI * real_t(0.5)));
	const real_t mid = L * real_t(0.5);
	const Basis transported_from_a = CassieCurvenet::parallel_transport_along(c, 0.0, R_a, mid);
	const Basis transported_from_b = CassieCurvenet::parallel_transport_along(c, L, R_b, mid);
	const Quaternion qa = transported_from_a.get_rotation_quaternion();
	const Quaternion qb = transported_from_b.get_rotation_quaternion();
	const Quaternion blended = qa.slerp(qb, real_t(0.5));
	// For a straight curve, parallel-transport preserves the source basis.
	// Blended quaternion at t=0.5 between identity and 90° Y rotation
	// → 45° around Y → (0, sin(pi/8), 0, cos(pi/8)).
	CHECK(Math::abs(blended.x) < 1e-5);
	CHECK(Math::abs(Math::abs(blended.y) - real_t(0.3826834323650898)) < 1e-5);
	CHECK(Math::abs(blended.z) < 1e-5);
	CHECK(Math::abs(Math::abs(blended.w) - real_t(0.9238795325112867)) < 1e-5);
}

TEST_CASE("[Cassie][Curvenet] non-intersection knot — 25 percent blend favors near intersection") {
	const Basis R_a = Basis();
	const Basis R_b = Basis(Quaternion(Vector3(0, 1, 0), Math::PI * real_t(0.5)));
	const Quaternion qa = R_a.get_rotation_quaternion();
	const Quaternion qb = R_b.get_rotation_quaternion();
	const Quaternion blended = qa.slerp(qb, real_t(0.25));
	// Half-angle of 90° × 0.25 = 22.5° → sin/cos of pi/16.
	CHECK(Math::abs(Math::abs(blended.y) - real_t(0.19509032201612825)) < 1e-5);
	CHECK(Math::abs(Math::abs(blended.w) - real_t(0.9807852804032304)) < 1e-5);
}

TEST_CASE("[Cassie][Curvenet] non-intersection knot — coincident with intersection returns R exactly") {
	Ref<Curve3D> c;
	c.instantiate();
	c->add_point(Vector3(0, 0, 0), Vector3(), Vector3(real_t(0.5), 0, 0));
	c->add_point(Vector3(1, 0, 0), Vector3(real_t(-0.5), 0, 0), Vector3());

	const Basis R_a = Basis(Quaternion(Vector3(0, 1, 0), Math::PI * real_t(0.5)));
	const Basis at_zero = CassieCurvenet::parallel_transport_along(c, 0.0, R_a, 0.0);
	const Quaternion q = at_zero.get_rotation_quaternion();
	const Quaternion q_a = R_a.get_rotation_quaternion();
	const real_t dot = Math::abs(q.x * q_a.x + q.y * q_a.y + q.z * q_a.z + q.w * q_a.w);
	// Even for bitwise-identical quaternions, the fp32 self-dot lands at
	// ~0.99999994 (components like sin(pi/4) square to just under 0.5), so a
	// 1e-9 bound is below single-precision epsilon and can never hold in
	// real_t=float builds. 1e-6 still asserts "same rotation" to fp32 limits.
	CHECK(Math::abs(dot - real_t(1.0)) < 1e-6);
}

TEST_CASE("[Cassie][Curvenet] non-intersection knot on curved segment uses Curve3D parallel transport") {
	// Curve with non-trivial parallel-transport frame at the midpoint.
	Ref<Curve3D> c;
	c.instantiate();
	c->add_point(Vector3(0, 0, 0), Vector3(), Vector3(0, real_t(0.5), 0));
	c->add_point(Vector3(1, 0, 0), Vector3(0, real_t(-0.5), 0), Vector3());

	const real_t L = c->get_baked_length();
	const Basis R_id = Basis();
	const Basis transported = CassieCurvenet::parallel_transport_along(c, 0.0, R_id, L * real_t(0.5));

	// Expected: the rotation that takes the Curve3D frame at offset 0 to
	// the frame at offset L/2, applied to identity.
	const Transform3D f0 = c->sample_baked_with_rotation(0.0, true, true);
	const Transform3D fm = c->sample_baked_with_rotation(L * real_t(0.5), true, true);
	const Basis expected = fm.basis * f0.basis.inverse() * R_id;
	const Quaternion qt = transported.get_rotation_quaternion();
	const Quaternion qe = expected.get_rotation_quaternion();
	const real_t dot = Math::abs(qt.x * qe.x + qt.y * qe.y + qt.z * qe.z + qt.w * qe.w);
	CHECK(Math::abs(dot - real_t(1.0)) < 1e-5);
}

TEST_CASE("[Cassie][Curvenet] isolated curve before any intersection forms — identity + needs_setup") {
	// Build a 2-knot curvenet with a single curve and no degree-3 nodes.
	Ref<Curve3D> c;
	c.instantiate();
	c->add_point(Vector3(0, 0, 0), Vector3(), Vector3(real_t(0.5), 0, 0));
	c->add_point(Vector3(1, 0, 0), Vector3(real_t(-0.5), 0, 0), Vector3());
	Ref<CassieFinalStroke> s;
	s.instantiate();
	s->set_curve(c);
	TypedArray<CassieFinalStroke> strokes;
	strokes.push_back(s);

	Array nodes;
	for (int i = 0; i < 2; ++i) {
		Dictionary node;
		node["id"] = i;
		node["position"] = i == 0 ? Vector3(0, 0, 0) : Vector3(1, 0, 0);
		PackedInt32Array incident;
		incident.push_back(0);
		node["incident_curve_ids"] = incident;
		nodes.push_back(node);
	}
	Dictionary graph;
	graph["curves"] = strokes;
	graph["nodes"] = nodes;

	Ref<CassieCurvenet> cn;
	cn.instantiate();
	cn->build_from_graph(graph);
	cn->compute_orientations();
	REQUIRE(cn->get_knot_count() == 2);
	for (int i = 0; i < 2; ++i) {
		Ref<CassieCurvenetKnot> k = cn->get_knots()[i];
		REQUIRE(k.is_valid());
		CHECK(k->get_needs_setup());
		const Basis b = k->get_rest_pose_transform().basis;
		const Quaternion q = b.get_rotation_quaternion();
		// Identity basis.
		CHECK(Math::abs(Math::abs(q.w) - real_t(1.0)) < 1e-6);
	}
}

TEST_CASE("[Cassie][Curvenet] knot translation equals projected position on patch") {
	// Build a sphere mesh (small grid), patch it, build a knot at a known
	// off-surface position, run update_rest_pose. The translation must
	// equal the patch projection.
	Vector<Vector3> verts;
	verts.push_back(Vector3(0, 0, 0));
	verts.push_back(Vector3(1, 0, 0));
	verts.push_back(Vector3(0, 0, 1));
	Vector<int> idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	PackedVector3Array pv;
	pv.resize(3);
	for (int i = 0; i < 3; ++i) {
		pv.write[i] = verts[i];
	}
	PackedInt32Array pi;
	pi.resize(3);
	for (int i = 0; i < 3; ++i) {
		pi.write[i] = idx[i];
	}
	arrays[Mesh::ARRAY_VERTEX] = pv;
	arrays[Mesh::ARRAY_INDEX] = pi;
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	Ref<CassieSurfacePatch> patch;
	patch.instantiate();
	patch->set_mesh(mesh);

	Ref<CassieCurvenetKnot> knot;
	knot.instantiate();
	Transform3D t;
	t.origin = Vector3(real_t(1.0 / 3.0), 1, real_t(1.0 / 3.0));
	knot->set_rest_pose_transform(t);
	TypedArray<CassieCurvenetKnot> knots;
	knots.push_back(knot);
	Ref<CassieCurvenet> cn;
	cn.instantiate();
	cn->set_knots(knots);
	cn->update_rest_pose(patch);
	const Vector3 origin = knot->get_rest_pose_transform().origin;
	CHECK(origin.distance_to(Vector3(real_t(1.0 / 3.0), 0, real_t(1.0 / 3.0))) < 1e-4);
}

TEST_CASE("[Cassie][Curvenet] orientation is right-handed (det == +1)") {
	const Vector3 axis = Vector3(1, 2, 3).normalized();
	const Basis R_in(Quaternion(axis, real_t(0.4)));
	PackedVector3Array projection;
	projection.push_back(Vector3(1, 0, 0));
	projection.push_back(Vector3(0, 1, 0));
	projection.push_back(Vector3(0, 0, 1));
	PackedVector3Array rest;
	for (int i = 0; i < projection.size(); ++i) {
		rest.push_back(R_in.xform(projection[i]));
	}
	const Basis R_out = CassieCurvenet::wahba_align(projection, rest);
	CHECK(Math::abs(R_out.determinant() - real_t(1.0)) < 1e-6);
}

// ── Step 1.5 (ENG-47) — topology ops ─────────────────────────────────────

namespace _Topo {
// Build a minimal 3-knot, 2-curve fixture: knots at (0,0,0), (1,0,0), (2,0,0)
// joined by two straight-line curves. Knot 1 is the shared midpoint.
static Ref<CassieCurvenet> _make_three_knot_chain() {
	Ref<CassieCurvenet> cn;
	cn.instantiate();
	Dictionary g;

	Ref<Curve3D> c0;
	c0.instantiate();
	c0->add_point(Vector3(0, 0, 0), Vector3(), Vector3());
	c0->add_point(Vector3(1, 0, 0), Vector3(), Vector3());
	Ref<CassieFinalStroke> s0;
	s0.instantiate();
	s0->set_id(0);
	s0->set_curve(c0, false);

	Ref<Curve3D> c1;
	c1.instantiate();
	c1->add_point(Vector3(1, 0, 0), Vector3(), Vector3());
	c1->add_point(Vector3(2, 0, 0), Vector3(), Vector3());
	Ref<CassieFinalStroke> s1;
	s1.instantiate();
	s1->set_id(1);
	s1->set_curve(c1, false);

	TypedArray<CassieFinalStroke> curves;
	curves.push_back(s0);
	curves.push_back(s1);
	g["curves"] = curves;

	Array nodes;
	Dictionary n0;
	n0["id"] = 0;
	n0["position"] = Vector3(0, 0, 0);
	PackedInt32Array inc0;
	inc0.push_back(0);
	n0["incident_curve_ids"] = inc0;
	Dictionary n1;
	n1["id"] = 1;
	n1["position"] = Vector3(1, 0, 0);
	PackedInt32Array inc1;
	inc1.push_back(0);
	inc1.push_back(1);
	n1["incident_curve_ids"] = inc1;
	Dictionary n2;
	n2["id"] = 2;
	n2["position"] = Vector3(2, 0, 0);
	PackedInt32Array inc2;
	inc2.push_back(1);
	n2["incident_curve_ids"] = inc2;
	nodes.push_back(n0);
	nodes.push_back(n1);
	nodes.push_back(n2);
	g["nodes"] = nodes;
	cn->build_from_graph(g);
	return cn;
}

static Ref<CassieFinalStroke> _make_straight_curve(const Vector3 &p_a, const Vector3 &p_b, int p_id) {
	Ref<Curve3D> c;
	c.instantiate();
	c->add_point(p_a, Vector3(), Vector3());
	c->add_point(p_b, Vector3(), Vector3());
	Ref<CassieFinalStroke> s;
	s.instantiate();
	s->set_id(p_id);
	s->set_curve(c, false);
	return s;
}
} // namespace _Topo

TEST_CASE("[Cassie][Curvenet] set_knot_transform writes the rest pose transform") {
	Ref<CassieCurvenet> cn = _Topo::_make_three_knot_chain();
	REQUIRE(cn->get_knot_count() == 3);
	Transform3D t;
	t.origin = Vector3(7, 8, 9);
	CHECK(cn->set_knot_transform(1, t));
	Ref<CassieCurvenetKnot> k = cn->get_knots()[1];
	REQUIRE(k.is_valid());
	CHECK_EQ(k->get_rest_pose_transform().origin, Vector3(7, 8, 9));
	CHECK_FALSE(cn->set_knot_transform(99, t));
	CHECK_FALSE(cn->set_knot_transform(-1, t));
}

TEST_CASE("[Cassie][Curvenet] add_curve appends and updates adjacency") {
	Ref<CassieCurvenet> cn = _Topo::_make_three_knot_chain();
	REQUIRE(cn->get_curve_count() == 2);

	Ref<CassieFinalStroke> bridge = _Topo::_make_straight_curve(
			Vector3(0, 0, 0), Vector3(2, 0, 0), 99);
	const int new_idx = cn->add_curve(bridge, 0, 2);
	CHECK_EQ(new_idx, 2);
	CHECK_EQ(cn->get_curve_count(), 3);
	const Vector2i ep = cn->get_curve_endpoint_knots(new_idx);
	CHECK_EQ(ep.x, 0);
	CHECK_EQ(ep.y, 2);

	// Null curve must not modify state.
	const int rej = cn->add_curve(Ref<CassieFinalStroke>(), 0, 1);
	CHECK_EQ(rej, -1);
	CHECK_EQ(cn->get_curve_count(), 3);
}

TEST_CASE("[Cassie][Curvenet] delete_curve removes and shifts higher indices down") {
	Ref<CassieCurvenet> cn = _Topo::_make_three_knot_chain();
	REQUIRE(cn->get_curve_count() == 2);
	// curve 0 spans knots (0, 1); curve 1 spans (1, 2). Drop curve 0.
	CHECK(cn->delete_curve(0));
	CHECK_EQ(cn->get_curve_count(), 1);
	// What was curve 1 is now curve 0. Its endpoints should still link knot 1 → 2.
	const Vector2i ep = cn->get_curve_endpoint_knots(0);
	CHECK_EQ(ep.x, 1);
	CHECK_EQ(ep.y, 2);

	CHECK_FALSE(cn->delete_curve(99));
	CHECK_FALSE(cn->delete_curve(-1));
}

TEST_CASE("[Cassie][Curvenet] replace_curve swaps geometry but preserves adjacency") {
	Ref<CassieCurvenet> cn = _Topo::_make_three_knot_chain();
	const Vector2i before = cn->get_curve_endpoint_knots(0);
	Ref<CassieFinalStroke> replacement = _Topo::_make_straight_curve(
			Vector3(0, 0, 0), Vector3(1, 5, 0), 1000);
	CHECK(cn->replace_curve(0, replacement));
	const Vector2i after = cn->get_curve_endpoint_knots(0);
	CHECK_EQ(after.x, before.x);
	CHECK_EQ(after.y, before.y);
	Ref<CassieFinalStroke> got = cn->get_curves()[0];
	CHECK_EQ(got, replacement);
	CHECK_FALSE(cn->replace_curve(99, replacement));
	CHECK_FALSE(cn->replace_curve(0, Ref<CassieFinalStroke>()));
}

} // namespace TestCassieCurvenet
