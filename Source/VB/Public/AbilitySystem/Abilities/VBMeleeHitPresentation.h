// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class ACharacter;
class UAbilitySystemComponent;
class UObject;
class UVBAttackConfig;
class UVBTargetLockComponent;
struct FVBSwingHitContext;

// 스윙이 맞은 뒤의 화면 쪽 반응. 피해와 무관하게 카메라와 쉐이크만 다룬다. 히트스톱은 능력 함수라 능력이 직접 부른다.
namespace VBMeleeHitPresentation
{
	// 자동 락온과 카메라 재중심을 스윙당 한 번 알린다. 락온이 없는 아바타는 표시만 남긴다.
	VB_API void ReportHitToTargetLock(UVBTargetLockComponent* TargetLock, FVBSwingHitContext& Context, AActor* HitActor,
		bool bDealtDamage);

	// 스윙 첫 접촉의 방향 쉐이크 또는 강타 화면 충격을 낸다.
	VB_API void PlaySwingImpact(const UObject& Ability, const FVBSwingHitContext& Context, const UVBAttackConfig& Config,
		ACharacter& Avatar, UAbilitySystemComponent& OwnerASC);
}
