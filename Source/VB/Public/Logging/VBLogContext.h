#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/EngineTypes.h"

enum class EVBLogLocality : uint8 { Unknown, Local, Remote, NotApplicable };
enum class EVBLogPIE : uint8 { Unknown, None, Instance };

// 관찰용일 뿐이다 - 값이 없다고 standalone, authority, local 소유로 읽지 않는다
struct VB_API FVBLogContext
{
	TOptional<ENetMode> NetMode;
	TOptional<ENetRole> Role;
	EVBLogLocality Locality = EVBLogLocality::Unknown;
	EVBLogPIE PIE = EVBLogPIE::Unknown;
	int32 PIEInstance = INDEX_NONE;
};

VB_API FVBLogContext VBResolveLogContext(const UObject* Context);
VB_API FString VBFormatLogContext(const FVBLogContext& Context);
VB_API FString VBGetLogContext(const UObject* Context);
