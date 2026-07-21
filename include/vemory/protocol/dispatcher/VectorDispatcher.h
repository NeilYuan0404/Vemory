#pragma once

#include <string>

#include "vemory/protocol/RequestContext.h"

// Domain dispatcher for VADD / VSIM / VDIM / VEMB / VCARD.
// arg must be VectorSetRegistry*.
void VectorDispatcher(const RequestContext& ctx, std::string* reply, void* arg);
