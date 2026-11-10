#pragma once

#include "CoreMinimal.h"

class UAbilitySystemComponent;
class USkeletalMeshComponent;
class UVBEnemyConfig;

// 적 캐릭터의 ASC 와 메시에 초기값을 적용하는 상태 없는 함수들. 전달받은 객체를 보관하지 않는다.
// Health 값은 ASC 에, 메시 갱신 요청은 FVBMeshRefresh 에 남는다.
namespace VBEnemySetup
{
	VB_API void ApplyDevHealthScale(UAbilitySystemComponent* ASC, float Scale);
	VB_API void ApplyAnimTickPolicy(USkeletalMeshComponent* Mesh, const UVBEnemyConfig& Config);
}
