// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CharacterCommon/VBDeathFacts.h"
#include "VBDeathSteps.generated.h"

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UVBDeathSteps : public UInterface
{
	GENERATED_BODY()
};

/**
 * 죽음 단계 중 캐릭터가 맡는 부분. UVBHealthComponent 가 순서를 관리하고 정해진 시점에 부른다(서버 전용).
 * 순서가 계약이라 델리게이트가 아니라 인터페이스로 둔다.
 */
class IVBDeathSteps
{
	GENERATED_BODY()

public:
	// 능력 취소 전. 취소가 걷어 가는 상태(엎드림 태그 등)를 읽거나, 취소 전에 끝나야 하는 것을 끝낸다.
	virtual void BeforeDeathTag(const FVBDeathFacts& Facts) {}

	// State.Dead 와 능력 취소 뒤, 표시 전. 입력, 락온, 전투 등록처럼 표시보다 먼저 풀려야 하는 것.
	virtual void AfterDeathTag(const FVBDeathFacts& Facts) {}

	// 사망 표시(멀티캐스트, 복제 상태). 이것이 돌아온 뒤 알림이 나간다.
	virtual void PresentDeath(const FVBDeathFacts& Facts) = 0;
};
