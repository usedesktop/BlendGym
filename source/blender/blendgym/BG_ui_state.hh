/* SPDX-FileCopyrightText: 2026 Usedesktop Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * BlendGym UI state exporter.
 *
 * This is intentionally gated behind an environment variable so normal Blender builds keep the
 * same runtime behavior unless a verifier/workbench process asks for UI-state evidence.
 */

#pragma once

#include "BLI_rect.hh"

namespace blender {

struct ARegion;
struct bContext;

namespace ui {
struct Block;
struct Button;
}

namespace blendgym {

void ui_state_capture_visible_button(const bContext *C,
                                     const ARegion *region,
                                     const ui::Block *block,
                                     const ui::Button *but,
                                     const rcti &rect);

void ui_state_process_pending_commands(const bContext *C);

void ui_state_record_button_dispatch_requested(const bContext *C, const ui::Button *but);

void ui_state_record_afterfunc_dispatch(const bContext *C,
                                        const char *label,
                                        const char *operator_id,
                                        int op_context,
                                        const char *rna_struct,
                                        const char *rna_property,
                                        bool operator_called,
                                        bool rna_updated);

}  // namespace blendgym
}  // namespace blender
