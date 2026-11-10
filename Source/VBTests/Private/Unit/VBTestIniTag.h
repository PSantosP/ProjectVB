// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

// 시험 전용 태그는 게임 태그 목록과 따로 Config/Tags/VBTests.ini 에 두고 쓸 때 찾는다. UncookedOnly 인 이 시험 모듈에 코드 태그
//   (UE_DEFINE_GAMEPLAY_TAG*)를 두면 엔진이 시작마다 모듈 형 ensure(NativeGameplayTags.cpp)를 내고 커맨드렛이 1 로 끝난다.
// 코드 태그처럼 FGameplayTag 자리에 그대로 쓴다. ini 에 없으면 RequestGameplayTag 가 시험 중에 크게 실패한다.
struct FVBTestIniTag
{
	const TCHAR* Name;
	operator FGameplayTag() const { return FGameplayTag::RequestGameplayTag(FName(Name)); }
};
