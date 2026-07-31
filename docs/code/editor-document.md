# Editor document and selection

`yorstudio/editor/editor_document.hpp` is the first editor-model boundary. It
owns a `yorengine::Scene`, not a second ECS, and exposes ordinary C++ values to
the UI adapter.

`SelectionModel` stores `yorengine::EntityId` values including their generation.
Invalid or destroyed entities are pruned, so a reused scene slot cannot select
an unrelated object accidentally. The current adapter uses single selection;
the model already keeps a vector for future multi-selection.

`EditorDocument` provides a tested command history for object creation, name
changes, and local transforms. Commands apply to the YorEngine scene, capture
their inverse state, and are removed from the redo branch when a new edit is
committed after undo. Failed engine mutations never enter history. The current
document is an in-memory editing document; scene-file serialization and saved
cursor tracking are the next scene-editor slice.

The ImGui adapter consumes `StudioUiFrame`/`StudioUiAction` values and never
passes ImGui types into the editor model. A fake UI port exercises the same
application path in the native smoke test.
