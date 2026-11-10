// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "AbilitySystem/VBHitImpact.h"
#include "CharacterCommon/VBCombatChargeCredit.h"
#include "CharacterCommon/VBDeathFacts.h"
#include "VBGameplayEffectContext.generated.h"

/**
 * 한 타격의 사실을 싣는 효과 컨텍스트(Lyra FLyraGameplayEffectContext 모양). 근접 공격이 쓰고 VBDamageResponse 가 읽는다.
 * 숫자(피해, 넉백 세기 등)는 SetByCaller 에 남고 이것은 '무엇이 일어났나' 만 든다. 사실을 안 쓴 효과는 기본값으로 읽힌다.
 */
USTRUCT()
struct VB_API FVBGameplayEffectContext : public FGameplayEffectContext
{
	GENERATED_BODY()

	// 핸들이 이 타입을 들고 있으면 그 포인터, 아니면 null. 파생 타입까지 받는다.
	static FVBGameplayEffectContext* Get(FGameplayEffectContextHandle& Handle);
	static const FVBGameplayEffectContext* Get(const FGameplayEffectContextHandle& Handle);

	// 공격이 선언한 등급. 저항을 빼기 전 값이다 - 받는 쪽이 VBResolveHitImpact 로 깎는다.
	void SetDeclaredHitImpact(EVBHitImpact Impact) { HitImpact = Impact; }
	// 범위 밖과 None 은 Light 로 읽는다(VBHitImpactFromMagnitude 와 같은 규칙 - None 은 저항이 깎은 결과로만 생긴다).
	EVBHitImpact GetDeclaredHitImpact() const;

	void SetLaunchHeight(float Height) { LaunchHeight = Height; }
	float GetLaunchHeight() const { return LaunchHeight; }

	// 유한한 값만 받는다 - NaN 은 방향 계산을 그대로 통과해 LaunchCharacter 까지 간다. 받지 못한 값은 '방향 없음'.
	void SetPushYaw(float Yaw);
	// 방향이 실렸으면 true 와 그 값. 0도는 유효한 방향이다('없음' 과 다르다).
	bool GetPushYaw(float& OutYaw) const;

	// 공격이 고른 피 이펙트 큐(맞은 부위, 강타판). 맞은 쪽 반응이 막지도 피하지도 않은 타격에만 실행한다(VBDamageResponse).
	//   서버의 반응만 읽으므로 네트워크로 보내지 않는다. 비면 피 이펙트 없음(근접이 아닌 피해).
	void SetImpactCue(const FGameplayTag& Cue) { ImpactCue = Cue; }
	const FGameplayTag& GetImpactCue() const { return ImpactCue; }

	// 때린 부위(Melee.Shape.*). 무기 타격 연출이 주먹, 발, 내려찍기를 가른다 - 클라 큐가 읽으므로 네트워크로 보낸다. 비면 부위 모름.
	void SetStrikingShape(const FGameplayTag& Shape) { StrikingShape = Shape; }
	const FGameplayTag& GetStrikingShape() const { return StrikingShape; }

	void SetChargeSource(EVBCombatChargeSource Source) { ChargeSource = Source; }
	EVBCombatChargeSource GetChargeSource() const { return ChargeSource; }
	/** 서버 사망 처리에 넘길 타격 마무리 */
	void SetDeathMotion(const FVBDeathMotion& Motion) { DeathMotion = Motion; }
	const FVBDeathMotion& GetDeathMotion() const { return DeathMotion; }

	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	virtual FVBGameplayEffectContext* Duplicate() const override;
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;

private:
	UPROPERTY()
	FVBDeathMotion DeathMotion;
	// 서버에서만 정하는 출처 분류 - 로컬 복제도 이 값을 지킨다
	UPROPERTY()
	EVBCombatChargeSource ChargeSource = EVBCombatChargeSource::Unspecified;
	UPROPERTY()
	EVBHitImpact HitImpact = EVBHitImpact::Light;

	// cm. 실효 등급이 Launch 일 때만 받는 쪽이 쓴다.
	UPROPERTY()
	float LaunchHeight = 0.0f;

	UPROPERTY()
	bool bHasPushYaw = false;

	// 월드 yaw(도). bHasPushYaw 가 false 면 읽지 않는다.
	UPROPERTY()
	float PushYaw = 0.0f;

	// NetSerialize 에 넣지 않는다 - 위 SetImpactCue 주석.
	UPROPERTY()
	FGameplayTag ImpactCue;

	UPROPERTY()
	FGameplayTag StrikingShape;
};

template<>
struct TStructOpsTypeTraits<FVBGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FVBGameplayEffectContext>
{
	enum
	{
		WithNetSerializer = true,
		// 기본형과 같은 이유 - HitResult 의 TSharedPtr 를 복사로 옮긴다.
		WithCopy = true
	};
};
