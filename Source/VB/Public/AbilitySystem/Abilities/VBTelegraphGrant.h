// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class AActor;
class UAbilitySystemComponent;

// 적 공격 예고의 태그와 큐. 붙인 ASC 를 기억해 그 ASC 에서 함께 뗀다.
// 예고 시작 이벤트처럼 공격마다 다른 일은 능력이 맡는다.
struct VB_API FVBTelegraphGrant
{
	// 예고 태그와 큐를 붙인다. Seconds 는 큐가 받는 예고 길이, Answer 는 큐가 받는 답 태그다(없어도 된다).
	void Grant(UAbilitySystemComponent& ASC, AActor* Instigator, const UObject* SourceObject, float Seconds, const FGameplayTag& Answer);

	// 붙인 예고를 뗀다. 붙인 적이 없으면 아무것도 안 한다.
	void Release();

	bool IsGranted() const { return bGranted; }

private:
	bool bGranted = false;
	TWeakObjectPtr<UAbilitySystemComponent> GrantedASC;
};
