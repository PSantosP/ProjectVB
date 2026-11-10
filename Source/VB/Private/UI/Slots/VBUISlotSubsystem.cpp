// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Slots/VBUISlotSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "UI/Slots/VBUISlotWidget.h"
#include "VB/VB.h"

void FVBUISlotHandle::Unregister()
{
	if (UVBUISlotSubsystem* Registry = Owner.Get())
	{
		Registry->Unregister(Id);
	}

	Owner.Reset();
	Id = INDEX_NONE;
}

void FVBUISlotHandle::ForgetWithoutNotify()
{
	if (UVBUISlotSubsystem* Registry = Owner.Get())
	{
		Registry->Forget(Id);
	}

	Owner.Reset();
	Id = INDEX_NONE;
}

UVBUISlotSubsystem* UVBUISlotSubsystem::Get(const APlayerController* PC)
{
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UVBUISlotSubsystem>() : nullptr;
}

FVBUISlotHandle UVBUISlotSubsystem::RegisterWidget(const FVBUISlotRequest& Request)
{
	FVBUISlotHandle Handle;
	if (!Request.Slot.IsValid() || !Request.WidgetClass)
	{
		VB_LOG(Warning, "UI 슬롯: 태그(%s) 또는 클래스(%s)가 비어 등록하지 않는다",
			*Request.Slot.ToString(), *GetNameSafe(Request.WidgetClass));
		return Handle;
	}

	PruneDeadRegistrants();
	// 같은 등록자가 같은 자리에 같은 클래스를 두 번 거는 것은 실수다 - 두 장이 겹쳐 뜬다.
	const bool bDuplicate = Request.Registrant && Entries.ContainsByPredicate([&Request](const FVBUISlotEntry& Entry)
	{
		return Entry.bHasRegistrant && Entry.Registrant.Get() == Request.Registrant
			&& Entry.Slot == Request.Slot && Entry.WidgetClass == Request.WidgetClass;
	});
	if (bDuplicate)
	{
		VB_LOG(Warning, "UI 슬롯: %s 가 %s 에 %s 를 이미 등록했다 - 두 번째 등록을 거절한다",
			*GetNameSafe(Request.Registrant), *Request.Slot.ToString(), *GetNameSafe(Request.WidgetClass));
		return Handle;
	}

	FVBUISlotEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Id = NextId++;
	Entry.Slot = Request.Slot;
	Entry.WidgetClass = Request.WidgetClass;
	Entry.Order = Request.Order;
	Entry.bHasInitialVisibility = Request.InitialVisibility.IsSet();
	Entry.InitialVisibility = Request.InitialVisibility.Get(ESlateVisibility::Visible);
	// 약참조만 든다(수명을 늘리지 않는다). const 를 떼는 것은 UPROPERTY 약참조가 const 대상을 못 들어서다 - 대상을 고치지 않는다.
	Entry.Scope = const_cast<UObject*>(Request.Scope);
	Entry.bScoped = Request.Scope != nullptr;
	Entry.Registrant = const_cast<UObject*>(Request.Registrant);
	Entry.bHasRegistrant = Request.Registrant != nullptr;

	Handle.Owner = this;
	Handle.Id = Entry.Id;
	// 알리기 전에 손잡이를 다 채운다 - 알림이 자리의 위젯 구성을 부르고, 그 구성이 다시 등록할 수 있다.
	const FGameplayTag Slot = Entry.Slot;
	NotifySlots(Slot);
	return Handle;
}

void UVBUISlotSubsystem::Unregister(int32 Id)
{
	const int32 Index = Entries.IndexOfByPredicate([Id](const FVBUISlotEntry& Entry) { return Entry.Id == Id; });
	if (Index == INDEX_NONE)
	{
		return;
	}

	const FGameplayTag Slot = Entries[Index].Slot;
	Entries.RemoveAt(Index);
	NotifySlots(Slot);
}

void UVBUISlotSubsystem::Forget(int32 Id)
{
	// 자리에 알리지 않는다 - 부르는 쪽이 GC 중이다. 자리가 아직 들고 있는 위젯은 다음 변경 때 목록에서 빠져 떼어진다.
	Entries.RemoveAll([Id](const FVBUISlotEntry& Entry) { return Entry.Id == Id; });
}

void UVBUISlotSubsystem::AddSlot(UVBUISlotWidget& SlotWidget)
{
	Slots.RemoveAll([](const TWeakObjectPtr<UVBUISlotWidget>& Weak) { return !Weak.IsValid(); });
	Slots.AddUnique(&SlotWidget);
	PruneDeadRegistrants();
	SlotWidget.RefreshEntries();
}

void UVBUISlotSubsystem::RemoveSlot(UVBUISlotWidget& SlotWidget)
{
	Slots.RemoveAll([&SlotWidget](const TWeakObjectPtr<UVBUISlotWidget>& Weak)
	{
		return !Weak.IsValid() || Weak.Get() == &SlotWidget;
	});
}

bool UVBUISlotSubsystem::HasSlot(const UVBUISlotWidget& SlotWidget) const
{
	return Slots.ContainsByPredicate([&SlotWidget](const TWeakObjectPtr<UVBUISlotWidget>& Weak) { return Weak.Get() == &SlotWidget; });
}

bool UVBUISlotSubsystem::Accepts(const FVBUISlotEntry& Entry, const UVBUISlotWidget& SlotWidget) const
{
	if (Entry.Slot != SlotWidget.GetSlotTag())
	{
		return false;
	}
	// 등록자가 이미 사라졌으면 걷히기 전이라도 보이지 않는다.
	if (Entry.bHasRegistrant && !Entry.Registrant.IsValid())
	{
		return false;
	}
	// 범위가 있는 항목은 그 소유자가 만든 슬롯만 받는다. 범위가 사라졌으면 어떤 슬롯도 받지 않는다.
	return !Entry.bScoped || (Entry.Scope.IsValid() && Entry.Scope.Get() == SlotWidget.GetSlotOwner());
}

TArray<FVBUISlotEntry> UVBUISlotSubsystem::GetEntriesFor(const UVBUISlotWidget& SlotWidget) const
{
	TArray<FVBUISlotEntry> Result;
	for (const FVBUISlotEntry& Entry : Entries)
	{
		if (Accepts(Entry, SlotWidget))
		{
			Result.Add(Entry);
		}
	}
	// Id 는 등록 순으로 늘어난다 - 같은 Order 면 먼저 등록한 것이 아래.
	Result.Sort([](const FVBUISlotEntry& A, const FVBUISlotEntry& B)
	{
		return A.Order != B.Order ? A.Order < B.Order : A.Id < B.Id;
	});
	return Result;
}

void UVBUISlotSubsystem::PruneDeadRegistrants()
{
	TSet<FGameplayTag> Changed;
	Entries.RemoveAll([&Changed](const FVBUISlotEntry& Entry)
	{
		const bool bDead = (Entry.bHasRegistrant && !Entry.Registrant.IsValid()) || (Entry.bScoped && !Entry.Scope.IsValid());
		if (bDead)
		{
			Changed.Add(Entry.Slot);
		}

		return bDead;
	});
	for (const FGameplayTag& Slot : Changed)
	{
		NotifySlots(Slot);
	}
}

void UVBUISlotSubsystem::NotifySlots(const FGameplayTag& Slot)
{
	// 복사본을 돈다 - 자리의 위젯 구성이 자리를 더하거나 뺄 수 있다. 재진입한 자리 쪽 적용은 자리가 직렬화한다.
	const TArray<TWeakObjectPtr<UVBUISlotWidget>> Snapshot = Slots;
	for (const TWeakObjectPtr<UVBUISlotWidget>& Weak : Snapshot)
	{
		UVBUISlotWidget* SlotWidget = Weak.Get();
		if (SlotWidget && SlotWidget->GetSlotTag() == Slot && HasSlot(*SlotWidget))
		{
			SlotWidget->RefreshEntries();
		}
	}
}
