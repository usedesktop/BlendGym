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

This keeps upstream merge conflicts small: when Blender changes, most BlendGym code should remain in
this directory, and only the small hook surface may need manual adjustment.
