#ifndef Point3_h
#define Point3_h

#include "Vector3.h"
#include <iostream>
#include <random>

#define IZ_EPSILON 1e-16
#define P3AE_EPSILON 1e-12

namespace mwt {

class Point3
{
public:
    Point3();
    Point3(const Point3& p);
    Point3(double _x, double _y, double _z);

    Point3& operator=(const Point3& a);

	const double &operator[](int n) const;
	double &operator[](int n);

    Point3& operator+=(const Vector3& v);
    Point3& operator-=(const Vector3& v);
    Point3& operator*=(double s);

    Vector3 operator-(const Point3 & p) const;
    Point3 operator+(const Vector3 & v) const;
    Point3 operator-(const Vector3 & v) const;

    double distanceTo(const Point3& p) const;
    double distanceToSquared(const Point3& p) const;
    double distanceFromOrigin() const;
    double distanceFromOriginSquared() const;

    bool operator==( const Point3 &p ) const;
    bool operator!=( const Point3 &p ) const;

    bool approxEqual( const Point3 &p, double eps = P3AE_EPSILON ) const;
    void print() const;
	void pertube(double ptb);

private:
    double x, y, z;
};

inline Point3 lerp( const Point3& p0, const Point3& p1, double dT )
{
    const double dTMinus = 1.0 - dT;
    return Point3( dTMinus * p0[0] + dT * p1[0], dTMinus * p0[1] + dT * p1[1], dTMinus * p0[2] + dT * p1[2] );
}

// Per-thread Mersenne Twister used to break input degeneracy in
// MingCurve's edge-protection loop. Caller (Triangulate) re-seeds
// via reset_perturb_rng() to guarantee call-to-call determinism.
inline std::mt19937& perturb_rng()
{
	static thread_local std::mt19937 engine{0u};
	return engine;
}

inline void reset_perturb_rng(unsigned seed = 0u)
{
	perturb_rng().seed(seed);
}

inline void Point3::pertube(double ptb)
{
	std::uniform_int_distribution<int> d(0, 9999);
	auto& rng = perturb_rng();
	x += ptb * d(rng);
	y += ptb * d(rng);
	z += ptb * d(rng);
}

inline std::ostream& operator<<(std::ostream& os, const Point3& p)
{
    os << "(" << p[0] << ", " << p[1] << ", " << p[2] << ")";
    return os;
}

}  // namespace mwt

#endif /* Point3_h */
