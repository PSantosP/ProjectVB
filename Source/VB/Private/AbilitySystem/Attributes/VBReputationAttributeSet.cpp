// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Attributes/VBReputationAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UVBReputationAttributeSet::UVBReputationAttributeSet()
{
	// 기본값: 중립 상태
	InitReputation(0.0f);
}

void UVBReputationAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	// 여기서 잡히는 것은 CurrentValue 뿐이다. BaseValue 클램프는 PostGameplayEffectExecute 몫이다.
	if (Attribute == GetReputationAttribute())
	{
		ClampAttributeOnChange(Attribute, NewValue, ReputationMin, ReputationMax);
	}
}

void UVBReputationAttributeSet::PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// IncomingReputationChange 처리 (Meta Attribute 파이프라인)
	if (Data.EvaluatedData.Attribute == GetIncomingReputationChangeAttribute())
	{
		const float ChangeValue = GetIncomingReputationChange();
		SetIncomingReputationChange(0.0f);

		if (ChangeValue != 0.0f)
		{
			// BaseValue 클램프 - PreAttributeChange(CurrentValue)와 중복이 아니다. 지우면 범위 밖 명성이 세이브에 굳는다.
			const float NewReputation = FMath::Clamp(GetReputation() + ChangeValue, ReputationMin, ReputationMax);
			SetReputation(NewReputation);
		}
	}
}

void UVBReputationAttributeSet::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME_CONDITION_NOTIFY(UVBReputationAttributeSet,
		Reputation,
		COND_None,
		REPNOTIFY_Always);
}

void UVBReputationAttributeSet::OnRep_Reputation(const FGameplayAttributeData& OldReputation)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBReputationAttributeSet, Reputation, OldReputation);
}
