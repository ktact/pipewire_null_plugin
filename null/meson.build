# SPA Null Plugin Build Configuration
# SPDX-FileCopyrightText: Copyright © 2024
# SPDX-License-Identifier: MIT

# Null plugin source files
null_sources = [
  'null.c',
  'null-sink.c',
]

# Null plugin dependencies
null_deps = [
  pipewire_dep,
  spa_dep,
  mathlib,
]

# Build null plugin as shared library
null_lib = shared_library('spa-null',
  null_sources,
  include_directories : inc_dirs,
  dependencies : null_deps,
  install : true,
  install_dir : spa_plugindir / 'null',
  name_prefix : '',  # Don't add 'lib' prefix
)