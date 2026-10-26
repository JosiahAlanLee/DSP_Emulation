// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// The pinned upstream header uses unqualified size_t but includes only vector
// and cstdint. GCC 11 does not transitively declare ::size_t through those.
// Supply the standard type without modifying the pinned dependency.
#include <cstddef>
using std::size_t;
#include "Resampler.h"
