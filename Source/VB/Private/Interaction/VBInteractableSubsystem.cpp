// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBInteractableSubsystem.h"

#include "GameFramework/Actor.h"
#include "Interaction/VBInteractableComponent.h"
#include "Subsystems/SubsystemCollection.h"
#include "SaveSystem/VBSaveGame.h"
#include "VB/VB.h"

void UVBInteractableSubsystem::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	CollectSavedStates(Save.Progress.InteractableStates);
	CollectSavedTransforms(Save.WorldObjects.Transforms);
}

void UVBInteractableSubsystem::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	ApplySavedStates(Save.Progress.InteractableStates);
	ApplySavedTransforms(Save.WorldObjects.Transforms);
}

void UVBInteractableSubsystem::Register(UVBInteractableComponent* Interactable)
{
	if (Interactable)
	{
		// 이름표 충돌은 한 번 훑어 둘 다 본다. 자리 저장(움직이는 대상)과 켜짐 저장(SharesSaveKey)은 칸이 따로라 따로 말한다.
		//   스트리밍 레벨 사이 액터 이름 충돌이 흔한 원인이다 - SaveId 로 가른다. 켜짐 저장 충돌은 편집기 검사(IsDataValid)가 저작 때도 막는다.
		const FName ObjectKey = Interactable->GetObjectKey();
		for (const TWeakObjectPtr<UVBInteractableComponent>& Entry : GetInteractables())
		{
			if (Entry.Get() == Interactable)
			{
				continue;
			}

			if (Interactable->IsTransformSaved() && Entry->IsTransformSaved() && Entry->GetObjectKey() == ObjectKey)
			{
				VB_LOG(Warning, "자리 저장 이름표 충돌: %s 가 %s 와 같다 - 한쪽에 SaveId 를 적어라", *GetNameSafe(Interactable->GetOwner()),
					*GetNameSafe(Entry->GetOwner()));
			}

			if (UVBInteractableComponent::SharesSaveKey(*Interactable, *Entry))
			{
				VB_LOG(Warning, "켜짐 저장 이름표 충돌: %s 가 %s 와 같은 %s - 한쪽에 SaveId 를 적어라", *GetNameSafe(Interactable->GetOwner()),
					*GetNameSafe(Entry->GetOwner()), *Interactable->GetSaveKey().ToString());
			}
		}

		Interactables.AddUnique(Interactable);
		ApplyPendingState(Interactable);
		ApplyKnownTransform(*Interactable);
	}
}

void UVBInteractableSubsystem::Unregister(const UVBInteractableComponent* Interactable)
{
	if (Interactable && Interactable->IsTransformSaved() && Interactable->GetOwner() && Interactable->GetOwner()->HasAuthority())
	{
		// 스트리밍으로 내려가기 직전의 자리를 남긴다 - 마지막 저장 뒤에 민 것이 다음 저장에서 사라지지 않게(a6 검토 F3).
		RefreshKnownTransform(*Interactable);
	}

	Interactables.RemoveAll([Interactable](const TWeakObjectPtr<UVBInteractableComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Interactable;
	});
}

const TArray<TWeakObjectPtr<UVBInteractableComponent>>& UVBInteractableSubsystem::GetInteractables()
{
	Interactables.RemoveAll([](const TWeakObjectPtr<UVBInteractableComponent>& Entry) { return !Entry.IsValid(); });
	return Interactables;
}

void UVBInteractableSubsystem::CollectSavedStates(TMap<FName, bool>& InOutStates)
{
	for (const TWeakObjectPtr<UVBInteractableComponent>& Entry : GetInteractables())
	{
		const FName Key = Entry->GetSaveKey();
		if (!Key.IsNone())
		{
			InOutStates.Add(Key, Entry->IsActivated());
		}
	}
}

void UVBInteractableSubsystem::ApplySavedStates(const TMap<FName, bool>& States)
{
	PendingSavedStates = States;
	int32 Applied = 0;
	for (const TWeakObjectPtr<UVBInteractableComponent>& Entry : GetInteractables())
	{
		if (PendingSavedStates.Contains(Entry->GetSaveKey()))
		{
			ApplyPendingState(Entry.Get());
			++Applied;
		}
	}

	VB_LOG(Log, "상호작용 상태 로드: 세이브 %d건, 지금 떠 있는 대상에 %d건 적용(나머지는 스트리밍으로 들어올 때)",
		PendingSavedStates.Num(), Applied);
}

void UVBInteractableSubsystem::CollectSavedTransforms(TMap<FName, FTransform>& OutTransforms)
{
	for (const TWeakObjectPtr<UVBInteractableComponent>& Entry : GetInteractables())
	{
		if (Entry->IsTransformSaved())
		{
			RefreshKnownTransform(*Entry);
		}
	}

	OutTransforms = KnownTransforms;
}

void UVBInteractableSubsystem::ApplySavedTransforms(const TMap<FName, FTransform>& Transforms)
{
	if (bReadSaveOnce)
	{
		VB_LOG(Warning, "자리 저장: 이 월드에서 세이브를 두 번째 읽는다 - 맵에 없는 대상을 원자리로 되돌리지 않는다(지원하는 불러오기는 OpenLevel 뿐)");
	}

	bReadSaveOnce = true;
	KnownTransforms = Transforms;
	int32 Applied = 0;
	for (const TWeakObjectPtr<UVBInteractableComponent>& Entry : GetInteractables())
	{
		if (Entry->IsTransformSaved() && KnownTransforms.Contains(Entry->GetObjectKey()))
		{
			ApplyKnownTransform(*Entry);
			++Applied;
		}
	}

	VB_LOG(Log, "자리 저장 로드: 세이브 %d건, 지금 떠 있는 대상에 %d건 적용(나머지는 스트리밍으로 들어올 때)", KnownTransforms.Num(), Applied);
}

void UVBInteractableSubsystem::RefreshKnownTransform(const UVBInteractableComponent& Interactable)
{
	const FName Key = Interactable.GetObjectKey();
	if (Key.IsNone())
	{
		return;
	}

	FTransform Moved;
	if (Interactable.CaptureMovedTransform(Moved))
	{
		KnownTransforms.Add(Key, Moved);
	}
	else
	{
		KnownTransforms.Remove(Key);
	}
}

void UVBInteractableSubsystem::ApplyKnownTransform(UVBInteractableComponent& Interactable) const
{
	if (!Interactable.IsTransformSaved())
	{
		return;
	}

	const FTransform* Saved = KnownTransforms.Find(Interactable.GetObjectKey());
	if (Saved && !Interactable.ApplySavedTransform(*Saved))
	{
		// 맵에는 남겨 둔다 - 다음 저장이 지금 자리로 덮을 때까지 저장된 자리가 진실이다.
		VB_LOG(Warning, "자리 저장 적용 거절: %s - 누가 밀고 있거나 권한이 없다", *GetNameSafe(Interactable.GetOwner()));
	}
}

void UVBInteractableSubsystem::ApplyPendingState(UVBInteractableComponent* Interactable) const
{
	const FName Key = Interactable ? Interactable->GetSaveKey() : NAME_None;
	if (const bool* Saved = Key.IsNone() ? nullptr : PendingSavedStates.Find(Key))
	{
		// SetActivated 는 서버에서만 동작한다. 같은 값이면 아무 일도 없다.
		Interactable->SetActivated(*Saved, /*bInstant=*/true);
	}
}
