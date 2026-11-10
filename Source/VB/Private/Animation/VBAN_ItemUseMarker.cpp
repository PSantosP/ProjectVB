// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBAN_ItemUseMarker.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Components/SkeletalMeshComponent.h"
#include "VB/VB.h"

namespace
{
	int32 CountMarkers(const UAnimSequenceBase& Animation)
	{
		int32 Count = 0;
		for (const FAnimNotifyEvent& Event : Animation.Notifies)
		{
			Count += Cast<UVBAN_ItemUseMarker>(Event.Notify) ? 1 : 0;
		}

		return Count;
	}
}

UVBAN_ItemUseMarker::UVBAN_ItemUseMarker()
{
#if WITH_EDITORONLY_DATA
	// 에디터 미리보기에서 보낼 상대가 없다.
	bShouldFireInEditor = false;
#endif
}

int32 UVBAN_ItemUseMarker::CountIn(const UAnimMontage& Montage)
{
	int32 Count = CountMarkers(Montage);
	for (const FSlotAnimationTrack& Slot : Montage.SlotAnimTracks)
	{
		for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
		{
			// 몽타주가 자기 자신을 조각으로 품어도 두 번 세지 않는다.
			const UAnimSequenceBase* Animation = Segment.GetAnimReference();
			if (Animation && Animation != &Montage)
			{
				Count += CountMarkers(*Animation);
			}
		}
	}

	return Count;
}

#define LOCTEXT_NAMESPACE "VBAN_ItemUseMarker"

TArray<FText> UVBAN_ItemUseMarker::FindUseMontageProblems(const UAnimMontage& Montage)
{
	TArray<FText> Problems;
	const int32 Count = CountIn(Montage);
	if (Count != 1)
	{
		Problems.Add(FText::Format(LOCTEXT("Count", "사용 표식(Item Use 노티)이 {0} 개다 - 정확히 하나여야 한다."), FText::AsNumber(Count)));
	}

	if (CountMarkers(Montage) != Count)
	{
		Problems.Add(LOCTEXT("OnSegment", "사용 표식이 조각 애니에 놓여 있다 - 몽타주 자체에 둔다(그 애니를 쓰는 다른 몽타주에도 표식이 생기고, 시각을 몽타주 기준으로 잴 수 없다)."));
	}

	if (!Montage.bEnableAutoBlendOut)
	{
		Problems.Add(LOCTEXT("NoAutoBlendOut", "자동 블렌드아웃이 꺼져 있다 - 몽타주가 끝나지 않아 사용 GA 가 계속 켜진다."));
	}
	// 다음 섹션을 따라가다 같은 섹션을 다시 만나면 끝나지 않는다(자기 반복 포함).
	for (const FCompositeSection& Start : Montage.CompositeSections)
	{
		TSet<FName> Seen;
		FName Current = Start.SectionName;
		while (!Current.IsNone() && !Seen.Contains(Current))
		{
			Seen.Add(Current);
			const int32 Index = Montage.GetSectionIndex(Current);
			Current = Montage.CompositeSections.IsValidIndex(Index) ? Montage.CompositeSections[Index].NextSectionName : FName();
		}

		if (!Current.IsNone())
		{
			Problems.Add(FText::Format(LOCTEXT("SectionLoop", "섹션 {0} 이 되돌아온다 - 몽타주가 끝나지 않는다."), FText::FromName(Current)));
			break;
		}
	}
	// 자동 블렌드아웃은 끝에서 트리거 시간(음수면 BlendOut 시간) 앞에 시작하고, 사용 GA 는 그때 끝난다.
	// 트리거 시간은 실시간이라 몽타주 위치로는 RateScale 을 곱해 견준다.
	if (Montage.bEnableAutoBlendOut)
	{
		const float TriggerTime = Montage.BlendOutTriggerTime >= 0.0f ? Montage.BlendOutTriggerTime : Montage.BlendOut.GetBlendTime();
		const float BlendOutStart = Montage.GetPlayLength() - TriggerTime * FMath::Abs(Montage.RateScale);
		for (const FAnimNotifyEvent& Event : Montage.Notifies)
		{
			if (Cast<UVBAN_ItemUseMarker>(Event.Notify) && Event.GetTriggerTime() >= BlendOutStart)
			{
				Problems.Add(FText::Format(LOCTEXT("AfterBlendOut",
					"사용 표식({0}초)이 자동 블렌드아웃 시작({1}초)과 같거나 뒤다 - 사용 GA 는 블렌드아웃에서 끝나 확정이 안 온다."),
					FText::AsNumber(Event.GetTriggerTime()), FText::AsNumber(BlendOutStart)));
			}
		}
	}

	return Problems;
}

#undef LOCTEXT_NAMESPACE

void UVBAN_ItemUseMarker::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	// 확정은 서버만 보낸다.
	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	FGameplayEventData EventData;
	EventData.EventTag = VBGameplayTags::Event_Item_UseMarker;
	EventData.Instigator = Owner;
	EventData.Target = Owner;
	// 몽타주를 싣지 않는다 - 조각 애니의 표식이면 Animation 이 그 애니다. 몽타주 확인은 사용 GA 가 한다.
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, VBGameplayTags::Event_Item_UseMarker, EventData);
	VB_LOG(Verbose, "아이템 사용 표식: %s (%s)", *GetNameSafe(Owner), *GetNameSafe(Animation));
}

FString UVBAN_ItemUseMarker::GetNotifyName_Implementation() const
{
	return TEXT("Item Use");
}
