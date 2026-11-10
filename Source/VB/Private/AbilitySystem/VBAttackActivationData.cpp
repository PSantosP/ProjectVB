// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBAttackActivationData.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Data/VBAttackConfig.h"
#include "Engine/PackageMapClient.h"

bool FVBAttackActivationData::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << PressSerial;
	Ar << VariantIndex;
	uint8 InReachBit = bTargetInReach ? 1 : 0;
	Ar.SerializeBits(&InReachBit, 1);
	bTargetInReach = InReachBit != 0;
	Ar << ResumeComboIndex;
	UObject* ConfigObject = Config.Get();
	bOutSuccess = Map ? Map->SerializeObject(Ar, UVBAttackConfig::StaticClass(), ConfigObject) : true;
	if (Ar.IsLoading())
	{
		Config = Cast<UVBAttackConfig>(ConfigObject);
	}

	return true;
}

bool FVBAttackActivationData::IsExecutable() const
{
	// 재생할 몽타주가 하나도 없으면(변형 0개) 번호 0 도 실행할 수 없다 - 레거시 즉시 Trace 폴백은 준비 없는 활성화만의 길이다.
	return Config && Config->ComboSelection
		&& VariantIndex >= 0 && VariantIndex < Config->GetMontageVariantCount();
}

const FVBAttackActivationData* FVBAttackActivationData::Find(const FGameplayEventData* Event)
{
	if (!Event)
	{
		return nullptr;
	}

	for (int32 Index = 0; Index < Event->TargetData.Num(); ++Index)
	{
		const FGameplayAbilityTargetData* Data = Event->TargetData.Get(Index);
		if (Data && Data->GetScriptStruct() == StaticStruct())
		{
			return static_cast<const FVBAttackActivationData*>(Data);
		}
	}

	return nullptr;
}
