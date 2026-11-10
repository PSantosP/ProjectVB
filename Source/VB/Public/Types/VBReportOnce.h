// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

// 상태 없는 기본 객체(큐, 능력 CDO)가 같은 저작 문제를 대상마다 한 번만 말하게 한다. 대상은 경로 이름으로 가른다. 게임 스레드에서만.
inline bool VBFirstReportFor(const UObject* Subject)
{
	static TSet<FString> Reported;
	bool bAlreadyReported = false;
	Reported.Add(GetPathNameSafe(Subject), &bAlreadyReported);
	return !bAlreadyReported;
}
