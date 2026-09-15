// Verification for tmd::TmdDocument (src/core/tmd_document.h/.cpp) - the
// TMD-side counterpart to tim::Document, extracted out of TmdPanel so its
// model-list/undo-redo/save logic can be exercised directly, with no ImGui/
// GL involved. Writes small real .tmd files to the test's own working
// directory (like test_tinygltf.cpp's temp file) since SaveActiveModel/
// LoadFile round-trip through actual disk I/O.
#include "core/tmd_document.h"
#include "core/tmd_mesh_ops.h"
#include <cstdio>

int main() {
    int failures = 0;
    auto Check = [&](bool ok, const char* what) {
        printf("%s %s\n", ok ? "OK" : "FAILED", what);
        if (!ok) failures++;
    };

    // --- NewModelAt / AddObject ---
    {
        tmd::TmdDocument doc;
        doc.NewModelAt("tmd_document_test_a.tmd");
        Check(doc.HasActiveModel(), "NewModelAt makes a new model active");
        Check(doc.ActiveModelDirty(), "a brand-new model starts dirty (nothing written to disk yet)");
        Check(doc.ActiveModelFilename() == "tmd_document_test_a.tmd", "NewModelAt sets the filename");
        Check(!doc.HasActiveObject(), "a brand-new model has no objects, so no active object either");

        doc.AddObject(doc.ActiveModel());
        Check(doc.HasActiveObject(), "AddObject makes the new object active");
        Check(doc.Models()[doc.ActiveModel()].model.objects.size() == 1, "AddObject appends exactly one object");
        Check(doc.Models()[doc.ActiveModel()].model.objects[0].vertices.empty(),
              "a freshly added object starts with no geometry");

        // Re-opening the same path (e.g. the user picks the same "New TMD"
        // path twice) must re-select it, not create a duplicate entry.
        doc.NewModelAt("tmd_document_test_b.tmd");
        size_t count_before = doc.Models().size();
        doc.NewModelAt("tmd_document_test_a.tmd");
        Check(doc.Models().size() == count_before, "NewModelAt on an already-open path doesn't duplicate it");
        Check(doc.ActiveModelFilename() == "tmd_document_test_a.tmd", "NewModelAt on an already-open path re-selects it");
    }

    // --- PushUndo / Undo / Redo ---
    {
        tmd::TmdDocument doc;
        doc.NewModelAt("tmd_document_test_undo.tmd");
        doc.AddObject(doc.ActiveModel());
        int mi = doc.ActiveModel(), oi = doc.ActiveObject();
        tmd::TMD_Object& obj = doc.Models()[mi].model.objects[oi];

        doc.PushUndo(mi, oi);
        tmd::AddVertex(obj, 10, 20, 30);
        Check(obj.vertices.size() == 1, "the edit under test actually applied");
        Check(doc.CanUndo(), "CanUndo is true right after PushUndo+an edit");
        Check(!doc.CanRedo(), "CanRedo is false before any Undo");

        doc.Undo();
        Check(doc.Models()[mi].model.objects[oi].vertices.empty(), "Undo reverts the vertex add");
        Check(doc.CanRedo(), "CanRedo becomes true right after Undo");

        doc.Redo();
        Check(doc.Models()[mi].model.objects[oi].vertices.size() == 1, "Redo reapplies the vertex add");
        Check(!doc.CanRedo(), "Redo drains CanRedo back to false");

        // A fresh edit after Undo must clear the (now-stale) redo history,
        // same as tim::Document's Undo/Redo design.
        doc.Undo();
        doc.PushUndo(mi, oi);
        tmd::AddVertex(doc.Models()[mi].model.objects[oi], 1, 2, 3);
        Check(!doc.CanRedo(), "a new edit after Undo clears the stale redo entry");
    }

    // --- CloseModel ---
    {
        tmd::TmdDocument doc;
        doc.NewModelAt("tmd_document_test_close_a.tmd");
        doc.NewModelAt("tmd_document_test_close_b.tmd");
        int b_index = doc.ActiveModel();
        Check(doc.Models().size() == 2, "two distinct paths produce two models");

        doc.CloseModel(0); // closes "a", which sits before "b"
        Check(doc.Models().size() == 1, "CloseModel removes exactly one model");
        Check(doc.Models()[0].model.filename == "tmd_document_test_close_b.tmd", "the remaining model is the right one");
        Check(doc.ActiveModel() == b_index - 1, "closing a model before the active one shifts its index down");
    }

    // --- SaveActiveModelAs / LoadFile round-trip ---
    {
        tmd::TmdDocument doc;
        doc.NewModelAt("tmd_document_test_save.tmd");
        doc.AddObject(doc.ActiveModel());
        tmd::AddVertex(doc.Models()[doc.ActiveModel()].model.objects[0], 5, -5, 5);

        Check(doc.SaveActiveModelAs("tmd_document_test_saved.tmd"), "SaveActiveModelAs succeeds");
        Check(!doc.ActiveModelDirty(), "saving clears the dirty flag");
        Check(doc.ActiveModelFilename() == "tmd_document_test_saved.tmd", "SaveActiveModelAs re-points the filename");

        tmd::TmdDocument reloaded;
        Check(reloaded.LoadFile("tmd_document_test_saved.tmd"), "LoadFile loads what was just saved");
        Check(reloaded.Models()[0].model.objects.size() == 1, "the saved object count round-trips");
        Check(reloaded.Models()[0].model.objects[0].vertices.size() == 1, "the saved vertex count round-trips");

        Check(!reloaded.LoadFile("tmd_document_test_saved.tmd"), "LoadFile on an already-open path is a no-op");
        Check(reloaded.Models().size() == 1, "...and doesn't create a duplicate entry");
    }

    // --- AnyModelDirty / SaveAllDirtyModels ---
    {
        tmd::TmdDocument doc;
        doc.NewModelAt("tmd_document_test_dirty_a.tmd");
        doc.NewModelAt("tmd_document_test_dirty_b.tmd");
        Check(doc.AnyModelDirty(), "two brand-new models are both dirty");

        doc.SaveAllDirtyModels();
        Check(!doc.AnyModelDirty(), "SaveAllDirtyModels clears every model's dirty flag");
    }

    printf(failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
