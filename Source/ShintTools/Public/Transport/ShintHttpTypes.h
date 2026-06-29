// Copyright 2026 ShintTools. All Rights Reserved.
//
// Shared HTTP types used by both the Transport layer (FShintHttpClient) and
// the Core client (FShintCoreClient). Lives in Transport/ — the
// lower-dependency layer — so defining EShintHttpMethod in one place avoids
// an ODR violation when a TU pulls in both Core and Transport headers.

#pragma once

#include "CoreMinimal.h"

enum class EShintHttpMethod : uint8 { GET, POST, PUT, DELETE_ };
