// Entry points for builds without a hazard cloud renderer backend: the tests
// that link no device. The game links SDL GPU's or Direct3D 11's instead.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "Platform/HazardCloud.h"

namespace HazardCloud {

bool Supported() { return false; }
std::unique_ptr<Layer> CreateLayer() { return nullptr; }
void ReleaseDevice() {}

} // namespace HazardCloud
