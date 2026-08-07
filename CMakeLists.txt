# SPDX-License-Identifier: MIT

if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_GATED_MOUSE_MOVE app PRIVATE
    src/behaviors/behavior_gated_mouse_move.c
  )
endif()
