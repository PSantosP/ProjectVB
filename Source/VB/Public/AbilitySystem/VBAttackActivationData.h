// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "VBAttackActivationData.generated.h"

class UVBAttackConfig;
struct FGameplayEventData;

/**
 * 서버가 공격을 켜기 전에 정한 것. 활성화 이벤트 데이터에 실려 소유 클라로도 가고, 클라는 다시 고르지 않는다.
 */
USTRUCT()
struct VB_API FVBAttackActivationData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	// 이 공격을 켠 누름(UVBAbilitySystemComponent 기록 번호).
	UPROPERTY()
	int32 PressSerial = INDEX_NONE;

	// 고른 변형(콤보) 번호.
	UPROPERTY()
	int32 VariantIndex = 0;

	// 그 변형의 거리 자격 안에서 골랐는가(FVBComboChoice). 거짓이면 첫 타를 표적까지 늘이지 않는다.
	UPROPERTY()
	bool bTargetInReach = true;

	// 회피가 끊은 콤보를 이어받을 타 번호(서버 판정). INDEX_NONE = 처음부터.
	UPROPERTY()
	int32 ResumeComboIndex = INDEX_NONE;

	// 이 활성화의 공격 설정. 클라 무기 복제가 늦어도 서버와 같은 공격을 튼다.
	UPROPERTY()
	TObjectPtr<UVBAttackConfig> Config;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
	virtual UScriptStruct* GetScriptStruct() const override { return FVBAttackActivationData::StaticStruct(); }

	// 설정이 풀렸고 번호가 그 변형 안인가. 아니면 대신하지 않고 활성화를 거절한다.
	bool IsExecutable() const;

	// 이벤트 데이터에서 찾는다. 없으면 nullptr.
	static const FVBAttackActivationData* Find(const FGameplayEventData* Event);
};

template <>
struct TStructOpsTypeTraits<FVBAttackActivationData> : public TStructOpsTypeTraitsBase2<FVBAttackActivationData>
{
	enum
	{
		WithNetSerializer = true // FGameplayAbilityTargetDataHandle 복제에 필수
	};
};
