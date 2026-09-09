/-
ObjProbe — smoke test for the CassieObj FFI loader.

  lake exe obj_probe path/to/mesh.obj

Loads the mesh, prints vertex + face counts, and the first / last 3
vertices for visual sanity.
-/

import CassieObj

def main (args : List String) : IO Unit := do
  let path := args.headD "E:/TOOL_cloth_dynamics/src/assets/meshes/remeshed/agenthat2.obj"
  IO.eprintln s!"[obj_probe] loading {path}"
  let h ← CassieObj.load path
  let nv ← CassieObj.nVertices h
  let nf ← CassieObj.nFaces h
  IO.eprintln s!"[obj_probe]   vertices: {nv}"
  IO.eprintln s!"[obj_probe]   faces:    {nf}"
  if nv = 0 then
    IO.eprintln "[obj_probe] empty mesh — file open failed or no v lines"
    CassieObj.free h
    return
  let posOut := FloatArray.mk (Array.replicate (nv.toNat * 3) 0.0)
  let pos ← CassieObj.getPositions h posOut
  let triOut := ByteArray.mk (Array.replicate (nf.toNat * 3 * 4) (0 : UInt8))
  let tris ← CassieObj.getFaces h triOut
  -- First / last 3 vertices
  IO.eprintln "[obj_probe] first 3 vertices:"
  for i in [:3] do
    let x := pos[3*i]!
    let y := pos[3*i + 1]!
    let z := pos[3*i + 2]!
    IO.eprintln s!"  v{i} = ({x}, {y}, {z})"
  IO.eprintln "[obj_probe] last 3 vertices:"
  let n := nv.toNat
  for i in [n - 3 : n] do
    let x := pos[3*i]!
    let y := pos[3*i + 1]!
    let z := pos[3*i + 2]!
    IO.eprintln s!"  v{i} = ({x}, {y}, {z})"
  IO.eprintln "[obj_probe] first 2 triangles:"
  let nt := nf.toNat
  for t in [:min 2 nt] do
    let a := tris[12*t + 0]!.toNat ||| (tris[12*t + 1]!.toNat <<< 8) |||
             (tris[12*t + 2]!.toNat <<< 16) ||| (tris[12*t + 3]!.toNat <<< 24)
    let b := tris[12*t + 4]!.toNat ||| (tris[12*t + 5]!.toNat <<< 8) |||
             (tris[12*t + 6]!.toNat <<< 16) ||| (tris[12*t + 7]!.toNat <<< 24)
    let c := tris[12*t + 8]!.toNat ||| (tris[12*t + 9]!.toNat <<< 8) |||
             (tris[12*t + 10]!.toNat <<< 16) ||| (tris[12*t + 11]!.toNat <<< 24)
    IO.eprintln s!"  f{t} = ({a}, {b}, {c})"
  CassieObj.free h
