# BlendGym Source Layout

BlendGym code lives under this directory to keep Usedesktop-specific RL environment work isolated
from upstream Blender code.

The intended boundary is:

- Upstream Blender files contain only minimal hooks needed to observe or drive Blender internals.
- BlendGym owns protocol handling, session/reset/snapshot state, UI/action export, and verifier
  execution.
- Workbench owns workflows, task division, golden trajectories, verifier graph composition, and
  reward mapping.

Current hook surface:

- `source/blender/editors/interface/interface.cc` calls `blendgym::ui_state_capture_visible_button`
  while drawing visible UI buttons.
- `source/blender/editors/interface/CMakeLists.txt` includes the BlendGym UI-state exporter source.

UI/action verifier export:

- Every exported visible action carries `verifier_ids` for compatibility and `verifier_bindings`
  for data-driven verifier graph construction. Consumers should use `verifier_bindings` for
  required/optional semantics.
- `verifier_bindings` maps reusable primitives such as `ui.visible`, `ui.enabled`,
  `ui.hit_bbox`, `ui.invoked`, `operator.invoked`, `rna.updated`, and `workspace.active`
  to concrete verifier ids for that action instance.
- UI primitives are recording-time evidence only. MCP replay should prefer semantic bindings such
  as `operator.invoked`, `rna.updated`, and scene-state verifiers over UI hit boxes or click
  invocation evidence.
- `catalog_verifier_bindings` gives the same primitive mapping at the stable catalog id level so
  Workbench can build a global action catalog while still using visible instance bindings for the
  current UI state.

This keeps upstream merge conflicts small: when Blender changes, most BlendGym code should remain in
this directory, and only the small hook surface may need manual adjustment.
