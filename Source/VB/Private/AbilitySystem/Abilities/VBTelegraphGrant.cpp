// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBTelegraphGrant.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectTypes.h" // FGameplayCueParameters

void FVBTelegraphGrant::Grant(UAbilitySystemComponent& ASC, AActor* Instigator, const UObject* SourceObject, float Seconds,
	const FGameplayTag& Answer)
{
	// 복제 상태를 명시한다. 생략하면 None 이라 원격 클라가 이 태그를 못 본다.
	ASC.AddLooseGameplayTag(VBGameplayTags::State_Combat_Telegraphing, 1, EGameplayTagReplicationState::CountToOwner);
	bGranted = true;
	GrantedASC = &ASC;

	// 예고 큐(색, 소리, 화면 밖 경고). Add/Remove 라 예고 수명과 같이 산다.
	FGameplayCueParameters CueParams;
	CueParams.Instigator = Instigator;
	CueParams.SourceObject = SourceObject;
	CueParams.RawMagnitude = Seconds;
	if (Answer.IsValid())
	{
		CueParams.AggregatedSourceTags.AddTag(Answer);
	}

	ASC.AddGameplayCue(VBGameplayTags::GameplayCue_Combat_Telegraph, CueParams);
}

void FVBTelegraphGrant::Release()
{
	if (!bGranted)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GrantedASC.Get())
	{
		ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Telegraphing, 1, EGameplayTagReplicationState::CountToOwner);
		// 큐 Remove -> 각 클라 OnRemove(발광 정지, 위협 등록 해제).
		ASC->RemoveGameplayCue(VBGameplayTags::GameplayCue_Combat_Telegraph);
	}

	GrantedASC.Reset();
	bGranted = false;
}
