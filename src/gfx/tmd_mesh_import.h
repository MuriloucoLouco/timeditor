#pragma once
#include "../core/tmd_format.h"
#include <string>
#include <vector>

namespace gfx {

struct MeshVec3 {
    float x = 0, y = 0, z = 0;
};
struct MeshVec2 {
    float x = 0, y = 0;
};

// One corner of a source-format face, before any tri/quad splitting -
// indices into the owning GenericMeshObject's positions/uvs/normals
// arrays (-1 for uv/normal when that attribute isn't present).
struct MeshCorner {
    int position_idx = 0;
    int uv_idx = -1;
    int normal_idx = -1;
};

// Splits an arbitrary n-sided face's corners (n >= 3, already known not to
// be a plain triangle/quad) into a sequence of 3- or 4-corner groups, each
// still in the same *perimeter* winding order the source corners were in:
// fan-triangulate from corner 0, then merge consecutive pairs of those fan
// triangles back into quads (sharing their diagonal), leaving one triangle
// unmerged if the count is odd. A pentagon becomes 1 quad + 1 triangle, a
// hexagon becomes 2 quads, and so on - no convexity check, just geometry-
// agnostic index bookkeeping (good enough for reconstructing a TMD's
// tri/quad-only primitive list from a general mesh face).
std::vector<std::vector<MeshCorner>> SplitFaceCorners(const std::vector<MeshCorner>& corners);

// One already tri/quad-sized face, corners in perimeter order.
struct GenericFace {
    int num_verts = 3; // 3 or 4
    MeshCorner corners[4];
    bool has_color = false;
    MeshVec3 color; // 0..1 range, this face's own flat color, when has_color
};

// A generic mesh replacing one TMD object: positions/normals in the source
// format's own space (Y-up, as literally read from OBJ/glTF) - the Y-flip
// into TMD's native Y-down space happens once, centrally, in
// RebuildTmdObject. UVs are normalized 0..1.
struct GenericMeshObject {
    int object_index = -1; // which TMD object this replaces
    std::vector<MeshVec3> positions;
    std::vector<MeshVec2> uvs;
    std::vector<MeshVec3> normals;
    std::vector<GenericFace> faces;
};

struct MeshImportStats {
    int object_index = -1;
    bool exists_in_model = false;
    bool matched_by_geometry = false; // resolved by shape, not by its "ObjectN" name
    int face_count = 0;
    int vertex_count = 0;
    int ngon_splits = 0; // how many source faces had >4 sides and got split
};

// Parses the object index out of an "o"/node name written by this app's
// own exporters (e.g. "Object42" -> 42), anchored on the "Object" prefix
// rather than scanned from the end of the string - scanning from the end
// would misread a numeric duplicate-name suffix a DCC tool appends on a
// collision (Blender turns a second "Object42" into "Object42.001") as a
// different index entirely. Returns -1 if the name doesn't start with
// "Object" followed by at least one digit.
int ParseTmdObjectIndex(const std::string& name);

// For any GenericMeshObject whose object_index wasn't recognized (renamed,
// merged, or missing outright) or names an object outside the model's
// range, finds the best-matching *unclaimed* TMD object by comparing
// bounding-box center/extent and vertex count - external tools don't
// always preserve object names, but the geometry itself usually still
// resembles whatever TMD object it replaces. Matches are assigned
// greedily, closest-scoring pair first, so two ambiguous groups can never
// both claim the same object; anything left with no unclaimed candidate
// keeps its original (unresolved) index, so it's still honestly reported
// as skipped rather than risk overwriting the wrong object. Returns one
// bool per `meshes` entry (same order), true where geometry - not the
// name - decided the match.
std::vector<bool> ResolveUnmatchedObjects(std::vector<GenericMeshObject>& meshes, const tmd::TMD_Model& model);

// Rebuilds a TMD_Object's entire vertex/normal/primitive tables from
// `mesh`. Every primitive comes out flat-shaded (one shared normal, never
// gouraud/no_light) using the face's own imported normal data (or a
// computed geometric fallback if none was present):
//   - a face with UV data on every corner -> textured, tsb=cba=0 (to be
//     reassigned by hand afterward), u/v scaled directly from the UV
//   - else a face with its own flat color -> that color, untextured
//   - else -> a fixed placeholder purple, untextured
// Corners are expected in perimeter order (as SplitFaceCorners produces,
// and as OBJ/glTF naturally give you); the perimeter<->triangle-strip
// conversion for quads happens inside this function.
tmd::TMD_Object RebuildTmdObject(const GenericMeshObject& mesh, MeshImportStats& stats_out);

} // namespace gfx
