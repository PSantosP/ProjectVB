// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/GameplayCues/VBGC_HitPhysics.h"

#include "CharacterCommon/VBHitPhysicsComponent.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"

bool UVBGC_HitPhysics::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	UVBHitPhysicsComponent* HitPhysics = MyTarget ? MyTarget->FindComponentByClass<UVBHitPhysicsComponent>() : nullptr;
	if (!HitPhysics)
	{
		// UVBHitPhysicsComponent 가 없는 캐릭터(플레이어 등)가 맞는 것은 정상이다 - 알리지 않는다.
		return false;
	}

	const FHitResult* Hit = Parameters.EffectContext.GetHitResult();
	const EVBHitImpact Impact = static_cast<EVBHitImpact>(FMath::Clamp(FMath::RoundToInt(Parameters.RawMagnitude), 0, 255));
	HitPhysics->ApplyHit(Hit ? Hit->BoneName : NAME_None, FVector(Parameters.Normal), Impact);
	return true;
}
