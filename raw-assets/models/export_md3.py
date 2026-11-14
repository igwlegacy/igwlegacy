"""Export the visible mesh objects of a .blend as a static idTech 3 MD3.

Usage:
    blender --background model.blend --python export_md3.py -- output.md3

Conventions:
    * 1 Blender unit equals 1 Quake unit, +Z is up, the object origin is the
      model origin.
    * Every mesh object becomes one MD3 surface named after the object.
    * The name of the first material is written as the surface shader, so
      it must be the shader/texture path without extension.
    * Only a single static frame is written; no tags, no animation.
"""

import math
import struct
import sys

import bpy

MD3_IDENT = b"IDP3"
MD3_VERSION = 15
MD3_XYZ_SCALE = 1.0 / 64.0
MD3_MAX_VERTS = 4096
MD3_MAX_TRIANGLES = 8192


def _name(text, size):
    """Encode a NUL padded fixed-size string, rejecting names that truncate."""
    data = text.encode("ascii")
    if len(data) >= size:
        raise ValueError(f"name too long for {size} byte field: {text!r}")
    return data.ljust(size, b"\0")


def _encode_normal(n):
    """Pack a unit normal into idTech 3's latitude/longitude short.

    The renderer decodes the high byte as the azimuth around +Z and the low
    byte as the angle from +Z, both in 1/255 turns.
    """
    x, y, z = n
    if x == 0 and y == 0:
        return 0 if z > 0 else 128
    azimuth = int(round(math.atan2(y, x) * 255 / (2 * math.pi))) & 0xFF
    polar = int(round(math.acos(max(-1.0, min(1.0, z))) * 255 / (2 * math.pi))) & 0xFF
    return (azimuth << 8) | polar


def _surface(obj, depsgraph):
    """Collect unique (position, normal, uv) vertices and triangles."""
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    try:
        mesh.calc_loop_triangles()
        if not mesh.materials or mesh.materials[0] is None:
            raise ValueError(f"{obj.name}: needs a material named after its shader")
        uv_layer = mesh.uv_layers.active
        if uv_layer is None:
            raise ValueError(f"{obj.name}: needs a UV map")
        matrix = obj.matrix_world
        normal_matrix = matrix.to_3x3().inverted_safe().transposed()
        corner_normals = mesh.corner_normals

        verts, lookup, tris = [], {}, []
        for tri in mesh.loop_triangles:
            indices = []
            for loop_index in tri.loops:
                co = matrix @ mesh.vertices[mesh.loops[loop_index].vertex_index].co
                normal = (normal_matrix @ corner_normals[loop_index].vector).normalized()
                u, v = uv_layer.data[loop_index].uv
                # MD3 texture coordinates have t growing downwards.
                key = (tuple(round(c, 5) for c in co), tuple(round(c, 4) for c in normal),
                       round(u, 6), round(1.0 - v, 6))
                if key not in lookup:
                    lookup[key] = len(verts)
                    verts.append((co.copy(), normal.copy(), (u, 1.0 - v)))
                indices.append(lookup[key])
            # idTech 3 expects clockwise front faces, Blender stores them CCW.
            tris.append((indices[0], indices[2], indices[1]))
        if len(verts) > MD3_MAX_VERTS or len(tris) > MD3_MAX_TRIANGLES:
            raise ValueError(f"{obj.name}: exceeds MD3 vertex/triangle limits")
        return obj.name, mesh.materials[0].name, verts, tris
    finally:
        evaluated.to_mesh_clear()


def _pack_surface(name, shader, verts, tris):
    header_size = 108
    ofs_shaders = header_size
    ofs_triangles = ofs_shaders + 68
    ofs_st = ofs_triangles + 12 * len(tris)
    ofs_xyz = ofs_st + 8 * len(verts)
    ofs_end = ofs_xyz + 8 * len(verts)

    out = bytearray()
    out += MD3_IDENT + _name(name, 64)
    out += struct.pack("<10i", 0, 1, 1, len(verts), len(tris),
                       ofs_triangles, ofs_shaders, ofs_st, ofs_xyz, ofs_end)
    out += _name(shader, 64) + struct.pack("<i", 0)
    for tri in tris:
        out += struct.pack("<3i", *tri)
    for _, _, st in verts:
        out += struct.pack("<2f", *st)
    for co, normal, _ in verts:
        xyz = [int(round(c / MD3_XYZ_SCALE)) for c in co]
        if any(not -32768 <= c <= 32767 for c in xyz):
            raise ValueError(f"{name}: vertex outside the MD3 coordinate range")
        out += struct.pack("<3hH", *xyz, _encode_normal(normal))
    assert len(out) == ofs_end
    return bytes(out)


def export(path):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    objects = sorted((o for o in bpy.context.scene.objects
                      if o.type == "MESH" and o.visible_get()), key=lambda o: o.name)
    if not objects:
        raise ValueError("no visible mesh objects to export")
    surfaces = [_surface(o, depsgraph) for o in objects]

    points = [co for _, _, verts, _ in surfaces for co, _, _ in verts]
    mins = [min(p[i] for p in points) for i in range(3)]
    maxs = [max(p[i] for p in points) for i in range(3)]
    radius = max(p.length for p in points)

    header_size, frame_size = 108, 56
    ofs_frames = header_size
    ofs_tags = ofs_frames + frame_size
    ofs_surfaces = ofs_tags
    packed = [_pack_surface(*s) for s in surfaces]
    ofs_end = ofs_surfaces + sum(len(p) for p in packed)

    out = bytearray()
    out += MD3_IDENT + struct.pack("<i", MD3_VERSION) + _name(bpy.path.basename(path), 64)
    out += struct.pack("<9i", 0, 1, 0, len(packed), 0,
                       ofs_frames, ofs_tags, ofs_surfaces, ofs_end)
    out += struct.pack("<3f3f3ff", *mins, *maxs, 0.0, 0.0, 0.0, radius)
    out += _name("frame0", 16)
    for p in packed:
        out += p
    assert len(out) == ofs_end
    with open(path, "wb") as handle:
        handle.write(out)
    return {"surfaces": len(packed), "bytes": len(out)}


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(args) != 1:
        raise SystemExit("usage: blender -b model.blend --python export_md3.py -- out.md3")
    print(export(args[0]))
