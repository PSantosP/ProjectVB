// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"

class AActor;
class UGameplayAbility;

// 동기 시점의 스냅샷이다. ability 인스턴스를 붙잡지 않고, 사용자에게 보일 문구도 아니다.
struct VB_API FVBAbilityFailureReport
{
	FGameplayAbilitySpecHandle Handle;
	TSubclassOf<UGameplayAbility> AbilityClass;
	TWeakObjectPtr<AActor> Avatar;
	FGameplayTagContainer FailureTags;
};
