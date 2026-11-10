// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h" // EGameplayTagReplicationState
#include "GameplayTagContainer.h"

class UAbilitySystemComponent;

/**
 * 한 소유자가 한 ASC 에 붙인 느슨한 태그 한 몫의 영수증. 붙인 ASC 에서만 같은 복제 방식으로 뗀다.
 * 뗄 때 '지금의' ASC 를 다시 찾지 않는다.
 */
struct VB_API FVBLooseTagHold
{
	FVBLooseTagHold() = default;
	// 한 몫에 영수증 하나 - 복사본이 생기면 같은 몫을 두 번 뗀다.
	FVBLooseTagHold(const FVBLooseTagHold&) = delete;
	FVBLooseTagHold& operator=(const FVBLooseTagHold&) = delete;

	// 태그마다 1 을 붙이고 기억한다. 이미 보유 중이거나 ASC 가 없으면 아무것도 안 하고 false.
	bool Hold(UAbilitySystemComponent* InASC, const FGameplayTagContainer& InTags, EGameplayTagReplicationState InReplication);
	// 기억한 ASC 에서 1 씩 뗀다. ASC 가 사라졌으면 비우기만 한다.
	void Release();
	bool IsHeld() const { return bHeld; }
	UAbilitySystemComponent* GetASC() const { return ASC.Get(); }

private:
	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	FGameplayTagContainer Tags;
	EGameplayTagReplicationState Replication = EGameplayTagReplicationState::None;
	bool bHeld = false;
};
