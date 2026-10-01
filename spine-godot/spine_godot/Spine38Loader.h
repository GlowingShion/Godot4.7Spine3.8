/******************************************************************************

 * Spine Runtimes License Agreement

 * Last updated April 5, 2025. Replaces all prior versions.

 *

 * Copyright (c) 2013-2025, Esoteric Software LLC

 *

 * Spine38Loader: loads Spine 3.8 editor exports (.skel / .json) into the 4.3

 * runtime by parsing them with an embedded, namespaced copy of the official

 * 3.8 spine-cpp runtime (spine38::) and translating the resulting

 * spine38::SkeletonData into a native spine::SkeletonData.

 *****************************************************************************/

#ifndef SPINE_GODOT_SPINE38LOADER_H
#define SPINE_GODOT_SPINE38LOADER_H

#ifdef SPINE_GODOT_EXTENSION

#include <spine/spine.h>

namespace Spine38Loader {

/// Returns true if the binary data is a Spine 3.x skeleton binary (3.8 only).
bool sniffs_binary_38(const uint8_t *data, size_t length, char *versionOut, size_t versionOutSize);

/// Returns true if the json text is a Spine 3.8 skeleton json.
bool sniffs_json_38(const char *json, char *versionOut, size_t versionOutSize);

/// Parses a 3.8 binary skeleton and translates it to the 4.3 data model.
/// Returns null and fills errorOut on failure.
spine::SkeletonData *load_binary_38(const uint8_t *data, size_t length, spine::Atlas &atlas, char *errorOut, size_t errorOutSize);

/// Parses a 3.8 json skeleton and translates it to the 4.3 data model.
spine::SkeletonData *load_json_38(const char *json, spine::Atlas &atlas, char *errorOut, size_t errorOutSize);

} // namespace Spine38Loader

#endif // SPINE_GODOT_EXTENSION

#endif // SPINE_GODOT_SPINE38LOADER_H
