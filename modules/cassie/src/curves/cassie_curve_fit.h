/**************************************************************************/
/*  cassie_curve_fit.h                                                    */
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

#include "core/math/vector3.h"
#include "core/object/ref_counted.h"
#include "core/variant/variant.h"
#include "scene/resources/curve.h"

// Schneider's recursive cubic Bezier fitter.
// Port of E:\cassie\Assets\Scripts\Curves\BezierCurve.cs (FitCurve and helpers).
//
// Pipeline: input samples -> RDP simplify -> chord-length parameterize ->
// 2x2 Cramer normal-equation solve (GenerateBezier) -> Newton-Raphson
// reparameterize up to 20 iterations -> recursive subdivide on error.
//
// Output is a baked-ready Curve3D (Godot composite cubic Bezier) where each
// fitted CubicBezier segment becomes one anchor pair via the conversion:
//   absolute (P0,P1,P2,P3) -> (anchor=P0, out=P1-P0) + (anchor=P3, in=P2-P3).
//
// Tier 3 mutators (cut_at, split_for_constraints, project_point) live in the
// same source file.

// Fits a single straight segment as a degenerate cubic Bezier
// (handles control = anchors). Returns a Curve3D with two anchors.
Ref<Curve3D> cassie_fit_line(const Vector3 &p_a, const Vector3 &p_b);

// Fits one or more cubic Bezier segments through p_points using Schneider's
// algorithm. p_error is the per-segment fit tolerance; p_rdp_error is the
// pre-pass polyline-simplification tolerance.
// Returns null Ref if p_points has fewer than 2 entries.
Ref<Curve3D> cassie_fit_curve(const PackedVector3Array &p_points,
		float p_error = 1e-2f, float p_rdp_error = 2e-3f);

// Pass-B mutator: trims p_curve at the chord-length-normalized parameter
// p_t in [0, 1]. If p_throw_before, the segment before t is discarded and
// the result curve starts at the cut point. Otherwise the segment after t
// is discarded and the result ends at the cut point. When the cut falls
// within p_snap_threshold of an existing anchor it snaps to that anchor
// instead of splitting via De Casteljau. Returns a new Curve3D; the input
// is not mutated.
//
// Port of BezierCurve.CutAt (E:\cassie\Assets\Scripts\Curves\BezierCurve.cs).
Ref<Curve3D> cassie_curve_cut_at(const Ref<Curve3D> &p_curve, float p_t,
		bool p_throw_before, float p_snap_threshold);

// Splits p_curve at every parameter in p_params, in ascending order. Each
// parameter is in [0, 1] on the original curve's chord-length-uniform
// parameterization (matches cassie_curve_cut_at's input convention).
// p_snap_threshold passes through to each cut; a parameter that lands within
// the threshold of an existing anchor produces a degenerate (zero/one point)
// substroke rather than crashing. Returns N+1 substrokes for N parameters.
Vector<Ref<Curve3D>> cassie_curve_split_for_constraints(
		const Ref<Curve3D> &p_curve,
		const PackedFloat32Array &p_params,
		float p_snap_threshold);

// Newton-Raphson closest-point projection of p_target onto p_curve. Performs
// a coarse uniform sample (8 per segment), refines the best segment with up
// to p_max_iter Newton steps using the same residual + Jacobian as the
// Schneider fitter's reparameterize pass. Writes the projected position into
// r_position and returns the chord-uniform parameter t ∈ [0, 1].
// Returns 0.0 and writes Vector3() on degenerate input (null curve, < 2
// anchors).
float cassie_curve_project_point(
		const Ref<Curve3D> &p_curve,
		const Vector3 &p_target,
		Vector3 &r_position,
		int p_max_iter = 20);
