// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBEnemyRegistrySubsystem.generated.h"

class AVBEnemyBase;

/**
 * 이 월드의 적 목록. 근처 적 체력 바가 물리 질의 없이 고르려고 둔다. 표시 전용이다.
 * AVBEnemyBase 가 BeginPlay/EndPlay 에서 올리고 내린다(서버, 클라 모두). 파괴된 약참조는 GetEnemies 가 걷어낸다.
 */
UCLASS()
class VB_API UVBEnemyRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void Register(AVBEnemyBase* Enemy);
	void Unregister(const AVBEnemyBase* Enemy);

	// 살아 있는 객체만(죽음 판정은 부르는 쪽이 한다). 파괴된 약참조는 이 호출이 걷어낸다.
	const TArray<TWeakObjectPtr<AVBEnemyBase>>& GetEnemies();

private:
	TArray<TWeakObjectPtr<AVBEnemyBase>> Enemies;
};
