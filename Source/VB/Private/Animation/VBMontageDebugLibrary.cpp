// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBMontageDebugLibrary.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"            // UAnimSequence::bEnableRootMotion / bForceRootLock
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimNotifies/AnimNotify.h"

FName UVBMontageDebugLibrary::GetMontageSlotName(UAnimMontage* Montage)
{
	// SlotAnimTracks 는 UAnimMontage public 멤버 - C++ 에선 직접 접근 가능(python 만 미노출).
	if (Montage && Montage->SlotAnimTracks.Num() > 0)
	{
		return Montage->SlotAnimTracks[0].SlotName;
	}

	return NAME_None;
}

TArray<FString> UVBMontageDebugLibrary::GetMontageNotifySummary(UAnimMontage* Montage)
{
	TArray<FString> Out;
	if (!Montage)
	{
		return Out;
	}
	// Notifies 는 UAnimSequenceBase public getter 로 접근(직접 멤버는 protected).
	for (const FAnimNotifyEvent& Ev : Montage->Notifies)
	{
		const UClass* Cls = Ev.NotifyStateClass ? Ev.NotifyStateClass->GetClass()
			: (Ev.Notify ? Ev.Notify->GetClass() : nullptr);

		// MotionWarping notify 의 WarpTargetName 추출 (reflection - RootMotionModifier.WarpTargetName).
		// cpp WT_FrontLedge("FrontLedge") 와 일치해야 워프 작동. 불일치면 RootMotion 폭주.
		FString WarpName;
		if (Ev.NotifyStateClass)
		{
			UObject* Mod = nullptr;
			if (const FObjectProperty* MP = CastField<FObjectProperty>(
				Ev.NotifyStateClass->GetClass()->FindPropertyByName(TEXT("RootMotionModifier"))))
			{
				Mod = MP->GetObjectPropertyValue_InContainer(Ev.NotifyStateClass);
			}

			if (Mod)
			{
				if (const FNameProperty* NP = CastField<FNameProperty>(
					Mod->GetClass()->FindPropertyByName(TEXT("WarpTargetName"))))
				{
					WarpName = NP->GetPropertyValue_InContainer(Mod).ToString();
				}
			}
		}

		Out.Add(FString::Printf(TEXT("%s @%.3f dur=%.3f cls=%s warpTarget=%s"),
			*Ev.NotifyName.ToString(),
			Ev.GetTriggerTime(),
			Ev.GetDuration(),
			Cls ? *Cls->GetName() : TEXT("None"),
			WarpName.IsEmpty() ? TEXT("-") : *WarpName));
	}

	return Out;
}

FString UVBMontageDebugLibrary::GetMontageRootMotionInfo(UAnimMontage* Montage)
{
	if (!Montage || Montage->SlotAnimTracks.Num() == 0)
	{
		return TEXT("no-slot");
	}

	const FAnimTrack& Track = Montage->SlotAnimTracks[0].AnimTrack;
	if (Track.AnimSegments.Num() == 0)
	{
		return TEXT("no-segment");
	}

	UAnimSequenceBase* Seq = Track.AnimSegments[0].GetAnimReference();
	UAnimSequence* AS = Cast<UAnimSequence>(Seq);
	if (!AS)
	{
		return FString::Printf(TEXT("src=%s (not UAnimSequence)"), *GetNameSafe(Seq));
	}

	return FString::Printf(TEXT("src=%s enableRM=%d forceRootLock=%d"),
		*AS->GetName(), (int)AS->bEnableRootMotion, (int)AS->bForceRootLock);
}

FName UVBMontageDebugLibrary::GetMontageSectionNext(UAnimMontage* Montage, FName SectionName)
{
	if (!Montage)
	{
		return NAME_None;
	}

	const int32 Index = Montage->GetSectionIndex(SectionName);
	return Montage->CompositeSections.IsValidIndex(Index) ? Montage->CompositeSections[Index].NextSectionName : NAME_None;
}
