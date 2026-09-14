#pragma once
#include "tmd_format.h"
#include <vector>

// Pure data-layer mesh surgery on a tmd::TMD_Object - add/delete/merge/
// extrude/duplicate vertices and primitives. No ImGui/GL dependency (this
// stays in core/, not gfx/, since it's just table manipulation on the
// format's own structs), so it's testable the same standalone way
// gfx::SplitFaceCorners/RebuildTmdObject already are.
//
// Positions passed in or handed back are always in TMD's own native space
// (int16, Y-down) - the UI layer is responsible for converting to/from
// whatever "viewer space" (Y-up) it renders in, exactly like every other
// piece of this codebase that touches raw TMD_Vertex/TMD_Normal data.
//
// A recurring rule followed throughout: an operation that would otherwise
// mutate a normal or vertex table entry that other, untouched primitives
// might still be referencing (a very real possibility - TMD normals in
// particular are commonly shared across adjacent faces) always allocates a
// fresh entry instead of mutating in place, so one primitive's edit can
// never silently corrupt a sibling that happens to alias the same index.
// Deleting/merging *is* allowed to repoint shared references, since that's
// the explicit point of those two operations.
namespace tmd {

// One undirected edge between two vertex indices, normalized so `a <= b`
// (use MakeEdge to construct one correctly) so it can be used as a
// dedup/lookup key regardless of which direction a primitive's winding
// traversed it in.
struct Edge {
    uint16_t a = 0, b = 0;
    bool operator==(const Edge& o) const { return a == o.a && b == o.b; }
    bool operator<(const Edge& o) const { return a != o.a ? a < o.a : b < o.b; }
};
inline Edge MakeEdge(uint16_t v0, uint16_t v1) { return v0 <= v1 ? Edge{ v0, v1 } : Edge{ v1, v0 }; }

// One deduplicated edge plus which of the considered primitives use it -
// exactly one means it's on the *boundary* of that set of primitives, two
// or more means it's interior to it. Edges are derived on the fly from
// each primitive's own perimeter (tmd::kQuadPerimeterOrder for quads,
// corners 0-1-2 directly for tris) - TMD stores no edge list of its own.
struct EdgeUsage {
    Edge edge;
    std::vector<int> primitive_indices;
};

// Builds the edge list implied by `primitive_indices`'s own perimeters -
// pass an empty vector (the default) for every primitive in `obj`. Used
// both as a general "what edges exist" query and, with a specific
// selection, by ExtrudeFaces to find that selection's boundary.
std::vector<EdgeUsage> BuildEdgeList(const TMD_Object& obj, const std::vector<int>& primitive_indices = {});

// --- Deletion ---

// Removes the given primitives outright; their vertices/normals are left
// in the table, possibly now unreferenced (see RemoveUnusedVerticesAndNormals).
// Indices need not be sorted or unique.
void DeleteFaces(TMD_Object& obj, const std::vector<int>& primitive_indices);

// Removes the given vertices AND every primitive that references any of
// them (a face can't survive losing one of its own corners), then compacts
// the remaining vertex indices so every surviving primitive's vert_idx
// stays valid. Returns how many primitives were cascaded away.
int DeleteVertices(TMD_Object& obj, const std::vector<int>& vertex_indices);

// --- Merging ---

enum class MergeTarget {
    Average, // the merged vertex sits at the average of the given positions
    Last,    // the merged vertex keeps the position of vertex_indices.back()
};

// Merges 2+ vertices into one (the first index given survives and is
// repositioned per `target`; the others become unreferenced, not removed -
// run RemoveUnusedVerticesAndNormals afterward if desired), remapping every
// primitive's vert_idx that pointed at a now-merged index. Any primitive
// that ends up with two identical corners as a result (i.e. loses a
// distinct corner) is dropped rather than left degenerate. Returns how many
// primitives were dropped. No-ops (returns 0) if fewer than 2 indices given.
int MergeVertices(TMD_Object& obj, const std::vector<int>& vertex_indices, MergeTarget target);

// --- Construction ---

// Appends one new vertex at the given position (TMD native space, rounded
// and clamped to int16 range) and returns its index.
int AddVertex(TMD_Object& obj, float x, float y, float z);

// Builds one new flat, untextured (placeholder purple, matching
// gfx::RebuildTmdObject's own "freshly built face" convention) tri/quad
// primitive from exactly 3 or 4 existing vertex indices, given in
// perimeter (outline) order - converted internally to TMD's native strip
// storage order for a quad via tmd::kQuadPerimeterOrder. A fresh shared
// normal is computed from the winding (see RecalculateNormal). Returns the
// new primitive's index, or -1 if given anything other than 3 or 4 indices
// or any index is out of range.
int AddFace(TMD_Object& obj, const std::vector<int>& vertex_indices_perimeter_order);

// --- Duplication ---

struct DuplicateResult {
    std::vector<int> new_vertex_indices;    // parallel to the input vertex_indices
    std::vector<int> new_primitive_indices; // parallel to the input primitive_indices
};

// Copies the given vertices and primitives into new table entries (the
// primitives must only reference vertices already included in
// `vertex_indices` - a face partially outside the given vertex set would
// have nothing valid to remap one of its corners to). Duplicated
// primitives keep referencing their *original* normal entries (untouched,
// possibly now shared with the source) rather than forking new ones - if
// the duplicate gets moved/reoriented, run RecalculateNormal on it
// afterward. The caller (a "Duplicate" or the basis for "Extrude") then
// moves/uses the copies via the returned indices.
DuplicateResult DuplicateSelection(TMD_Object& obj, const std::vector<int>& vertex_indices,
                                    const std::vector<int>& primitive_indices);

// --- Extrusion ---

struct ExtrudeResult {
    std::vector<int> new_vertex_indices; // the extruded duplicate, parallel to the affected old vertices
    std::vector<int> new_wall_primitive_indices; // newly created side-wall quads only
};

// Extrudes the given faces as one region: every vertex used by any of them
// is duplicated once (a vertex shared by two selected faces gets exactly
// one duplicate serving both, not two), each *original* primitive is then
// re-pointed to reference the new, duplicated vertices - so the original
// primitive indices become the moved "cap" and stay valid for the caller's
// existing selection to keep dragging - and one new quad "wall" is built
// per boundary edge of the selection (an edge used by exactly one of the
// given primitives; an edge shared by two selected faces is interior to
// the extruded region and gets no wall, matching a face-region extrude in
// any modeling tool). Every new wall gets the same flat placeholder-purple
// treatment AddFace gives brand-new geometry. `new_vertex_indices` is
// parallel to the deduplicated set of old vertices those primitives used,
// in first-seen order - move them (or just call ApplyDelta on them) to
// actually pull the extrusion out.
ExtrudeResult ExtrudeFaces(TMD_Object& obj, const std::vector<int>& primitive_indices);

// Extrudes a single edge into one new quad (old vertex_a/vertex_b on one
// side, their fresh duplicates on the other) - unlike ExtrudeFaces, this
// doesn't require the edge to belong to any existing face; it's a general
// "grow a new quad from this edge" tool. Returns the two new vertex
// indices (duplicate of a, then of b) and the one new wall primitive's
// index via the same ExtrudeResult shape.
ExtrudeResult ExtrudeEdge(TMD_Object& obj, int vertex_a, int vertex_b);

// --- Normals ---

// Negates every distinct normal entry the primitive's corners reference -
// allocates fresh, flipped copies rather than mutating in place, so a
// normal shared with an untouched sibling primitive is never affected.
void FlipNormal(TMD_Object& obj, int primitive_index);

// Recomputes one fresh shared normal from the primitive's own winding
// (cross product of its first three corners in storage order, normalized
// and scaled to the GTE's 4096-per-unit convention) and points every one
// of its corners at that new entry - i.e. resets the face to flat shading
// from its current geometry. Does not touch whatever normal entry it used
// to reference (which may still be shared elsewhere).
void RecalculateNormal(TMD_Object& obj, int primitive_index);

// --- Cleanup ---

struct CleanupResult {
    int vertices_removed = 0;
    int normals_removed = 0;
};

// Removes every vertex/normal no primitive references anymore, compacting
// and remapping every remaining primitive's vert_idx/norm_idx accordingly.
// Safe to call any time - a good "tidy up" step after a run of edits.
CleanupResult RemoveUnusedVerticesAndNormals(TMD_Object& obj);

} // namespace tmd
