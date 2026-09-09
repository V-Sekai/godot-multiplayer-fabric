/-!
# `CassieAvbd.CycleDetect.Vec` — 3D vector operations

Pure functions used by `Arrangement.lean` (segment-segment closest pair)
and `Walk.lean` (angular CCW pick). Float-typed; matches the C++ runtime
side bit-for-bit modulo IEEE rounding.

Notable choices:
 - tuples not records → no allocation overhead, `@[inline]` everywhere
 - no `Float.max` method; using top-level `max` / `min` from `Init`
-/

namespace CassieAvbd.CycleDetect

abbrev Vec3 := Float × Float × Float

@[inline] def Vec3.x (v : Vec3) : Float := v.1
@[inline] def Vec3.y (v : Vec3) : Float := v.2.1
@[inline] def Vec3.z (v : Vec3) : Float := v.2.2

@[inline] def sub (a b : Vec3) : Vec3 :=
  (a.x - b.x, a.y - b.y, a.z - b.z)

@[inline] def add (a b : Vec3) : Vec3 :=
  (a.x + b.x, a.y + b.y, a.z + b.z)

@[inline] def scl (a : Vec3) (s : Float) : Vec3 :=
  (a.x * s, a.y * s, a.z * s)

@[inline] def dot (a b : Vec3) : Float :=
  a.x * b.x + a.y * b.y + a.z * b.z

@[inline] def cross (a b : Vec3) : Vec3 :=
  (a.y * b.z - a.z * b.y,
   a.z * b.x - a.x * b.z,
   a.x * b.y - a.y * b.x)

@[inline] def vlen (a : Vec3) : Float := (dot a a).sqrt

@[inline] def vdist (a b : Vec3) : Float := vlen (sub a b)

@[inline] def normalize (a : Vec3) : Vec3 :=
  let l := vlen a
  if l < 1e-12 then (0.0, 0.0, 0.0) else scl a (1.0 / l)

-- Quick smoke checks.

#eval dot (1.0, 0.0, 0.0) (1.0, 0.0, 0.0)           -- expect 1.0
#eval cross (1.0, 0.0, 0.0) (0.0, 1.0, 0.0)         -- expect (0, 0, 1)
#eval vdist (0.0, 0.0, 0.0) (3.0, 4.0, 0.0)         -- expect 5.0

end CassieAvbd.CycleDetect
