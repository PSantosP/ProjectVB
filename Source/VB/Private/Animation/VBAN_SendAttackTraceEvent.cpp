// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "Animation/VBAN_SendAttackTraceEvent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBSwingImpactProfile.h" // OptionalObject(UObject) 로의 업캐스트 - 완전 타입 필요
#include "Data/VBSwingCombatProfile.h" // OptionalObject2 로의 업캐스트 - 같은 이유
#include "VB/VB.h"

UVBAN_SendAttackTraceEvent::UVBAN_SendAttackTraceEvent()
{
	// 정확한 프레임 타이밍 보장하도록
#if WITH_EDITORONLY_DATA
	bShouldFireInEditor = false;
#endif
}

void UVBAN_SendAttackTraceEvent::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	
	if (!MeshComp)
	{
		return;
	}
	
	AActor* OwnerActor = MeshComp->GetOwner();
	if (!OwnerActor)
	{
		return;
	}
	
	// Owner의 ASC를 통해 GameplayEvent 전송
	UAbilitySystemComponent* ASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OwnerActor);
	
	if (!ASC)
	{
		return;
	}
	
	// GameplayEvent 데이터 구성
	FGameplayEventData EventData;
	EventData.EventTag = VBGameplayTags::Event_Montage_AttackTrace;
	EventData.Instigator = OwnerActor;
	EventData.Target = OwnerActor; // GA에서 실제 타겟 탐색
	// 쉐이크 프로필. null 이면 수신측이 leaf 기본값을 쓴다. 서버에서 발화하므로 복제는 필요 없다.
	EventData.OptionalObject = SwingImpactProfile;
	// 전투 수치 보정 프로필. null 이면 보정 없음.
	EventData.OptionalObject2 = SwingCombatProfile;

	// 이벤트 전송 -> WaitGameplayEvent가 수신
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		OwnerActor,
		VBGameplayTags::Event_Montage_AttackTrace,
		EventData);
	
	VB_LOG(Log, "AN_SendAttackTraceEvent: 공격 Trace 이벤트 전송 (%s)", *OwnerActor->GetName());
}

FString UVBAN_SendAttackTraceEvent::GetNotifyName_Implementation() const
{
	return TEXT("Attack Trace");
}


