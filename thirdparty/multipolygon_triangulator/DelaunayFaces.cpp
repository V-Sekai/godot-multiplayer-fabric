#include "DelaunayFaces.h"

// ENG-88: ports verbatim from cassie-triangulation/src/Utility/DelaunayFaces.cpp
// (https://github.com/V-Sekai-fire/cassie-triangulation).
//
// Routes 3D Delaunay through Geogram's GEO::Delaunay BDEL creator
// instead of Godot's Delaunay3D::tetrahedralize. The Godot path was a
// ~100x perf regression on real CASSIE hand-drawn input — its R128
// fixed-point predicates are slow enough that MingCurve's Steiner
// insertion loop (insertMidPointsTetgen) spent 600+ ms per cycle on a
// nB=30 boundary. Upstream Unity CASSIE ships this exact Geogram path
// via Triangulation_dll.dll and runs real-time on the same data.
//
// Numerical robustness regressions ("No solution!" from Geogram on
// adversarial near-coplanar input) are handled by MingCurve's existing
// 500-iter perturb-retry loop, exactly as upstream relies on.

#include <geogram/basic/common.h>
#include <geogram/basic/logger.h>
#include <geogram/basic/numeric.h>
#include <geogram/delaunay/delaunay.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace {

void ensure_geogram_initialized() {
	static std::once_flag init_flag;
	std::call_once(init_flag, []() {
		GEO::initialize(GEO::GEOGRAM_INSTALL_NONE);
		GEO::Logger::instance()->set_quiet(true);
	});
}

struct TriangleKeyHash {
	std::size_t operator()(uint64_t k) const noexcept {
		k ^= k >> 33;
		k *= 0xff51afd7ed558ccdULL;
		k ^= k >> 33;
		k *= 0xc4ceb9fe1a85ec53ULL;
		k ^= k >> 33;
		return static_cast<std::size_t>(k);
	}
};

uint64_t make_key(int a, int b, int c) {
	return (uint64_t(uint32_t(a)) << 42) | (uint64_t(uint32_t(b)) << 21) |
			uint64_t(uint32_t(c));
}

}  // namespace

namespace cassie {

DelaunayFaces::DelaunayFaces() = default;
DelaunayFaces::~DelaunayFaces() { clear(); }

void DelaunayFaces::clear() {
	delete[] trifacelist;
	trifacelist      = nullptr;
	numberoftrifaces = 0;
}

bool DelaunayFaces::compute(const double *points, int npoints) {
	clear();
	if (points == nullptr || npoints < 4) {
		return false;
	}

	ensure_geogram_initialized();

	// Reset Geogram's global RNG so symbolic perturbation is
	// deterministic call-to-call. Without this the near-coplanar
	// paths produce wildly varying tet counts.
	GEO::Numeric::random_reset();

	// -fno-exceptions: upstream wraps Delaunay::create + set_vertices in
	// a try/catch to recover from InvalidInput on adversarial point sets.
	// Under Godot's -fno-exceptions, Geogram's would-be throws have been
	// patched to geo_abort in thirdparty/geogram/patches/. The fallback
	// for adversarial input is MingCurve's 500-iter perturb-retry loop.
	GEO::Delaunay_var delaunay = GEO::Delaunay::create(3, "BDEL");
	if (delaunay.get() == nullptr) {
		return false;
	}
	delaunay->set_vertices(GEO::index_t(npoints), points);

	const GEO::index_t nb_cells = delaunay->nb_cells();
	std::unordered_set<uint64_t, TriangleKeyHash> seen;
	seen.reserve(std::size_t(nb_cells) * 4u);

	static const int kFacetVerts[4][3] = {
		{ 1, 2, 3 },
		{ 0, 2, 3 },
		{ 0, 1, 3 },
		{ 0, 1, 2 },
	};

	std::vector<int> tris;
	tris.reserve(std::size_t(nb_cells) * 12u);

	for (GEO::index_t c = 0; c < nb_cells; ++c) {
		int v[4];
		for (int i = 0; i < 4; ++i) {
			v[i] = int(delaunay->cell_vertex(c, GEO::index_t(i)));
		}
		for (int f = 0; f < 4; ++f) {
			int a = v[kFacetVerts[f][0]];
			int b = v[kFacetVerts[f][1]];
			int d = v[kFacetVerts[f][2]];
			if (a > b) std::swap(a, b);
			if (b > d) std::swap(b, d);
			if (a > b) std::swap(a, b);
			if (seen.insert(make_key(a, b, d)).second) {
				tris.push_back(a);
				tris.push_back(b);
				tris.push_back(d);
			}
		}
	}

	if (tris.empty()) {
		return false;
	}

	numberoftrifaces = int(tris.size() / 3);
	trifacelist      = new int[tris.size()];
	std::copy(tris.begin(), tris.end(), trifacelist);
	return true;
}

}  // namespace cassie
