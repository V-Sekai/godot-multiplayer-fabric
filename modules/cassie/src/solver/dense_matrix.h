/**************************************************************************/
/*  dense_matrix.h                                                        */
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

#include "core/templates/local_vector.h"

#include <cstddef>

// Track 5 Phase B — Eigen-free dense linear-algebra scaffolding for the
// constraint-block API. Row-major `LocalVector<double>` storage so it
// composes with Godot core's allocators and ships zero Eigen surface to
// the rest of the module.
//
// The block API ({Soft, Hard}ConstraintBlock::get_blocks) accumulates
// contributions into a destination matrix + vector. Writes are
// per-element (every existing impl does this); Eigen offered no
// performance advantage at the CASSIE scale (≤ 45 x 45 KKT, ~12 rows
// per constraint).

namespace cassie_solver {

class DenseMatrix {
	int rows_ = 0;
	int cols_ = 0;
	LocalVector<double> data_; // row-major, size rows_ * cols_

public:
	DenseMatrix() = default;

	int rows() const { return rows_; }
	int cols() const { return cols_; }
	const double *data() const { return data_.ptr(); }
	double *data() { return data_.ptr(); }

	void resize(int p_rows, int p_cols) {
		rows_ = p_rows;
		cols_ = p_cols;
		data_.resize(size_t(p_rows) * size_t(p_cols));
	}

	void zero() {
		const int n = rows_ * cols_;
		for (int i = 0; i < n; ++i) {
			data_[i] = 0.0;
		}
	}

	void zero_resize(int p_rows, int p_cols) {
		resize(p_rows, p_cols);
		zero();
	}

	double operator()(int p_i, int p_j) const {
		return data_[size_t(p_i) * size_t(cols_) + size_t(p_j)];
	}
	double &operator()(int p_i, int p_j) {
		return data_[size_t(p_i) * size_t(cols_) + size_t(p_j)];
	}

	// In-place A += B element-wise. Caller asserts compatible shapes.
	void add_into(const DenseMatrix &p_other) {
		const int n = rows_ * cols_;
		for (int i = 0; i < n; ++i) {
			data_[i] += p_other.data_[i];
		}
	}

	// y = A * x. p_x has length cols_; p_y is resized to length rows_.
	void multiply_vec(const double *p_x, double *p_y) const {
		for (int i = 0; i < rows_; ++i) {
			double s = 0.0;
			const double *row = data_.ptr() + size_t(i) * size_t(cols_);
			for (int j = 0; j < cols_; ++j) {
				s += row[j] * p_x[j];
			}
			p_y[i] = s;
		}
	}

	// Extracts the diagonal into p_out (size = min(rows, cols)).
	void extract_diagonal(double *p_out) const {
		const int n = rows_ < cols_ ? rows_ : cols_;
		for (int i = 0; i < n; ++i) {
			p_out[i] = data_[size_t(i) * size_t(cols_) + size_t(i)];
		}
	}
};

class DenseVector {
	LocalVector<double> data_;

public:
	DenseVector() = default;

	int size() const { return int(data_.size()); }
	const double *data() const { return data_.ptr(); }
	double *data() { return data_.ptr(); }

	void resize(int p_n) { data_.resize(p_n); }

	void zero() {
		for (uint32_t i = 0; i < data_.size(); ++i) {
			data_[i] = 0.0;
		}
	}

	void zero_resize(int p_n) {
		resize(p_n);
		zero();
	}

	double operator[](int p_i) const { return data_[p_i]; }
	double &operator[](int p_i) { return data_[p_i]; }

	void add_into(const DenseVector &p_other) {
		const int n = int(data_.size());
		for (int i = 0; i < n; ++i) {
			data_[i] += p_other.data_[i];
		}
	}
};

} // namespace cassie_solver
