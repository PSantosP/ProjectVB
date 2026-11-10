// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGA_Guard.h"
#include "VBGA_GuardShield.generated.h"

/** 방패 가드만 식별한다. 패시브 요구는 전투 규칙 표가 맡고 가드 실행은 부모와 공유한다. */
UCLASS(Abstract)
class VB_API UVBGA_GuardShield : public UVBGA_Guard
{
	GENERATED_BODY()

public:
	UVBGA_GuardShield();
};
