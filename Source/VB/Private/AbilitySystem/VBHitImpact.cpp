// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBHitImpact.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Abilities/GameplayAbilityTargetTypes.h" // FGameplayAbilityTargetData_LocationInfo
#include "Abilities/GameplayAbilityTypes.h"       // FGameplayEventData

namespace
{
	constexpr int32 MaxImpactOrdinal = static_cast<int32>(EVBHitImpact::Launch);
}

EVBHitImpact VBResolveHitImpact(EVBHitImpact Declared, float Resistance)
{
	// 저항은 GE 가 저작하는 속성이라 2.0 이 1.9999 로 올 수 있다 - 내림하면 한 단계가 사라지므로 반올림한다.
	const int32 Steps = FMath::Max(0, FMath::RoundToInt(Resistance));
	const int32 Resolved = FMath::Max(0, static_cast<int32>(Declared) - Steps);
	return static_cast<EVBHitImpact>(Resolved);
}

EVBHitImpact VBHitImpactFromMagnitude(float Magnitude)
{
	// 유한성부터 본다 - NaN, 무한을 정수로 바꾸는 것 자체가 정의되지 않은 동작이다.
	if (!FMath::IsFinite(Magnitude))
	{
		return EVBHitImpact::Light;
	}

	const int32 Ordinal = FMath::RoundToInt(Magnitude);
	if (Ordinal < static_cast<int32>(EVBHitImpact::Light) || Ordinal > MaxImpactOrdinal)
	{
		return EVBHitImpact::Light;
	}

	return static_cast<EVBHitImpact>(Ordinal);
}

FGameplayTag VBHitImpactToTag(EVBHitImpact Impact)
{
	switch (Impact)
	{
	case EVBHitImpact::None: return VBGameplayTags::Combat_Impact_None;
	case EVBHitImpact::Light: return VBGameplayTags::Combat_Impact_Light;
	case EVBHitImpact::Heavy: return VBGameplayTags::Combat_Impact_Heavy;
	case EVBHitImpact::Knockdown: return VBGameplayTags::Combat_Impact_Knockdown;
	case EVBHitImpact::Launch: return VBGameplayTags::Combat_Impact_Launch;
	default: return FGameplayTag();
	}
}

EVBHitImpact VBHitImpactFromTags(const FGameplayTagContainer& Tags)
{
	// 센 것부터 본다 - 태그는 하나만 실리지만, 잘못 둘이 실렸을 때 약한 쪽으로 떨어지면 넘어뜨렸는데 플린치가 된다.
	if (Tags.HasTagExact(VBGameplayTags::Combat_Impact_Launch))
	{
		return EVBHitImpact::Launch;
	}

	if (Tags.HasTagExact(VBGameplayTags::Combat_Impact_Knockdown))
	{
		return EVBHitImpact::Knockdown;
	}

	if (Tags.HasTagExact(VBGameplayTags::Combat_Impact_Heavy))
	{
		return EVBHitImpact::Heavy;
	}

	if (Tags.HasTagExact(VBGameplayTags::Combat_Impact_Light))
	{
		return EVBHitImpact::Light;
	}

	if (Tags.HasTagExact(VBGameplayTags::Combat_Impact_None))
	{
		return EVBHitImpact::None;
	}
	// 등급 태그가 아예 없다 = 이 설계 전의 경로(패링 되받기 등). 종전처럼 플린치.
	return EVBHitImpact::Light;
}

const TCHAR* VBHitImpactName(EVBHitImpact Impact)
{
	switch (Impact)
	{
	case EVBHitImpact::None: return TEXT("없음");
	case EVBHitImpact::Light: return TEXT("가벼움");
	case EVBHitImpact::Heavy: return TEXT("무거움");
	case EVBHitImpact::Knockdown: return TEXT("넘어뜨림");
	case EVBHitImpact::Launch: return TEXT("띄움");
	default: return TEXT("?");
	}
}

void VBSetPushYaw(FGameplayEventData& Event, float Yaw)
{
	// 위치 정보 한 개의 끝점 변환에 회전만 담는다. 핸들이 소유권을 가져가므로 new 가 맞다(엔진 관례).
	FGameplayAbilityTargetData_LocationInfo* Data = new FGameplayAbilityTargetData_LocationInfo();
	Data->TargetLocation.LocationType = EGameplayAbilityTargetingLocationType::LiteralTransform;
	Data->TargetLocation.LiteralTransform = FTransform(FRotator(0.0f, Yaw, 0.0f));
	Event.TargetData = FGameplayAbilityTargetDataHandle(Data);
}

bool VBGetPushYaw(const FGameplayEventData& Event, float& OutYaw)
{
	const FGameplayAbilityTargetData* Data = Event.TargetData.Get(0);
	if (!Data || !Data->HasEndPoint())
	{
		return false;
	}

	OutYaw = Data->GetEndPointTransform().Rotator().Yaw;
	return true;
}

void VBSetPushDistance(FGameplayEventData& Event, float DistanceCm)
{
	// 방향이 실린 같은 끝점 변환의 X 이동에 둔다. 회전은 방향, 이동은 거리 - 한 밀림 값이다.
	FGameplayAbilityTargetData* Data = Event.TargetData.Get(0);
	if (!Data || Data->GetScriptStruct() != FGameplayAbilityTargetData_LocationInfo::StaticStruct() || !FMath::IsFinite(DistanceCm))
	{
		return;
	}

	static_cast<FGameplayAbilityTargetData_LocationInfo*>(Data)->TargetLocation.LiteralTransform.SetTranslation(
		FVector(FMath::Max(0.0f, DistanceCm), 0.0f, 0.0f));
}

bool VBGetPushDistance(const FGameplayEventData& Event, float& OutDistanceCm)
{
	const FGameplayAbilityTargetData* Data = Event.TargetData.Get(0);
	if (!Data || !Data->HasEndPoint())
	{
		return false;
	}

	const float Distance = static_cast<float>(Data->GetEndPointTransform().GetTranslation().X);
	if (!FMath::IsFinite(Distance) || Distance <= 0.0f)
	{
		return false;
	}

	OutDistanceCm = Distance;
	return true;
}

float VBEstimateKnockbackDistance(float KnockStrength, float LaunchHeight, float GravityAbs, float BrakingDeceleration)
{
	if (!FMath::IsFinite(KnockStrength) || !(KnockStrength > 0.0f))
	{
		return 0.0f;
	}

	const bool bValidLaunch = FMath::IsFinite(LaunchHeight) && LaunchHeight > 0.0f && FMath::IsFinite(GravityAbs) && GravityAbs > 0.0f;
	const float AirSeconds = bValidLaunch ? 2.0f * FMath::Sqrt(2.0f * LaunchHeight / GravityAbs) : 0.0f;
	const float SlideCm = FMath::IsFinite(BrakingDeceleration) && BrakingDeceleration > 0.0f
		? KnockStrength * KnockStrength / (2.0f * BrakingDeceleration) : 0.0f;
	return KnockStrength * AirSeconds + SlideCm;
}

FVBHitMovement VBResolveHitMovement(const FVector& PushDir, float KnockStrength,
	bool bClipOwnsHorizontal, float LaunchHeight, float GravityAbs)
{
	FVBHitMovement Movement;
	const FVector PlanarDir = PushDir.ContainsNaN() ? FVector::ZeroVector : PushDir.GetSafeNormal2D();
	if (FMath::IsFinite(KnockStrength) && KnockStrength > 0.0f && !bClipOwnsHorizontal && !PlanarDir.IsNearlyZero())
	{
		Movement.Mode = EVBHitMovement::Push;
		Movement.Velocity = PlanarDir * KnockStrength;
	}

	if (FMath::IsFinite(LaunchHeight) && LaunchHeight > 0.0f && FMath::IsFinite(GravityAbs) && GravityAbs > 0.0f)
	{
		Movement.Mode = EVBHitMovement::Launch;
		Movement.Velocity.Z = FMath::Sqrt(2.0f * GravityAbs * LaunchHeight);
	}

	return Movement;
}
