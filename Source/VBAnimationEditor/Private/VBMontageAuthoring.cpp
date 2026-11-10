// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "VBMontageAuthoring.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/Skeleton.h"

namespace
{
	int32 FindUniqueNotifyState(const UAnimMontage* Montage, const UAnimNotifyState* ExpectedState)
	{
		if (!Montage || !ExpectedState || ExpectedState->GetOuter() != Montage)
		{
			return INDEX_NONE;
		}

		int32 Match = INDEX_NONE;
		for (int32 Index = 0; Index < Montage->Notifies.Num(); ++Index)
		{
			if (Montage->Notifies[Index].NotifyStateClass != ExpectedState)
			{
				continue;
			}

			if (Match != INDEX_NONE)
			{
				return INDEX_NONE;
			}

			Match = Index;
		}

		return Match;
	}
}

bool UVBMontageAuthoring::RemoveMontageNotifyState(UAnimMontage* Montage, UAnimNotifyState* ExpectedState)
{
	const int32 Match = FindUniqueNotifyState(Montage, ExpectedState);
	if (Match == INDEX_NONE)
	{
		return false;
	}

	Montage->Modify();
	Montage->Notifies.RemoveAt(Match);
	Montage->RefreshCacheData();
	Montage->MarkPackageDirty();
	return true;
}

bool UVBMontageAuthoring::SetMontageNotifyStateRange(UAnimMontage* Montage, UAnimNotifyState* ExpectedState,
	float StartTime, float Duration)
{
	const int32 Match = FindUniqueNotifyState(Montage, ExpectedState);
	if (Match == INDEX_NONE || !FMath::IsFinite(StartTime) || !FMath::IsFinite(Duration)
		|| StartTime < 0.0f || Duration <= 0.0f || StartTime + Duration > Montage->GetPlayLength())
	{
		return false;
	}

	Montage->Modify();
	FAnimNotifyEvent& Event = Montage->Notifies[Match];
	Event.SetTime(StartTime);
	Event.SetDuration(Duration);
	Event.ConditionalRelink();
	Event.EndLink.ConditionalRelink();
	Event.TriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(StartTime));
	Event.EndTriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(StartTime + Duration));
	Montage->RefreshCacheData();
	Montage->MarkPackageDirty();
	return true;
}

bool UVBMontageAuthoring::SetMontageSlotName(UAnimMontage* Montage, FName NewSlotName)
{
	// UVBMontageDebugLibrary::GetMontageSlotName 과 같은 경로 - SlotAnimTracks[0].SlotName 을 직접 재저작한다.
	// 그 자리에서 바꾼다: 세그먼트, 노티, 블렌드, 워프 등 몽타주의 나머지 데이터는 전부 그대로다.
	if (!Montage || Montage->SlotAnimTracks.Num() == 0)
	{
		return false;
	}

	Montage->SlotAnimTracks[0].SlotName = NewSlotName;
	Montage->MarkPackageDirty();
	return true;
}

bool UVBMontageAuthoring::SetMontageSegmentAnim(UAnimMontage* Montage, UAnimSequenceBase* Anim)
{
	if (!Montage || !Anim || Montage->SlotAnimTracks.Num() == 0)
	{
		return false;
	}

	FAnimTrack& Track = Montage->SlotAnimTracks[0].AnimTrack;
	if (Track.AnimSegments.Num() == 0)
	{
		return false;
	}

	Track.AnimSegments[0].SetAnimReference(Anim, false);
	Montage->MarkPackageDirty();
	return true;
}

bool UVBMontageAuthoring::SetMontageBranchInDatabase(UAnimMontage* Montage, UObject* Database)
{
	if (!Montage || !Database)
	{
		return false;
	}

	bool bSet = false;
	for (const FAnimNotifyEvent& Ev : Montage->Notifies)
	{
		if (!Ev.NotifyStateClass)
		{
			continue;
		}

		const UClass* Cls = Ev.NotifyStateClass->GetClass();
		if (!Cls->GetName().Contains(TEXT("PoseSearchBranchIn")))
		{
			continue;
		}

		if (const FObjectProperty* DBProp = CastField<FObjectProperty>(Cls->FindPropertyByName(TEXT("Database"))))
		{
			DBProp->SetObjectPropertyValue_InContainer(Ev.NotifyStateClass, Database);
			bSet = true;
		}
	}

	if (bSet)
	{
		Montage->MarkPackageDirty();
	}

	return bSet;
}

int32 UVBMontageAuthoring::SetMontageWarpTranslation(UAnimMontage* Montage, bool bEnable)
{
	if (!Montage)
	{
		return 0;
	}

	int32 Applied = 0;
	for (const FAnimNotifyEvent& Ev : Montage->Notifies)
	{
		if (!Ev.NotifyStateClass)
		{
			continue;
		}

		const FObjectProperty* MP = CastField<FObjectProperty>(
			Ev.NotifyStateClass->GetClass()->FindPropertyByName(TEXT("RootMotionModifier")));
		if (!MP)
		{
			continue;
		}

		UObject* Mod = MP->GetObjectPropertyValue_InContainer(Ev.NotifyStateClass);
		if (!Mod)
		{
			continue;
		}

		if (const FBoolProperty* BWP = CastField<FBoolProperty>(
			Mod->GetClass()->FindPropertyByName(TEXT("bWarpTranslation"))))
		{
			BWP->SetPropertyValue_InContainer(Mod, bEnable);
			++Applied;
		}
	}

	if (Applied > 0)
	{
		Montage->MarkPackageDirty();
	}

	return Applied;
}

bool UVBMontageAuthoring::SetMontageSectionNext(UAnimMontage* Montage, FName SectionName, FName NextSectionName)
{
	if (!Montage)
	{
		return false;
	}

	const int32 Index = Montage->GetSectionIndex(SectionName);
	if (!Montage->CompositeSections.IsValidIndex(Index))
	{
		return false;
	}

	Montage->Modify();
	Montage->CompositeSections[Index].NextSectionName = NextSectionName;
	Montage->MarkPackageDirty();
	return true;
}

bool UVBMontageAuthoring::SetMontageSegmentRange(UAnimMontage* Montage, float AnimStartTime, float AnimEndTime, float PlayRate)
{
	return SetMontageSegmentRangeAt(Montage, 0, AnimStartTime, AnimEndTime, PlayRate);
}

bool UVBMontageAuthoring::SetMontageSegmentRangeAt(UAnimMontage* Montage, int32 SegmentIndex, float AnimStartTime,
	float AnimEndTime, float PlayRate)
{
	if (!Montage || Montage->SlotAnimTracks.Num() == 0 || !Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsValidIndex(SegmentIndex))
	{
		return false;
	}

	FAnimTrack& Track = Montage->SlotAnimTracks[0].AnimTrack;
	FAnimSegment& Segment = Track.AnimSegments[SegmentIndex];
	const UAnimSequenceBase* Anim = Segment.GetAnimReference();
	// 원료 밖, 뒤집힌 구간, 0 이하 배속은 거절한다 - 엔진은 그런 구간을 조용히 잘라 쓰고 길이만 엉뚱해진다.
	if (!Anim || !(AnimStartTime >= 0.0f) || !(AnimEndTime > AnimStartTime)
		|| AnimEndTime > Anim->GetPlayLength() + KINDA_SMALL_NUMBER || !(PlayRate > 0.0f))
	{
		return false;
	}

	Montage->Modify();
	Segment.AnimStartTime = AnimStartTime;
	Segment.AnimEndTime = AnimEndTime;
	Segment.AnimPlayRate = PlayRate;
	// 뒤 세그먼트가 있으면 그 시작 자리를 앞 세그먼트 끝에 다시 잇는다.
	float Position = 0.0f;
	for (FAnimSegment& Each : Track.AnimSegments)
	{
		Each.StartPos = Position;
		Position += Each.GetLength();
	}
	// MCP 의 set_montage_segment_anim 과 같은 길 - 길이는 세그먼트에서 다시 잰다.
	Montage->SetCompositeLength(Montage->CalculateSequenceLength());
	Montage->MarkPackageDirty();
	return true;
}

bool UVBMontageAuthoring::RegisterSkeletonSlot(USkeleton* Skeleton, FName SlotName, FName GroupName)
{
	if (!Skeleton || SlotName.IsNone() || !Skeleton->FindAnimSlotGroup(GroupName))
	{
		return false;
	}

	if (Skeleton->ContainsSlotName(SlotName) && Skeleton->GetSlotGroupName(SlotName) == GroupName)
	{
		return true;
	}

	Skeleton->Modify();
	Skeleton->RegisterSlotNode(SlotName);
	Skeleton->SetSlotGroupName(SlotName, GroupName);
	Skeleton->MarkPackageDirty();
	return Skeleton->GetSlotGroupName(SlotName) == GroupName;
}
