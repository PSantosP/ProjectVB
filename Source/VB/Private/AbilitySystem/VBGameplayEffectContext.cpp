// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBGameplayEffectContext.h"

FVBGameplayEffectContext* FVBGameplayEffectContext::Get(FGameplayEffectContextHandle& Handle)
{
	FGameplayEffectContext* Base = Handle.Get();
	return Base && Base->GetScriptStruct()->IsChildOf(StaticStruct()) ? static_cast<FVBGameplayEffectContext*>(Base) : nullptr;
}

const FVBGameplayEffectContext* FVBGameplayEffectContext::Get(const FGameplayEffectContextHandle& Handle)
{
	const FGameplayEffectContext* Base = Handle.Get();
	return Base && Base->GetScriptStruct()->IsChildOf(StaticStruct()) ? static_cast<const FVBGameplayEffectContext*>(Base) : nullptr;
}

EVBHitImpact FVBGameplayEffectContext::GetDeclaredHitImpact() const
{
	return VBHitImpactFromMagnitude(static_cast<float>(HitImpact));
}

void FVBGameplayEffectContext::SetPushYaw(float Yaw)
{
	// 옛 SetByCaller 경로의 '실리지 않음' 표식(float 최솟값)도 방향 없음으로 본다.
	bHasPushYaw = FMath::IsFinite(Yaw) && Yaw > TNumericLimits<float>::Lowest();
	PushYaw = bHasPushYaw ? Yaw : 0.0f;
}

bool FVBGameplayEffectContext::GetPushYaw(float& OutYaw) const
{
	OutYaw = PushYaw;
	return bHasPushYaw;
}

FVBGameplayEffectContext* FVBGameplayEffectContext::Duplicate() const
{
	FVBGameplayEffectContext* NewContext = new FVBGameplayEffectContext();
	*NewContext = *this;
	if (GetHitResult())
	{
		// 복사는 HitResult 의 공유 포인터를 함께 보유한다 - 복제본을 고치면 원본이 바뀌지 않게 깊게 다시 넣는다(기본형 Duplicate 와 같다).
		NewContext->AddHitResult(*GetHitResult(), true);
	}

	return NewContext;
}

bool FVBGameplayEffectContext::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	// 기본형(가해자, 원인, 출처, HitResult, 원점)을 먼저, 그 뒤에 우리 사실. 순서가 쓰기, 읽기에서 같아야 한다.
	bool bBaseSuccess = true;
	const bool bMapped = FGameplayEffectContext::NetSerialize(Ar, Map, bBaseSuccess);

	uint8 ImpactOrdinal = static_cast<uint8>(HitImpact);
	Ar << ImpactOrdinal;
	if (Ar.IsLoading())
	{
		// 범위 밖 값도 그대로 둔다 - GetDeclaredHitImpact 가 Light 로 읽는다.
		HitImpact = static_cast<EVBHitImpact>(ImpactOrdinal);
	}

	Ar << LaunchHeight;

	uint8 bYaw = bHasPushYaw ? 1 : 0;
	Ar.SerializeBits(&bYaw, 1);
	if (bYaw)
	{
		Ar << PushYaw;
	}
	else if (Ar.IsLoading())
	{
		// 다시 쓰는 객체에 지난 값이 남지 않게.
		PushYaw = 0.0f;
	}

	if (Ar.IsLoading())
	{
		bHasPushYaw = bYaw != 0;
		ChargeSource = EVBCombatChargeSource::Unspecified;
		DeathMotion = FVBDeathMotion();
	}

	bool bShapeSuccess = true;
	StrikingShape.NetSerialize(Ar, Map, bShapeSuccess);
	bBaseSuccess &= bShapeSuccess;

	bOutSuccess = bBaseSuccess && !Ar.IsError();
	return bMapped;
}
