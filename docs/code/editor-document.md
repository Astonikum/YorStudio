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
committed after undo. Failed engine mutations never enter history.

Scene documents are version-1 JSON files. Each object has a persistent UUID,
name, active state, local transform, and optional parent UUID. Loading validates
the complete hierarchy before mutating the in-memory scene. Saving writes a
temporary sibling file and atomically replaces the scene, retaining unknown
top-level and object fields so newer editor data is not silently destroyed.
The saved history cursor drives `StudioUiFrame::sceneDirty`.

Hierarchy edits operate on the YorEngine scene directly. Delete and duplicate
commands include the selected subtree, preserve persistent object GUIDs and
extension data, and restore fresh runtime `EntityId` values on undo/redo.
Parenting rejects self/descendant cycles through the engine contract; active
state changes are also undoable. The editor exposes hierarchy parent identity
through the UI port without leaking YorEngine or ImGui types into the adapter.
The ImGui adapter presents this data as a collapsible tree and keeps
expanded/collapsed state local to the adapter.

Tags and the 0-31 layer index are native YorEngine metadata exposed through
the document model. They are validated before scene replacement, serialized as
version-1 object fields, and edited through undoable inspector commands.

Native camera authoring is persisted under each object's `components` object:
`camera` stores the Brain lens/channel, `camera_key_point` stores priority,
blend/lens, GUID-based follow/look-at targets and explicit offset spaces, and
`camera_noise` stores deterministic shake parameters. Loading validates these
components on a temporary YorEngine scene before replacing the live document;
duplicate/undo restoration resolves internal and external GUID targets, while
unknown component fields remain preserved on save.

The camera inspector exposes this state through plain `StudioUiFrame` and
`StudioUiAction` values. Add/remove/set operations are routed through
`EditorDocument` commands, so validation and undo/redo stay independent from
Dear ImGui; target pickers write stable object GUIDs rather than transient
entity indices.

Light authoring follows the same contract under `components.light`: kind,
color, intensity, range, and cone limits are validated by YorEngine before a
command is committed. Kind changes replace the native Light with an undoable
command so its immutable native kind stays authoritative.

The ImGui adapter consumes `StudioUiFrame`/`StudioUiAction` values and never
passes ImGui types into the editor model. A fake UI port exercises the same
application path in the native smoke test.
