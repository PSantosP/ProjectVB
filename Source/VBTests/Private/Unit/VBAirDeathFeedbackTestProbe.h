#pragma once

#include "CoreMinimal.h"
#include "Enemy/VBEnemyBase.h"
#include "GameplayCueInterface.h"
#include "VBAirDeathFeedbackTestProbe.generated.h"

UCLASS(NotBlueprintable, NotPlaceable)
class AVBAirDeathFeedbackTestEnemy : public AVBEnemyBase, public IGameplayCueInterface
{
	GENERATED_BODY()

public:
	AVBAirDeathFeedbackTestEnemy(const FObjectInitializer& ObjectInitializer);
	// 몸의 사망 단계가 부르는 순서 그대로 죽음 연출을 돌린다: 공중 상태 기록 -> 스냅샷 -> 이 기계에서 재생.
	void Die(const FVBDeathFacts& Facts);
	// 엔진이 착지 때 울리는 델리게이트를 그대로 울린다.
	void Land(const FHitResult& Hit) { LandedDelegate.Broadcast(Hit); }
	virtual void HandleGameplayCue(UObject* Self, FGameplayTag Tag, EGameplayCueEvent::Type Event,
		const FGameplayCueParameters& Parameters) override;
	int32 LandingCues = 0;
	FGameplayCueParameters LastLanding;
};
