// SPDX-License-Identifier: GPL-3.0-or-later
// ProsperoEden's paths for the self-update kit (update_notice.cpp): 0 the helper in the app's
// folder, 1 the app's param.json, 2 the file that keeps the catalog's highest sequence.
#pragma once
#ifdef __cplusplus
extern "C"
#endif
const char *eden_self_update_path(int which);
