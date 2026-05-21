// Copyright (C) Radioform / Original Authors
// Modified by Shankar (2026) for the KRISHA Architecture. Renamed namespaces and variables.
// Licensed under the GNU GPLv3.

/**
 * @file biquad.cpp
 * @brief Biquad filter implementation (no dependencies, clean RBJ cookbook)
 *
 * This file is intentionally minimal.
 * Coefficient math is implemented in biquad.h from the Audio EQ Cookbook formulas.
 */

#include "biquad.h"

// Implementation is header-only (in biquad.h) for inlining
// This file exists for explicit instantiation if needed later
