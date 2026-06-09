// Copyright 2026 ShintTools. All Rights Reserved.
//
// Shared HTTP types used by both the Transport layer (FShintHttpClient) and
// the legacy monolith (FShintCoreClient). Lives in Transport/ because that's
// the lower-dependency layer — Core can pull from Transport, the reverse
// would create a cycle once Sprint 2's Api/* classes start depending on
// Transport without dragging in the monolith.
//
// Defining EShintHttpMethod in only one place avoids the ODR violation that
// would surface the moment any TU pulls in both Core/ShintCoreClient.h and
// Transport/FShintHttpClient.h (planned for Sprint 2's Api/* code that uses
// FShintCodeIssue from the monolith while sending requests through the new
// transport).

#pragma once

#include "CoreMinimal.h"

enum class EShintHttpMethod : uint8 { GET, POST, PUT, DELETE_ };
