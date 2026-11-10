#pragma once

#include "CoreMinimal.h"

class UAbilitySystemComponent;

namespace VBKnockdownTagPolicy
{
	// 기존 Knockdown GA 임대가 가진 기여 하나를 더한다. 공유 상태를 초기화하지 않는다.
	VB_API void Grant(UAbilitySystemComponent& ASC);
	VB_API void Release(UAbilitySystemComponent& ASC);
}
