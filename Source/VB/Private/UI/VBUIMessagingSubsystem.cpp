// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBUIMessagingSubsystem.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Engine/LocalPlayer.h"
#include "UI/VBUIManagerSubsystem.h"
#include "VB/VB.h"

UVBUIMessagingSubsystem* UVBUIMessagingSubsystem::Get(const UObject* WorldContextObject)
{
	const UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(WorldContextObject);
	const ULocalPlayer* LocalPlayer = UI ? UI->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UVBUIMessagingSubsystem>() : nullptr;
}

void UVBUIMessagingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 표시 쪽을 먼저 초기화한다. 실패, 취소된 push 는 결론을 내거나 다시 시도해야 한다.
	if (UVBUIManagerSubsystem* UI = Collection.InitializeDependency<UVBUIManagerSubsystem>())
	{
		PushEndedHandle = UI->OnScreenPushEnded.AddUObject(this, &UVBUIMessagingSubsystem::HandleScreenPushEnded);
	}
}

void UVBUIMessagingSubsystem::Deinitialize()
{
	bDeinitializing = true;
	if (PresentTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PresentTicker);
		PresentTicker.Reset();
	}

	if (UVBUIManagerSubsystem* UI = GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<UVBUIManagerSubsystem>() : nullptr)
	{
		UI->OnScreenPushEnded.Remove(PushEndedHandle);
	}

	PushEndedHandle.Reset();
	Queue.Reset();
	DialogState = EDialogState::Idle;
	Super::Deinitialize();
}

FVBConfirmationHandle UVBUIMessagingSubsystem::ShowConfirmation(const FVBConfirmationRequest& Request,
	FVBConfirmationResultDelegate OnResult)
{
	if (bDeinitializing)
	{
		return FVBConfirmationHandle();
	}

	FPendingConfirmation& Entry = Queue.AddDefaulted_GetRef();
	Entry.Handle.Id = NextId++;
	if (NextId == 0)
	{
		NextId = 1;
	}

	Entry.Request = Request;
	Entry.OnResult = MoveTemp(OnResult);
	const FVBConfirmationHandle Handle = Entry.Handle;

	if (DialogState == EDialogState::Idle)
	{
		// 풀링된 대화상자를 자기 닫기, 클릭 콜백 도중에 다시 쓰지 않는다.
		if (PresentDeferDepth > 0)
		{
			SchedulePresent();
		}
		else
		{
			Present();
		}
	}

	return IsPending(Handle) ? Handle : FVBConfirmationHandle();
}

void UVBUIMessagingSubsystem::SetBody(FVBConfirmationHandle Handle, const FText& Body)
{
	const int32 Index = FindIndex(Handle);
	if (Index == INDEX_NONE)
	{
		return;
	}

	if (Queue[Index].Request.Body.EqualTo(Body))
	{
		return;
	}

	Queue[Index].Request.Body = Body;
	if (Index == 0)
	{
		OnPresentedChanged.Broadcast();
	}
}

void UVBUIMessagingSubsystem::Cancel(FVBConfirmationHandle Handle)
{
	const int32 Index = FindIndex(Handle);
	if (Index != INDEX_NONE)
	{
		Resolve(Index, EVBConfirmationResult::Cancelled);
	}
}

bool UVBUIMessagingSubsystem::IsPending(FVBConfirmationHandle Handle) const
{
	return FindIndex(Handle) != INDEX_NONE;
}

const FVBConfirmationRequest* UVBUIMessagingSubsystem::GetPresented(FVBConfirmationHandle& OutHandle) const
{
	if (Queue.Num() == 0)
	{
		OutHandle = FVBConfirmationHandle();
		return nullptr;
	}

	OutHandle = Queue[0].Handle;
	return &Queue[0].Request;
}

void UVBUIMessagingSubsystem::Answer(FVBConfirmationHandle Handle, EVBConfirmationResult Result)
{
	if (DialogState != EDialogState::Shown || Queue.Num() == 0 || !(Queue[0].Handle == Handle))
	{
		return;
	}

	if (Result == EVBConfirmationResult::Declined && !Queue[0].Request.bOfferDecline)
	{
		Result = EVBConfirmationResult::Cancelled;
	}

	Resolve(0, Result);
}

void UVBUIMessagingSubsystem::NotifyDialogActivated()
{
	DialogState = EDialogState::Shown;
}

void UVBUIMessagingSubsystem::NotifyDialogDeactivated()
{
	const bool bWasShown = DialogState == EDialogState::Shown;
	DialogState = EDialogState::Idle;
	++PresentDeferDepth;
	if (bWasShown && Queue.Num() > 0)
	{
		Resolve(0, EVBConfirmationResult::Cancelled);
	}

	--PresentDeferDepth;
	if (Queue.Num() > 0)
	{
		SchedulePresent();
	}
}

void UVBUIMessagingSubsystem::Present()
{
	UVBUIManagerSubsystem* UI = GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<UVBUIManagerSubsystem>() : nullptr;
	if (!UI)
	{
		FailAll(TEXT("UI manager unavailable"));
		return;
	}
	// 길이 0 레이어는 닫힌 위젯을 동기로 치운다. 한 번 미뤄 다시 본 뒤에도 그 약속이 깨지면
	// 큐를 묶어 두지 않고 실패로 닫는다.
	if (UI->IsScreenOpen(VBGameplayTags::UI_Screen_Confirmation))
	{
		FailAll(TEXT("closed confirmation still listed"));
		return;
	}

	DialogState = EDialogState::Requested;
	UI->PushScreen(VBGameplayTags::UI_Screen_Confirmation);
}

void UVBUIMessagingSubsystem::SchedulePresent()
{
	if (PresentTicker.IsValid())
	{
		return;
	}

	PresentTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UVBUIMessagingSubsystem::HandlePresentTick));
}

bool UVBUIMessagingSubsystem::HandlePresentTick(float DeltaSeconds)
{
	PresentTicker.Reset();
	if (!bDeinitializing && Queue.Num() > 0 && DialogState == EDialogState::Idle)
	{
		Present();
	}

	return false;
}

void UVBUIMessagingSubsystem::Resolve(int32 Index, EVBConfirmationResult Result)
{
	if (!Queue.IsValidIndex(Index))
	{
		return;
	}
	// 관찰자, 결과보다 먼저 뺀다 - 재귀 취소가 이미 끝난 것으로 봐야 한다.
	FPendingConfirmation Entry = MoveTemp(Queue[Index]);
	Queue.RemoveAt(Index);
	++PresentDeferDepth;
	if (Index == 0)
	{
		OnPresentedChanged.Broadcast();
	}
	// weak 바인딩은 조용히 만료된다. 콜백 중에 들어온 요청은 다음 tick 에 띄운다.
	if (!bDeinitializing)
	{
		Entry.OnResult.ExecuteIfBound(Result);
	}

	--PresentDeferDepth;
}

void UVBUIMessagingSubsystem::FailAll(const TCHAR* Why)
{
	VB_LOG(Warning, "[VBUI] Cannot present confirmation (%s); failing %d remaining requests", Why, Queue.Num());
	// 콜백 전에 이번 회차를 떼어 낸다. 새 요청은 다음 회차에 속한다.
	DialogState = EDialogState::Idle;
	TArray<FPendingConfirmation> Failed = MoveTemp(Queue);
	Queue.Reset();
	++PresentDeferDepth;
	OnPresentedChanged.Broadcast();
	for (FPendingConfirmation& Entry : Failed)
	{
		if (bDeinitializing)
		{
			break;
		}

		Entry.OnResult.ExecuteIfBound(EVBConfirmationResult::Failed);
	}

	--PresentDeferDepth;
}

void UVBUIMessagingSubsystem::HandleScreenPushEnded(FGameplayTag ScreenTag, EVBScreenPushEnd Reason)
{
	if (ScreenTag != VBGameplayTags::UI_Screen_Confirmation || DialogState != EDialogState::Requested)
	{
		return;
	}

	if (Reason == EVBScreenPushEnd::Failed)
	{
		FailAll(TEXT("screen route/layout/class/layer"));
		return;
	}

	DialogState = EDialogState::Idle;
	if (Queue.Num() > 0)
	{
		SchedulePresent();
	}
}

bool FVBConfirmationRequester::Ask(const UObject* Requester, const FVBConfirmationRequest& Request, FVBConfirmationResultDelegate OnResult)
{
	if (IsPending(Requester))
	{
		return false;
	}

	UVBUIMessagingSubsystem* Messaging = UVBUIMessagingSubsystem::Get(Requester);
	if (!Messaging)
	{
		VB_LOG(Warning, "[VBUI] %s has no local confirmation owner; action remains unconfirmed", *GetNameSafe(Requester));
		return false;
	}

	Owner = Messaging;
	const uint64 CallRevision = ++Revision;
	const FVBConfirmationHandle NewHandle = Messaging->ShowConfirmation(Request, MoveTemp(OnResult));
	// 동기 실패는 이 호출이 돌아오기 전에 Ask/Cancel 로 다시 들어올 수 있다.
	if (Revision == CallRevision)
	{
		Handle = NewHandle;
	}

	return Revision == CallRevision && Messaging->IsPending(NewHandle);
}

void FVBConfirmationRequester::Cancel(const UObject* /*Requester*/)
{
	UVBUIMessagingSubsystem* Messaging = Owner.Get();
	const FVBConfirmationHandle CancelledHandle = Handle;
	Handle = FVBConfirmationHandle();
	Owner.Reset();
	++Revision;
	// 외부 콜백 전에 비운다. 재진입한 Ask 는 자기 새 핸들을 가진다.
	if (Messaging && CancelledHandle.IsValid())
	{
		Messaging->Cancel(CancelledHandle);
	}
}

void FVBConfirmationRequester::SetBody(const UObject* /*Requester*/, const FText& Body)
{
	if (UVBUIMessagingSubsystem* Messaging = Handle.IsValid() ? Owner.Get() : nullptr)
	{
		Messaging->SetBody(Handle, Body);
	}
}

bool FVBConfirmationRequester::IsPending(const UObject* /*Requester*/) const
{
	const UVBUIMessagingSubsystem* Messaging = Handle.IsValid() ? Owner.Get() : nullptr;
	return Messaging && Messaging->IsPending(Handle);
}

int32 UVBUIMessagingSubsystem::FindIndex(FVBConfirmationHandle Handle) const
{
	if (!Handle.IsValid())
	{
		return INDEX_NONE;
	}

	return Queue.IndexOfByPredicate([Handle](const FPendingConfirmation& Entry) { return Entry.Handle == Handle; });
}
