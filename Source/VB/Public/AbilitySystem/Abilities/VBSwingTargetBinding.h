// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h" // FOnGameplayEffectTagCountChanged
#include "UObject/ScriptDelegates.h"

class AActor;
class UAbilitySystemComponent;

/**
 * 한 타가 잡은 대상과 그 죽음 신호 구독(State.Dead 태그, 액터 파괴)의 영수증. 묶은 곳에서만 푼다.
 * 소멸자에서 풀지 않는다 - 능력이 끝날 때(ClearWarpTarget) 푼다.
 */
struct VB_API FVBSwingTargetBinding
{
	// 앞의 대상을 풀고 새 대상에 묶는다. ASC 가 없으면 파괴만 묶는다.
	void Bind(AActor* InTarget, const FScriptDelegate& OnDestroyed, const FOnGameplayEffectTagCountChanged::FDelegate& OnDeadTag);
	// 기억한 ASC, 대상에서 구독을 뺀다. 태그 방송 중(처리 함수 안)에 불러도 된다.
	void Unbind();
	// 묶인 대상. 풀었거나 대상이 사라졌으면 nullptr.
	AActor* Get() const { return Target.Get(); }

private:
	TWeakObjectPtr<AActor> Target;
	TWeakObjectPtr<UAbilitySystemComponent> TargetASC;
	FDelegateHandle DeadTagHandle;
	FScriptDelegate DestroyedBinding;
};
