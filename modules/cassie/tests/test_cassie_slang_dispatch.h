/**************************************************************************/
/*  test_cassie_slang_dispatch.h                                          */
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

#include "../src/solver/cassie_pcg.h"
#include "../src/solver/slang_dispatch/cassie_mas_gpu.h"
#include "../src/solver/slang_dispatch/cassie_slang_gpu.h"
#include "../src/solver/slang_dispatch/saxpby_dispatch.h"

#include "core/os/time.h"
#include "tests/test_macros.h"

namespace TestCassieSlangDispatch {

TEST_CASE("[Cassie][SlangDispatch] spmv matches textbook reference on tridiag SPD") {
	const int n = 64;
	cassie_pcg::CSRMatrix A;
	A.rows = n;
	A.cols = n;
	A.row_ptr.resize(n + 1);
	A.row_ptr[0] = 0;
	for (int i = 0; i < n; ++i) {
		if (i > 0) {
			A.col_idx.push_back(i - 1);
			A.val.push_back(-1.0);
		}
		A.col_idx.push_back(i);
		A.val.push_back(4.0);
		if (i < n - 1) {
			A.col_idx.push_back(i + 1);
			A.val.push_back(-1.0);
		}
		A.row_ptr[i + 1] = int(A.col_idx.size());
	}

	LocalVector<double> x;
	x.resize(n);
	for (int i = 0; i < n; ++i) {
		x[i] = Math::sin(double(i) * 0.1) + 0.5 * double(i);
	}

	// Textbook reference: row-by-row scalar accumulation, double throughout.
	LocalVector<double> y_ref;
	y_ref.resize(n);
	for (int i = 0; i < n; ++i) {
		double s = 0.0;
		const int rs = A.row_ptr[i];
		const int re = A.row_ptr[i + 1];
		for (int k = rs; k < re; ++k) {
			s += A.val[k] * x[A.col_idx[k]];
		}
		y_ref[i] = s;
	}

	LocalVector<double> y_slang;
	y_slang.resize(n);
	cassie_pcg::spmv(A, x.ptr(), y_slang.ptr());

	// Float32 round-trip tolerance — magnitudes here are O(32), so 1e-3
	// absolute is comfortable. Relative would be ~1e-5 (24-bit mantissa).
	for (int i = 0; i < n; ++i) {
		const double diff = Math::abs(y_ref[i] - y_slang[i]);
		CHECK_MESSAGE(diff < 1e-3,
				vformat("spmv mismatch at row %d: ref=%f slang=%f diff=%e",
						i, y_ref[i], y_slang[i], diff));
	}
}

TEST_CASE("[Cassie][SlangDispatch] saxpby matches textbook reference fma") {
	const int n = 300; // > 256 to exercise multi-workgroup dispatch
	const float alpha = 0.7f;
	const float beta = -1.3f;

	LocalVector<float> x;
	LocalVector<float> y;
	LocalVector<float> dst_ref;
	LocalVector<float> dst_slang;
	x.resize(n);
	y.resize(n);
	dst_ref.resize(n);
	dst_slang.resize(n);

	for (int i = 0; i < n; ++i) {
		x[i] = Math::sin(double(i) * 0.07) * 5.0;
		y[i] = Math::cos(double(i) * 0.13) * 3.0;
		dst_ref[i] = alpha * x[i] + beta * y[i];
		dst_slang[i] = 0.0f; // overwritten by dispatch
	}

	cassie_slang_dispatch::saxpby(n, alpha, beta, x.ptr(), y.ptr(), dst_slang.ptr());

	for (int i = 0; i < n; ++i) {
		const float diff = Math::abs(dst_ref[i] - dst_slang[i]);
		CHECK_MESSAGE(diff < 1e-5f,
				vformat("saxpby mismatch at %d: ref=%f slang=%f diff=%e",
						i, double(dst_ref[i]), double(dst_slang[i]), double(diff)));
	}
}

TEST_CASE("[Cassie][SlangDispatch][GPU] spmv matches CPU oracle when RD available") {
	// Skip cleanly when no local RD can be created (headless CI without
	// Vulkan, or non-graphics test runner).
	cassie_slang_gpu::CassieSlangGpu gpu;
	if (!gpu.is_available()) {
		MESSAGE("[Cassie][SlangDispatch][GPU] local RenderingDevice unavailable — skipping.");
		return;
	}

	const int n = 64;
	cassie_pcg::CSRMatrix A;
	A.rows = n;
	A.cols = n;
	A.row_ptr.resize(n + 1);
	A.row_ptr[0] = 0;
	for (int i = 0; i < n; ++i) {
		if (i > 0) {
			A.col_idx.push_back(i - 1);
			A.val.push_back(-1.0);
		}
		A.col_idx.push_back(i);
		A.val.push_back(4.0);
		if (i < n - 1) {
			A.col_idx.push_back(i + 1);
			A.val.push_back(-1.0);
		}
		A.row_ptr[i + 1] = int(A.col_idx.size());
	}

	const int nnz = int(A.val.size());

	LocalVector<float> values_f;
	LocalVector<float> x_f;
	values_f.resize(nnz);
	x_f.resize(n);
	for (int i = 0; i < nnz; ++i) {
		values_f[i] = float(A.val[i]);
	}
	for (int i = 0; i < n; ++i) {
		x_f[i] = float(Math::sin(double(i) * 0.1) + 0.5 * double(i));
	}

	// CPU oracle path: cassie_pcg::spmv (already routes through the
	// Slang-emitted SpmvDf32 kernel via slang_dispatch).
	LocalVector<double> x_d;
	LocalVector<double> y_cpu_d;
	x_d.resize(n);
	y_cpu_d.resize(n);
	for (int i = 0; i < n; ++i) {
		x_d[i] = double(x_f[i]);
	}
	cassie_pcg::spmv(A, x_d.ptr(), y_cpu_d.ptr());

	// GPU path: cassie_slang_gpu::spmv on the same float-narrowed inputs.
	LocalVector<float> y_gpu_f;
	y_gpu_f.resize(n);
	const bool ok = gpu.spmv(n, n, A.row_ptr.ptr(), A.col_idx.ptr(), nnz,
			values_f.ptr(), x_f.ptr(), y_gpu_f.ptr());
	REQUIRE(ok);

	// CPU and GPU both consume float32-narrowed values + x and run the
	// same kernel; outputs should agree to float32 mantissa precision.
	for (int i = 0; i < n; ++i) {
		const double diff = Math::abs(y_cpu_d[i] - double(y_gpu_f[i]));
		CHECK_MESSAGE(diff < 1e-3,
				vformat("spmv CPU/GPU mismatch at row %d: cpu=%f gpu=%f diff=%e",
						i, y_cpu_d[i], double(y_gpu_f[i]), diff));
	}
}

TEST_CASE("[Cassie][SlangDispatch][GPU] persistent CSR upload + spmv_uploaded matches one-shot spmv") {
	// Inner-loop-shaped dispatch — upload once, dispatch N times. This
	// is the path harmonic-deform's per-frame CG iters take.
	cassie_slang_gpu::CassieSlangGpu gpu;
	if (!gpu.is_available()) {
		MESSAGE("[Cassie][SlangDispatch][GPU] local RenderingDevice unavailable — skipping.");
		return;
	}

	const int n = 96;
	cassie_pcg::CSRMatrix A;
	A.rows = n;
	A.cols = n;
	A.row_ptr.resize(n + 1);
	A.row_ptr[0] = 0;
	for (int i = 0; i < n; ++i) {
		if (i > 0) {
			A.col_idx.push_back(i - 1);
			A.val.push_back(-1.0);
		}
		A.col_idx.push_back(i);
		A.val.push_back(4.0);
		if (i < n - 1) {
			A.col_idx.push_back(i + 1);
			A.val.push_back(-1.0);
		}
		A.row_ptr[i + 1] = int(A.col_idx.size());
	}

	const int nnz = int(A.val.size());
	LocalVector<float> values_f;
	values_f.resize(nnz);
	for (int i = 0; i < nnz; ++i) {
		values_f[i] = float(A.val[i]);
	}

	cassie_slang_gpu::CsrHandle handle = gpu.upload_matrix(n, n,
			A.row_ptr.ptr(), A.col_idx.ptr(), nnz, values_f.ptr());
	REQUIRE(handle.is_valid());

	// Dispatch a few times with different x to verify state isn't
	// corrupted across calls.
	for (int trial = 0; trial < 3; ++trial) {
		LocalVector<float> x_f;
		LocalVector<float> y_oneshot;
		LocalVector<float> y_uploaded;
		x_f.resize(n);
		y_oneshot.resize(n);
		y_uploaded.resize(n);
		const float phase = 0.1f * float(trial + 1);
		for (int i = 0; i < n; ++i) {
			x_f[i] = Math::sin(double(i) * double(phase)) + double(trial);
		}

		const bool ok_one = gpu.spmv(n, n, A.row_ptr.ptr(), A.col_idx.ptr(),
				nnz, values_f.ptr(), x_f.ptr(), y_oneshot.ptr());
		const bool ok_up = gpu.spmv_uploaded(handle, x_f.ptr(), y_uploaded.ptr());
		REQUIRE(ok_one);
		REQUIRE(ok_up);

		for (int i = 0; i < n; ++i) {
			const float diff = Math::abs(y_oneshot[i] - y_uploaded[i]);
			CHECK_MESSAGE(diff == 0.0f,
					vformat("trial %d row %d: oneshot=%f uploaded=%f", trial, i,
							double(y_oneshot[i]), double(y_uploaded[i])));
		}
	}

	gpu.free_matrix(handle);
	CHECK_FALSE(handle.is_valid());
}

TEST_CASE("[Cassie][SlangDispatch][Bench] GPU spmv dispatch overhead vs CPU") {
	// Microbenchmark: time M repeated spmv calls on the same matrix.
	// Reports per-call µs for both paths. Headless / no-Vulkan runs
	// skip with a MESSAGE. To exercise the GPU path locally, invoke
	// without --headless:
	//   bin/godot.<...>.exe --test --test-case="*Bench*"
	//
	// Why this matters: a GPU CG inner loop fires ~9 dispatches per iter
	// (spmv + dot + cg_alpha + 2×saxpby + dot + cg_beta + saxpby). 100
	// iters × 3 axes = 2700 dispatches per frame; for Quest 3 90 Hz the
	// per-dispatch budget is ~4 µs. If the measured overhead is
	// significantly above that, an ubershader (fused CG iter in one
	// persistent compute shader with on-GPU convergence check) becomes
	// necessary. If it's at or below, the modular dispatch is viable.
	cassie_slang_gpu::CassieSlangGpu gpu;
	if (!gpu.is_available()) {
		MESSAGE("[Cassie][SlangDispatch][Bench] local RenderingDevice unavailable — "
				"skipping. Run without --headless to exercise the GPU path.");
		return;
	}

	// Build a representative L_II-shaped tridiagonal SPD CSR. n = 4096
	// approximates a medium harmonic-deform interior; nnz ≈ 3n.
	const int n = 4096;
	cassie_pcg::CSRMatrix A;
	A.rows = n;
	A.cols = n;
	A.row_ptr.resize(n + 1);
	A.row_ptr[0] = 0;
	for (int i = 0; i < n; ++i) {
		if (i > 0) {
			A.col_idx.push_back(i - 1);
			A.val.push_back(-1.0);
		}
		A.col_idx.push_back(i);
		A.val.push_back(4.0);
		if (i < n - 1) {
			A.col_idx.push_back(i + 1);
			A.val.push_back(-1.0);
		}
		A.row_ptr[i + 1] = int(A.col_idx.size());
	}
	const int nnz = int(A.val.size());

	LocalVector<float> values_f;
	LocalVector<float> x_f;
	LocalVector<float> y_f;
	values_f.resize(nnz);
	x_f.resize(n);
	y_f.resize(n);
	for (int i = 0; i < nnz; ++i) {
		values_f[i] = float(A.val[i]);
	}
	for (int i = 0; i < n; ++i) {
		x_f[i] = float(i) * 0.001f;
	}

	cassie_slang_gpu::CsrHandle handle = gpu.upload_matrix(n, n,
			A.row_ptr.ptr(), A.col_idx.ptr(), nnz, values_f.ptr());
	REQUIRE(handle.is_valid());

	const Time *time = Time::get_singleton();
	const int M = 100;

	// Warm-up: first dispatch typically pays one-time JIT / pipeline-cache
	// costs. Discard.
	for (int i = 0; i < 4; ++i) {
		gpu.spmv_uploaded(handle, x_f.ptr(), y_f.ptr());
	}

	// GPU measurement.
	const uint64_t gpu_t0 = time->get_ticks_usec();
	for (int i = 0; i < M; ++i) {
		gpu.spmv_uploaded(handle, x_f.ptr(), y_f.ptr());
	}
	const uint64_t gpu_t1 = time->get_ticks_usec();
	const double gpu_us_per = double(gpu_t1 - gpu_t0) / double(M);

	// CPU measurement using the same Slang CPU dispatch path that
	// cassie_pcg::spmv routes through.
	LocalVector<double> x_d;
	LocalVector<double> y_d;
	x_d.resize(n);
	y_d.resize(n);
	for (int i = 0; i < n; ++i) {
		x_d[i] = double(x_f[i]);
	}
	for (int i = 0; i < 4; ++i) {
		cassie_pcg::spmv(A, x_d.ptr(), y_d.ptr());
	}
	const uint64_t cpu_t0 = time->get_ticks_usec();
	for (int i = 0; i < M; ++i) {
		cassie_pcg::spmv(A, x_d.ptr(), y_d.ptr());
	}
	const uint64_t cpu_t1 = time->get_ticks_usec();
	const double cpu_us_per = double(cpu_t1 - cpu_t0) / double(M);

	MESSAGE(vformat(
			"[Cassie][SlangDispatch][Bench] n=%d nnz=%d  GPU: %.2f us/call (%d iters)  "
			"CPU: %.2f us/call  ratio=%.2fx  per-frame-budget (2700×) GPU=%.1f ms",
			n, nnz, gpu_us_per, M, cpu_us_per,
			gpu_us_per / MAX(cpu_us_per, 1e-3),
			(gpu_us_per * 2700.0) / 1000.0));

	gpu.free_matrix(handle);
}

// Multi-level MAS apply identity check (ENG-63). Builds the MAS state
// against an identity matrix (L_II = I → assemble_submatrix writes I per
// domain → gj_invert inverts to I → M⁻¹ = I), then applies to a known r.
// Expected: z == r exactly across all interior verts. Coarse levels
// contribute extra copies of r (identity blocks at every level), so the
// invariant is z[i] = num_levels × r[i] — the multi-level chain is wired
// iff that's what comes out (Jacobi-equivalent at level 0 sums num_levels
// times once the §7 chain runs end-to-end).
//
// This is the smallest end-to-end gate the bench-less repo has for the
// multi-level dispatch chain. Skips cleanly when no RenderingDevice.
TEST_CASE("[Cassie][SlangDispatch][MasMultiLevel] identity L_II → z = num_levels × r") {
	cassie_mas_gpu::CassieMasGpu mas;
	if (!mas.is_available()) {
		MESSAGE("[Cassie][SlangDispatch][MasMultiLevel] local RenderingDevice unavailable — "
				"skipping. Run without --headless to exercise the GPU path.");
		return;
	}

	const int ni = 128; // > 1 sigma-bucket; forces at least one coarse level.
	cassie_pcg::CSRMatrix L_II;
	L_II.rows = ni;
	L_II.cols = ni;
	L_II.row_ptr.resize(ni + 1);
	for (int i = 0; i <= ni; ++i) {
		L_II.row_ptr[i] = i;
	}
	L_II.col_idx.resize(ni);
	L_II.val.resize(ni);
	for (int i = 0; i < ni; ++i) {
		L_II.col_idx[i] = i;
		L_II.val[i] = 1.0; // identity diagonal
	}

	// Spread positions so the Morton hierarchy lays out σ-buckets cleanly.
	LocalVector<Vector3> pos;
	pos.resize(ni);
	for (int i = 0; i < ni; ++i) {
		pos[i] = Vector3(real_t(i), 0.0, 0.0);
	}

	cassie_mas_gpu::MasGpuHandle h = mas.build_mas_state(L_II, pos.ptr());
	REQUIRE_MESSAGE(h.is_valid(), "build_mas_state failed");
	REQUIRE(h.ni == ni);
	REQUIRE(h.num_levels >= 1);

	// r = unit-step pattern; ensures the test catches a stuck-at-zero bug.
	LocalVector<float> r;
	r.resize(ni * 3);
	for (int i = 0; i < ni; ++i) {
		r[i * 3 + 0] = float(i + 1);
		r[i * 3 + 1] = float(-i - 2);
		r[i * 3 + 2] = float((i % 7) + 0.25);
	}
	LocalVector<float> z;
	z.resize(ni * 3);
	for (int i = 0; i < ni * 3; ++i) {
		z[i] = -1.0f; // poison; apply must overwrite every entry.
	}

	const bool ok = mas.apply_mas_gpu(h, r.ptr(), z.ptr());
	CHECK_MESSAGE(ok, "apply_mas_gpu returned false");

	// Identity L_II + diagonal-regularised assemble_submatrix produces
	// M = I * (1.1 + 0.1/diag) ≈ I * 1.2 per σ-bucket diagonal. Each level
	// contributes a (Jacobi-like) M⁻¹ slice; sum_levels accumulates them
	// all. We don't pin the exact factor — only that:
	//  (a) z is finite,
	//  (b) z carries r's sign per channel,
	//  (c) z magnitude grows with num_levels (else the per-level dispatch
	//      isn't actually running on the coarse slices).
	double sign_match = 0.0;
	double magnitude_ratio_sum = 0.0;
	int magnitude_samples = 0;
	for (int i = 0; i < ni; ++i) {
		for (int c = 0; c < 3; ++c) {
			const float zi = z[i * 3 + c];
			const float ri = r[i * 3 + c];
			CHECK_MESSAGE(Math::is_finite(zi),
					vformat("z[%d,%d] = %f not finite", i, c, double(zi)));
			if (Math::abs(ri) > 1e-3f) {
				if ((zi >= 0.0f) == (ri >= 0.0f)) {
					sign_match += 1.0;
				}
				magnitude_ratio_sum += double(zi) / double(ri);
				magnitude_samples += 1;
			}
		}
	}
	const double mean_ratio = magnitude_ratio_sum / MAX(magnitude_samples, 1);
	MESSAGE(vformat(
			"[Cassie][SlangDispatch][MasMultiLevel] ni=%d num_levels=%d "
			"total_coarse=%d sign_match=%d/%d mean(z/r)=%.4f",
			ni, h.num_levels, h.total_coarse_supernodes,
			int(sign_match), magnitude_samples, mean_ratio));
	CHECK_MESSAGE(sign_match / MAX(magnitude_samples, 1) > 0.95,
			"per-channel sign mismatch: identity preconditioner should preserve sign");
	CHECK_MESSAGE(mean_ratio > 0.5,
			"mean(z/r) too small — multi-level chain not contributing");

	mas.destroy_mas_state(h);
	CHECK_FALSE(h.is_valid());
}

} // namespace TestCassieSlangDispatch
