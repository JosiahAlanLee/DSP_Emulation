// SPDX-License-Identifier: AGPL-3.0-only
// The shared renderer (vcv/adapter) for the Monomachine's layout.
#pragma once
#include "Control.hpp"
#include "../../../adapter/src/Renderer.hpp"

namespace mnm_rack {
using emu_rack::Machine;
using Backend=emu_rack::Backend<Layout>;
using Renderer=emu_rack::Renderer<Layout>;
} // namespace mnm_rack
