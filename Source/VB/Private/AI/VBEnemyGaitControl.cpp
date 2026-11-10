// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AI/VBEnemyGaitControl.h"

#include "AI/VBEnemyCombatComponent.h"
#include "Animation/VBAnimationTypes.h"
#include "Data/VBEnemyConfig.h"
#include "Enemy/VBEnemyBase.h"

EVBGait VBEnemyGaitControl::ChooseGait(double RemainingDistance, bool bRunningNow, float RunEnterDistance, float RunExitDistance)
{
	const float Threshold = bRunningNow ? RunExitDistance : RunEnterDistance;
	return RemainingDistance > Threshold ? EVBGait::Run : EVBGait::Walk;
}

void VBEnemyGaitControl::Update(const UVBEnemyCombatComponent& Combat)
{
	AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(Combat.GetOwner());
	if (!Enemy)
	{
		return;
	}

	const AActor* Target = Combat.GetCombatTarget();
	if (!Target)
	{
		Enemy->ApplyGait(EVBGait::Walk);
		return;
	}

	// Z 를 무시한다 - 경사의 수직차로 걸음이 왕복하지 않게.
	// 링 슬롯으로 가면 슬롯까지(슬롯이 없으면 제 위치라 0), 아니면 공격 시작 거리까지 더 가야 하는 거리(이미 안이면 음수)다.
	const FVector Here = Enemy->GetActorLocation();
	const double Remaining = Combat.GetRole() == EVBCombatRole::Surrounder
		? FVector::Dist2D(Here, Combat.GetSlotLocation())
		: FVector::Dist2D(Here, Target->GetActorLocation()) - Combat.GetAttackStartDistance();
	const UVBEnemyConfig* Config = Combat.GetCombatConfig(); // 절대 null 아님
	Enemy->ApplyGait(ChooseGait(Remaining, Enemy->GetGait() != EVBGait::Walk, Config->RunEnterDistance, Config->RunExitDistance));
}
