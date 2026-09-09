/**************************************************************************/
/*  cassie_constraint_solver.h                                            */
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

// Public Tier-3 solver façade. Intentionally has NO Eigen in its
// signatures — Eigen is an implementation detail confined to the .cpp.

#include "../constraints/cassie_constraint.h"
#include "../constraints/cassie_intersection_constraint.h"
#include "../constraints/cassie_mirror_plane_constraint.h"

#include "core/io/resource.h"
#include "core/math/plane.h"
#include "core/math/vector3.h"
#include "core/object/ref_counted.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"
#include "scene/resources/curve.h"

// Solver parameter bundle. Maps to E:/cassie/Assets/Scripts/Create/Sketch/
// Beautify\ConstraintSolver.cs SolverParams.
class CassieSolverParams : public Resource {
	GDCLASS(CassieSolverParams, Resource);

	double mu_fidelity = 0.7;
	double w_p = 0.5;
	double w_t = 0.5;
	double proximity_threshold = 0.02;
	double angular_proximity_threshold = 0.5;
	double min_distance_between_anchors = 0.01;
	bool planarity_allowed = true;
	// ENG-54 — weight for the OnSurfaceEnergy soft block. Default 0.0
	// preserves the post-solve projection behavior at
	// cassie_beautifier.cpp:294-316. Positive values fold the projection
	// into the KKT/AVBD system; recommended range 0.1-0.3. Larger values
	// override planarity.
	double on_surface_weight = 0.0;
	// ENG-54 — projection callback used by OnSurfaceEnergy. Set by
	// CassieBeautifier from its context before calling
	// CassieConstraintSolver::solve(). Same Callable shape as
	// CassieSketchContext::project_on_patch_callback. Returns the
	// CassieSurfacePatch::project Dictionary.
	Callable project_on_patch_callback;
	// ENG-49 — when true, the KKT solve runs the Augmented Lagrangian
	// iteration (ALM) instead of the one-shot Eigen FullPivLU. ALM is
	// AVBD's outer loop — true per-vertex block descent doesn't pay off
	// at CASSIE's matrix sizes (<= 45x45) but the augmented-Lagrangian
	// stability win does.
	bool use_avbd = false;
	double avbd_rho = 10.0;
	int avbd_max_iter = 24;
	double avbd_tol = 1e-8;

protected:
	static void _bind_methods();

public:
	CassieSolverParams() = default;

	void set_mu_fidelity(double p_v) { mu_fidelity = p_v; }
	double get_mu_fidelity() const { return mu_fidelity; }
	void set_w_p(double p_v) { w_p = p_v; }
	double get_w_p() const { return w_p; }
	void set_w_t(double p_v) { w_t = p_v; }
	double get_w_t() const { return w_t; }
	void set_proximity_threshold(double p_v) { proximity_threshold = p_v; }
	double get_proximity_threshold() const { return proximity_threshold; }
	void set_angular_proximity_threshold(double p_v) { angular_proximity_threshold = p_v; }
	double get_angular_proximity_threshold() const { return angular_proximity_threshold; }
	void set_min_distance_between_anchors(double p_v) { min_distance_between_anchors = p_v; }
	double get_min_distance_between_anchors() const { return min_distance_between_anchors; }
	void set_planarity_allowed(bool p_v) { planarity_allowed = p_v; }
	bool get_planarity_allowed() const { return planarity_allowed; }
	void set_on_surface_weight(double p_v) { on_surface_weight = p_v; }
	double get_on_surface_weight() const { return on_surface_weight; }
	void set_project_on_patch_callback(const Callable &p_cb) { project_on_patch_callback = p_cb; }
	Callable get_project_on_patch_callback() const { return project_on_patch_callback; }
	void set_use_avbd(bool p_v) { use_avbd = p_v; }
	bool get_use_avbd() const { return use_avbd; }
	void set_avbd_rho(double p_v) { avbd_rho = p_v; }
	double get_avbd_rho() const { return avbd_rho; }
	void set_avbd_max_iter(int p_v) { avbd_max_iter = p_v; }
	int get_avbd_max_iter() const { return avbd_max_iter; }
	void set_avbd_tol(double p_v) { avbd_tol = p_v; }
	double get_avbd_tol() const { return avbd_tol; }
};

// Greedy outer-loop KKT solver. Port of ConstraintSolver.cs.
//
// Usage:
//   var solver := CassieConstraintSolver.new()
//   var result := solver.solve(curve, constraints, ortho_dirs, params, is_closed)
//   var beautified : Curve3D = result["curve"]
//
// Result dictionary keys:
//   "curve"               : Curve3D (beautified)
//   "intersections"       : TypedArray<CassieIntersectionConstraint>
//   "mirror_constraints"  : TypedArray<CassieMirrorPlaneConstraint>
//   "applied_anchors"     : PackedInt32Array
//   "rejected_count"      : int
//   "planar"              : bool
//   "is_closed_loop"      : bool
class CassieConstraintSolver : public RefCounted {
	GDCLASS(CassieConstraintSolver, RefCounted);

protected:
	static void _bind_methods();

public:
	CassieConstraintSolver() = default;

	// Returns a Dictionary as documented above. p_constraints is a flat
	// array of CassieConstraint-derived candidates. p_ortho_directions
	// supplies snap directions for the planarity constraint (typically the
	// world XYZ axes); pass an empty array to disable ortho snapping.
	Dictionary solve(
			const Ref<Curve3D> &p_curve,
			const TypedArray<CassieConstraint> &p_constraints,
			const PackedVector3Array &p_ortho_directions,
			const Ref<CassieSolverParams> &p_params,
			bool p_is_closed);
};
