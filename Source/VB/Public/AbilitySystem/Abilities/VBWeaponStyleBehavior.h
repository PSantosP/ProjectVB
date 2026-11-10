// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VBWeaponStyleBehavior.generated.h"

class UVBGA_MeleeAttackBase;

/**
 * 무기 스타일 고유 동작 전략. UVBAttackConfig 에 인라인 인스턴스로 붙어 공격 GA 가 호출한다.
 * 데이터로 표현 못 하는 메커닉만 둔다. null 이면 기본 근접 판정을 쓴다.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced)
class VB_API UVBWeaponStyleBehavior : public UObject
{
	GENERATED_BODY()

public:
	// 타격 전달 훅(서버). true 면 이 Behavior 가 처리해 기본 판정을 건너뛰고, false 면 기본 판정을 한다.
	virtual bool DeliverHit(UVBGA_MeleeAttackBase* Ability) { return false; }
};
