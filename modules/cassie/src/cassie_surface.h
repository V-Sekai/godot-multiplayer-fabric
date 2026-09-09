/**************************************************************************/
/*  cassie_surface.h                                                      */
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

#include "cassie_path_3d.h"

#include "core/object/ref_counted.h"
#include "core/templates/vector.h"
#include "scene/resources/mesh.h"

class CassieSurface : public RefCounted {
	GDCLASS(CassieSurface, RefCounted);

private:
	Vector<Ref<CassiePath3D>> boundary_paths;
	Ref<ArrayMesh> generated_mesh;

	bool auto_beautify = true;
	bool auto_resample = true;
	bool use_intrinsic_remeshing = true;
	int target_boundary_points = 30;

	float beautify_lambda = 0.5;
	float beautify_mu = -0.53;
	int beautify_iterations = 5;

	int max_flip_iterations = 100;
	int smooth_iterations = 5;
	float target_edge_length = -1.0;

protected:
	static void _bind_methods();

public:
	CassieSurface();
	~CassieSurface();

	void add_boundary_path(const Ref<CassiePath3D> &p_path);
	void clear_boundary_paths();
	int get_boundary_path_count() const;
	Ref<CassiePath3D> get_boundary_path(int p_index) const;

	void set_auto_beautify(bool p_enable);
	bool get_auto_beautify() const;

	void set_auto_resample(bool p_enable);
	bool get_auto_resample() const;

	void set_use_intrinsic_remeshing(bool p_enable);
	bool get_use_intrinsic_remeshing() const;

	void set_target_boundary_points(int p_count);
	int get_target_boundary_points() const;

	void set_beautify_lambda(float p_lambda);
	float get_beautify_lambda() const;

	void set_beautify_mu(float p_mu);
	float get_beautify_mu() const;

	void set_beautify_iterations(int p_iterations);
	int get_beautify_iterations() const;

	void set_max_flip_iterations(int p_iterations);
	int get_max_flip_iterations() const;

	void set_smooth_iterations(int p_iterations);
	int get_smooth_iterations() const;

	void set_target_edge_length(float p_length);
	float get_target_edge_length() const;

	Ref<ArrayMesh> generate_surface();
	Ref<ArrayMesh> get_generated_mesh() const;
	void clear_generated_mesh();
};
