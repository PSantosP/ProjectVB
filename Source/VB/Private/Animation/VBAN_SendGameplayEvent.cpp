// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBAN_SendGameplayEvent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "VB/VB.h"

void UVBAN_SendGameplayEvent::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	// 듣는 능력은 서버 것이다(서버도 몽타주를 튼다) - 권한 없는 쪽이 시각을 말하지 않게 한다.
	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	if (!EventTag.IsValid())
	{
		VB_LOG(Warning, "노티(Send Gameplay Event): %s 의 %s 에 EventTag 가 비어 있다 - 아무것도 보내지 않는다", *GetNameSafe(Owner), *GetNameSafe(Animation));
		return;
	}

	FGameplayEventData EventData;
	EventData.EventTag = EventTag;
	EventData.Instigator = Owner;
	EventData.Target = Owner;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, EventTag, EventData);
}

FString UVBAN_SendGameplayEvent::GetNotifyName_Implementation() const
{
	return EventTag.IsValid() ? FString::Printf(TEXT("Send %s"), *EventTag.ToString()) : TEXT("Send Gameplay Event (no tag)");
}
