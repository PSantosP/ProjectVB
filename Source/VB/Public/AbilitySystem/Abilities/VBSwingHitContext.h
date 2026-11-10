// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "AbilitySystem/VBHitImpact.h" // EVBHitImpact - 스윙의 반응 등급을 값으로 든다
#include "Data/VBSwingCombatProfile.h" // FVBSwingCombatScales - 스윙 배율을 값으로 든다
#include "Types/VBWeaponStateTypes.h" // EVBWeaponType - 휘두른 무기를 값으로 든다

class UVBSwingImpactProfile;

/** 스윙 하나의 무게와 방향. 창이 열릴 때 한 번 정해 창 동안 고정한다(옛 구 스윕도 같은 구조체). */
struct FVBSwingHitContext
{
	// 여러 부위 창에서 부위마다의 모양과 큐. 맞은 쪽의 MyBoneName 으로 찾는다. 옛 구 판정은 비어 있다.
	struct FPart
	{
		FName Bone;
		FGameplayTag Shape;
		FGameplayTag Cue;
	};

	// 조준 보정된 수평 정면(넉백 방향).
	FVector Forward = FVector::ForwardVector;
	float KnockbackYaw = 0.0f;
	FVBSwingCombatScales SwingScales;
	EVBHitImpact SwingImpact = EVBHitImpact::Light;
	// 이 스윙의 띄우는 높이(cm).
	float SwingLaunchHeight = 0.0f;
	float AttackPower = 0.0f;
	// 휘두르는 순간 든 무기와 강화 배율(서버) - 피해 사슬의 다섯째 층.
	EVBWeaponType WeaponType = EVBWeaponType::None;
	float WeaponEnhanceScale = 1.0f;
	float FinalDamage = 0.0f;
	// 이 창의 날이 정한 타격 큐(비면 공격 설정의 큐)와 때린 부위(첫 부위).
	FGameplayTag ImpactCueTag;
	FGameplayTag StrikeShape;
	TArray<FPart> Parts;
	// 이 스윙이 쓰는 노티 프로필(다른 창이 덮어써도 그대로).
	TWeakObjectPtr<const UVBSwingImpactProfile> ImpactProfile;
	TWeakObjectPtr<const UVBSwingCombatProfile> CombatProfile;
	// 스윙당 한 번 항목(자동 락온, 재중심, 히트스톱, 쉐이크)을 냈는지 표시한다.
	bool bAutoLocked = false;
	bool bCameraReported = false;
	bool bContactFinished = false;
};
