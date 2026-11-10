// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VBSubscriptionTestTypes.generated.h"

/** 신호를 들은 횟수만 세는 시험용 청취자. */
UCLASS(NotBlueprintable)
class UVBSubscriptionTestListener : public UObject
{
	GENERATED_BODY()

public:
	void Hear() { ++Heard; }

	int32 Heard = 0;
};
