/**************************************************************************/
/*  cassie_obj_ffi.cpp                                                    */
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

#include <lean/lean.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct ObjMesh {
	std::vector<double> positions; // flat x,y,z
	std::vector<uint32_t> faces; // flat a,b,c (0-based)
};

inline ObjMesh *as_mesh(size_t handle) {
	return reinterpret_cast<ObjMesh *>(handle);
}

// Parse one vertex index from a face token like "12", "12/3", "12//5",
// "12/3/5". Returns 0-based; -1 on parse failure.
int32_t parse_face_index(const char *tok, int32_t n_verts) {
	if (!tok || !*tok) {
		return -1;
	}
	char *end = nullptr;
	long idx = std::strtol(tok, &end, 10);
	if (end == tok) {
		return -1;
	}
	if (idx > 0) {
		return static_cast<int32_t>(idx - 1);
	}
	if (idx < 0) {
		return n_verts + static_cast<int32_t>(idx); // relative
	}
	return -1;
}

} // namespace

extern "C" {

LEAN_EXPORT lean_obj_res cassie_obj_load(
		lean_obj_arg path_str, lean_obj_arg /*world*/) {
	const char *path = lean_string_cstr(path_str);
	auto *mesh = new ObjMesh();
	std::ifstream f(path);
	if (!f.is_open()) {
		lean_dec_ref(path_str);
		return lean_io_result_mk_ok(lean_box_usize(
				reinterpret_cast<size_t>(mesh)));
	}
	std::string line;
	std::vector<int32_t> face_tokens;
	face_tokens.reserve(8);
	while (std::getline(f, line)) {
		// strip trailing CR for CRLF files
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
			line.pop_back();
		}
		if (line.empty() || line[0] == '#') {
			continue;
		}
		if (line.size() < 2) {
			continue;
		}
		if (line[0] == 'v' && line[1] == ' ') {
			double x = 0, y = 0, z = 0;
			std::istringstream s(line.substr(2));
			if (s >> x >> y >> z) {
				mesh->positions.push_back(x);
				mesh->positions.push_back(y);
				mesh->positions.push_back(z);
			}
		} else if (line[0] == 'f' && line[1] == ' ') {
			face_tokens.clear();
			const int32_t n_verts =
					static_cast<int32_t>(mesh->positions.size() / 3);
			std::istringstream s(line.substr(2));
			std::string tok;
			while (s >> tok) {
				// keep only the position component (before first '/')
				const size_t slash = tok.find('/');
				if (slash != std::string::npos) {
					tok.resize(slash);
				}
				const int32_t idx = parse_face_index(tok.c_str(), n_verts);
				if (idx >= 0 && idx < n_verts) {
					face_tokens.push_back(idx);
				}
			}
			// fan-triangulate
			for (size_t i = 2; i < face_tokens.size(); ++i) {
				mesh->faces.push_back(uint32_t(face_tokens[0]));
				mesh->faces.push_back(uint32_t(face_tokens[i - 1]));
				mesh->faces.push_back(uint32_t(face_tokens[i]));
			}
		}
		// vn, vt, s, g, o, mtllib, usemtl ignored
	}
	lean_dec_ref(path_str);
	return lean_io_result_mk_ok(lean_box_usize(
			reinterpret_cast<size_t>(mesh)));
}

LEAN_EXPORT lean_obj_res cassie_obj_free(
		size_t handle, lean_obj_arg /*world*/) {
	delete as_mesh(handle);
	return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res cassie_obj_n_vertices(
		size_t handle, lean_obj_arg /*world*/) {
	return lean_io_result_mk_ok(
			lean_box_usize(as_mesh(handle)->positions.size() / 3));
}

LEAN_EXPORT lean_obj_res cassie_obj_n_faces(
		size_t handle, lean_obj_arg /*world*/) {
	return lean_io_result_mk_ok(
			lean_box_usize(as_mesh(handle)->faces.size() / 3));
}

LEAN_EXPORT lean_obj_res cassie_obj_get_positions(
		size_t handle, lean_obj_arg out, lean_obj_arg /*world*/) {
	const auto *mesh = as_mesh(handle);
	double *dst = lean_float_array_cptr(out);
	std::memcpy(dst, mesh->positions.data(),
			mesh->positions.size() * sizeof(double));
	return lean_io_result_mk_ok(out);
}

LEAN_EXPORT lean_obj_res cassie_obj_get_faces(
		size_t handle, lean_obj_arg out, lean_obj_arg /*world*/) {
	const auto *mesh = as_mesh(handle);
	auto *dst = reinterpret_cast<uint32_t *>(lean_sarray_cptr(out));
	std::memcpy(dst, mesh->faces.data(),
			mesh->faces.size() * sizeof(uint32_t));
	return lean_io_result_mk_ok(out);
}

} // extern "C"
