// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "VBTargetable.generated.h"

class AActor;

UINTERFACE(MinimalAPI)
class UVBTargetable : public UInterface
{
	GENERATED_BODY()
};

class IVBTargetable
{
	GENERATED_BODY()

public:
	// 이 액터가 현재 타겟팅 가능한 상태인가?
	virtual bool IsTargetable() const = 0;
	// 액터만 있는 호출처가 묻는 같은 질문 - 이 인터페이스가 없거나 null 이면 거짓. "지금 노릴 수 있나" 는 이 하나로 묻는다.
	VB_API static bool IsActorTargetable(const AActor* Actor);
	virtual FVector GetTargetMarkerLocation() const = 0;
	// 이 대상이 무엇인지(데이터 정체, 예: Enemy.Boss.Troll) - 종류로 대상을 고르는 규칙이 쓴다. 비면 정체 없음.
	virtual FGameplayTagContainer GetTargetIdentityTags() const { return FGameplayTagContainer(); }
};
