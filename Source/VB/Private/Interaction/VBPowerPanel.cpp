// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBPowerPanel.h"

#include "Components/LightComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBInteractorComponent.h"
#include "Interaction/VBPowerSwitch.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "TimerManager.h"
#include "VB/VB.h"
#include "Interaction/VBSetupValidation.h"

AVBPowerPanel::AVBPowerPanel()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AVBPowerPanel::BeginPlay()
{
	Super::BeginPlay();
	FaultReason = ValidateSetup();
	if (HasFault())
	{
		VB_LOG(Error, "%s: 전력 제어함을 쓸 수 없다 - %s", *GetName(), *FaultReason);
		return;
	}

	StoryBinding.Refresh(this, GetWorld(), FSimpleDelegate::CreateUObject(this, &AVBPowerPanel::HandleStoryChanged));
	// 문, 스위치, 사실 저장소의 시작 순서에 기대지 않는다 - 다 준비된 뒤에 한 번 짓는다(PP-01).
	StartupDeadline = GetWorld()->GetTimeSeconds() + StartupTimeoutSeconds;
	if (!TryStart())
	{
		GetWorldTimerManager().SetTimer(StartupTimer, this, &AVBPowerPanel::HandleStartupTick, 0.1f, /*bLoop=*/true);
	}
}

void AVBPowerPanel::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 끝나는 동안 문의 Cancelled 가 다시 맞추기를 불러 새 요청을 내지 않게 먼저 멈춘다.
	bStarted = false;
	GetWorldTimerManager().ClearTimer(StartupTimer);
	StoryBinding.Disconnect();
	if (const FVBPowerCircuit* Circuit = FindDoorCircuit(); Circuit && Circuit->Door && DoorHandle.IsValid())
	{
		Circuit->Door->OnDoorMoveFinished.Remove(DoorHandle);
	}

	DoorHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

bool AVBPowerPanel::TryStart()
{
	const FVBPowerCircuit* DoorCircuit = FindDoorCircuit();
	bool bReady = StoryBinding.GetStory() != nullptr && (!DoorCircuit || DoorCircuit->Door->HasActorBegunPlay());
	for (const AVBPowerSwitch* Switch : Switches)
	{
		bReady &= Switch->HasActorBegunPlay();
	}

	if (!bReady)
	{
		return false;
	}

	if (HasAuthority() && DoorCircuit)
	{
		// 결과를 첫 요청보다 먼저 묶는다.
		DoorHandle = DoorCircuit->Door->OnDoorMoveFinished.AddUObject(this, &AVBPowerPanel::HandleDoorFinished);
	}

	bStarted = true;
	Reconcile();
	ApplyLights();
	return true;
}

void AVBPowerPanel::HandleStartupTick()
{
	if (TryStart())
	{
		GetWorldTimerManager().ClearTimer(StartupTimer);
		return;
	}
	// 시한은 서버만 본다. 클라는 복제가 늦게 와도 결함으로 굳히지 않고 조용히 계속 기다린다(등만 늦게 맞는다).
	if (HasAuthority() && GetWorld()->GetTimeSeconds() >= StartupDeadline)
	{
		GetWorldTimerManager().ClearTimer(StartupTimer);
		FaultReason = TEXT("시작 때 문·스위치·사실 저장소가 준비되지 않았다");
		VB_LOG(Error, "%s: 전력 제어함을 쓸 수 없다 - %s", *GetName(), *FaultReason);
	}
}

void AVBPowerPanel::HandleStoryChanged()
{
	if (!bStarted)
	{
		return; // 첫 맞추기는 TryStart 가 한다.
	}

	if (!StoryBinding.GetStory())
	{
		if (!GetWorld()->bIsTearingDown)
		{
			VB_LOG(Warning, "%s: 사실 저장소가 사라졌다 - 다시 묶일 때까지 장치를 그대로 둔다", *GetName());
		}

		return;
	}

	Reconcile();
	ApplyLights();
}

void AVBPowerPanel::RequestToggle(AVBPowerSwitch& Switch, AActor* Interactor)
{
	if (!HasAuthority())
	{
		return;
	}
	// 이 누름 동안만 누른 사람으로 둔다 - 나중의 이야기 알림이 낸 요청의 결과를 엉뚱한 사람에게 보이지 않게.
	TGuardValue<TWeakObjectPtr<AActor>> Pressing(PressRequester, Interactor);
	if (!IsReady())
	{
		Refuse(Interactor, DoorUnavailableText, TEXT("제어함이 준비되지 않았다(시작 전 · 결함 · 사실 저장소 없음)"));
		return;
	}

	if (IsOpened())
	{
		Refuse(Interactor, CompletedText, TEXT("출구가 이미 열렸다(걸쇠)"));
		return;
	}
	// 문이 움직이는 동안에는 배분을 바꾸지 않는다.
	if (DoorPhase != EDoorPhase::None)
	{
		Refuse(Interactor, DoorMovingText, TEXT("문이 움직이는 중"));
		return;
	}

	TArray<FName> Powered = Allocations[GetAllocationIndex()].Powered;
	const FName Circuit = Switch.GetCircuitId();
	if (Powered.Contains(Circuit))
	{
		Powered.Remove(Circuit);
	}
	else
	{
		Powered.Add(Circuit);
	}
	// 공급 초과는 배분표에 없다(배치 검사가 표의 모든 배분을 Supply 안으로 묶는다) - 표에 있는지만 본다.
	const int32 Next = FindAllocation(Powered);
	if (Next == INDEX_NONE)
	{
		Refuse(Interactor, OverSupplyText, TEXT("공급을 넘거나 배분표에 없는 배분"));
		return;
	}
	// 문 회로를 다시 누르는 것이 재시도다 - 붙든 열기/닫기 실패를 푼다(PD-02).
	if (const FVBPowerCircuit* DoorCircuit = FindDoorCircuit(); DoorCircuit && DoorCircuit->Id == Circuit)
	{
		bOpenFailed = false;
		bCloseFailed = false;
	}

	WriteCounter(Next);
	// 카운터가 바뀌면 이야기 알림이 Reconcile 을 부른다. 실패만 풀린 경우를 위해 한 번 더 맞춘다(같은 일을 두 번 하지 않는다).
	Reconcile();
}

void AVBPowerPanel::Reconcile()
{
	if (!IsReady() || !HasAuthority())
	{
		return;
	}

	const int32 Raw = ReadCounter();
	if (!Allocations.IsValidIndex(Raw))
	{
		VB_LOG(Error, "%s: 배분 카운터 %d 가 배분표 밖이다 - 처음 배분으로 고친다(효과 없음)", *GetName(), Raw);
		WriteCounter(0); // 이야기 알림이 다시 맞춘다.
		return;
	}

	const FVBPowerCircuit* DoorCircuit = FindDoorCircuit();
	const bool bOpened = IsOpened();
	if (bOpened && DoorCircuit && !IsPoweredIn(Raw, DoorCircuit->Id))
	{
		// 완료 사실이 이긴다: 문 회로가 켜진 배분으로 고친다.
		const int32 DoorIndex = Allocations.IndexOfByPredicate(
			[DoorCircuit](const FVBPowerAllocation& A) { return A.Powered.Contains(DoorCircuit->Id); });
		VB_LOG(Error, "%s: 출구가 열렸는데 배분 %d 에 문 회로가 없다 - 배분 %d 로 고친다(효과 없음)", *GetName(), Raw, DoorIndex);
		WriteCounter(DoorIndex);
		return;
	}

	for (AVBPowerSwitch* Switch : Switches)
	{
		Switch->GetInteractable()->SetActivated(IsPoweredIn(Raw, Switch->GetCircuitId()));
	}

	if (!DoorCircuit)
	{
		return;
	}

	AVBPhysicalDoor* Door = DoorCircuit->Door;
	if (bOpened)
	{
		// 걸쇠: 열린 끝으로 다시 짓는다(불러온 완료 포함, 효과 없음). 진행 중인 요청은 버린다.
		if (!Door->IsOpen() || DoorPhase != EDoorPhase::None)
		{
			DoorSerial = 0;
			DoorPhase = EDoorPhase::None;
			Door->SetPoseInstant(true);
		}

		return;
	}

	if (DoorPhase != EDoorPhase::None)
	{
		return; // 움직이는 중 - 그 결과가 다음을 정한다.
	}

	const bool bWantOpen = IsPoweredIn(Raw, DoorCircuit->Id);
	if (bWantOpen && Door->IsOpen() && !Door->IsBlocked())
	{
		// 열린 끝에 서 있는데 걸쇠가 없다(열림 도착을 놓친 경우) - 지금 건다. 효과는 처음 걸 때 한 번.
		Latch();
	}
	else if (bWantOpen && !bOpenFailed && (!Door->IsOpen() || Door->IsBlocked()))
	{
		StartDoor(true);
	}
	else if (!bWantOpen && !bCloseFailed && (Door->IsOpen() || Door->IsBlocked()))
	{
		StartDoor(false);
	}
}

void AVBPowerPanel::StartDoor(const bool bOpen)
{
	AVBPhysicalDoor* Door = FindDoorCircuit()->Door;
	FString Why;
	const uint32 Serial = Door->RequestMove(bOpen, &Why);
	if (Serial == 0)
	{
		(bOpen ? bOpenFailed : bCloseFailed) = true;
		VB_LOG(Error, "%s: 문이 %s 요청을 받지 않았다 - %s", *GetName(), bOpen ? TEXT("열기") : TEXT("닫기"), *Why);
		Refuse(PressRequester.Get(), DoorUnavailableText, *Why);
		return;
	}

	DoorSerial = Serial;
	DoorPhase = bOpen ? EDoorPhase::Open : EDoorPhase::Close;
	DoorRequester = PressRequester;
}

void AVBPowerPanel::HandleDoorFinished(const uint32 Serial, const EVBDoorMoveResult Result)
{
	if (Serial == 0 || Serial != DoorSerial)
	{
		return; // 지금 요청이 아니다(앞 요청의 늦은 결과).
	}

	const EDoorPhase Phase = DoorPhase;
	DoorSerial = 0;
	DoorPhase = EDoorPhase::None;
	const FVBPowerCircuit* DoorCircuit = FindDoorCircuit();
	if (Phase == EDoorPhase::Open)
	{
		if (Result == EVBDoorMoveResult::Reached && IsReady() && IsPoweredIn(GetAllocationIndex(), DoorCircuit->Id))
		{
			// 열기의 끝에 닿았을 때만 걸쇠(P3). 닫기의 결과는 여기로 오지 않는다.
			Latch();
		}
		else if (Result == EVBDoorMoveResult::Blocked)
		{
			bOpenFailed = true;
			Refuse(DoorRequester.Get(), OpenBlockedText, TEXT("문이 열리다 막혔다"));
		}
	}
	else if (Phase == EDoorPhase::Close && Result == EVBDoorMoveResult::Blocked)
	{
		bCloseFailed = true;
		Refuse(DoorRequester.Get(), CloseBlockedText, TEXT("문이 닫히다 막혔다"));
	}

	Reconcile();
}

void AVBPowerPanel::Latch()
{
	if (IsOpened())
	{
		return;
	}

	const FVBRuleContext Context = FVBRuleContext::Make(this, RequesterState());
	FVBEffect_SetFact Set;
	Set.Fact = OpenedFact;
	Set.bOn = true;
	Set.Apply(Context);
	VBRules::ApplyAll(OnOpenedEffects, Context);
}

void AVBPowerPanel::ApplyLights() const
{
	if (!IsReady())
	{
		return;
	}

	const int32 Index = GetAllocationIndex();
	for (const FVBPowerCircuit& Circuit : Circuits)
	{
		const bool bOn = IsPoweredIn(Index, Circuit.Id);
		for (const AActor* LightActor : Circuit.Lights)
		{
			TInlineComponentArray<ULightComponent*> Lights(LightActor);
			for (ULightComponent* Light : Lights)
			{
				Light->SetVisibility(bOn);
			}
		}
	}
}

int32 AVBPowerPanel::GetAllocationIndex() const
{
	const int32 Raw = ReadCounter();
	return Allocations.IsValidIndex(Raw) ? Raw : 0;
}

bool AVBPowerPanel::IsOpened() const
{
	const UVBStoryStateComponent* Story = StoryBinding.GetStory();
	return Story && OpenedFact.IsValid() && Story->HasFact(OpenedFact);
}

const FVBPowerCircuit* AVBPowerPanel::FindDoorCircuit() const
{
	return Circuits.FindByPredicate([](const FVBPowerCircuit& Circuit) { return Circuit.Door != nullptr; });
}

bool AVBPowerPanel::IsPoweredIn(const int32 AllocationIndex, const FName CircuitId) const
{
	return Allocations.IsValidIndex(AllocationIndex) && Allocations[AllocationIndex].Powered.Contains(CircuitId);
}

int32 AVBPowerPanel::FindAllocation(const TArray<FName>& Powered) const
{
	return Allocations.IndexOfByPredicate([&Powered](const FVBPowerAllocation& Allocation)
	{
		return Allocation.Powered.Num() == Powered.Num()
			&& !Powered.ContainsByPredicate([&Allocation](const FName& Id) { return !Allocation.Powered.Contains(Id); });
	});
}

int32 AVBPowerPanel::ReadCounter() const
{
	const UVBStoryStateComponent* Story = StoryBinding.GetStory();
	return Story ? Story->GetCounter(AllocationCounter) : 0;
}

void AVBPowerPanel::WriteCounter(const int32 Index) const
{
	// 쓰는 길은 규칙 효과 하나(FVBEffect_SetCounter) - 같은 값이면 바뀜도 알림도 없다.
	FVBEffect_SetCounter Set;
	Set.Counter = AllocationCounter;
	Set.Value = Index;
	Set.Apply(FVBRuleContext::Make(this, RequesterState()));
}

void AVBPowerPanel::Refuse(AActor* Interactor, const FText& Text, const TCHAR* Why) const
{
	VB_LOG(Log, "%s: 거절 - %s", *GetName(), Why);
	UVBInteractorComponent* Target = Interactor ? Interactor->FindComponentByClass<UVBInteractorComponent>() : nullptr;
	if (Target && !Text.IsEmpty())
	{
		Target->ClientInteractionRefused(Text);
	}
}

APlayerState* AVBPowerPanel::RequesterState() const
{
	// 누른 사람(또는 이 문 요청을 낸 사람)의 것. 없으면(불러온 P2 를 다시 여는 등) 첫 플레이어 - 혼자 하는 게임의 그 사람이다.
	const APawn* Pawn = Cast<APawn>(PressRequester.IsValid() ? PressRequester.Get() : DoorRequester.Get());
	if (Pawn)
	{
		return Pawn->GetPlayerState();
	}

	const APlayerController* First = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return First ? First->PlayerState.Get() : nullptr;
}

FString AVBPowerPanel::ValidateSetup() const
{
	if (Circuits.IsEmpty() || Allocations.IsEmpty() || !AllocationCounter.IsValid())
	{
		return TEXT("회로·배분표·배분 카운터 중 빈 것이 있다");
	}

	TSet<FName> Ids;
	int32 Doors = 0;
	for (const FVBPowerCircuit& Circuit : Circuits)
	{
		if (Circuit.Id.IsNone() || Ids.Contains(Circuit.Id))
		{
			return TEXT("회로 Id 가 비었거나 겹친다");
		}

		Ids.Add(Circuit.Id);
		for (const AActor* LightActor : Circuit.Lights)
		{
			if (!LightActor || !LightActor->FindComponentByClass<ULightComponent>())
			{
				return FString::Printf(TEXT("회로 %s 의 등에 빛 컴포넌트가 없다"), *Circuit.Id.ToString());
			}
		}

		if (const AVBPhysicalDoor* Door = Circuit.Door)
		{
			++Doors;
			// 제어함이 모는 문은 손으로 열리지 않고 스스로 저장하지 않는다 - 상태는 카운터, 사실에서 다시 짓는다(13절 3, 6항).
			if (Door->GetInteractable()->IsInteractionEnabled() || !Door->GetInteractable()->GetSaveKey().IsNone())
			{
				return TEXT("제어함의 문은 상호작용과 저장을 꺼야 한다");
			}
			// 문을 모는 쪽은 하나다 - 이야기 닫힘을 따르는 문을 제어함이 함께 몰면 서로의 요청을 덮는다.
			if (Door->HasStoryControl())
			{
				return TEXT("제어함의 문은 이야기 닫힘(ClosedWhile · ClosedWhileOccupied)을 비워야 한다");
			}
		}
	}

	if (Doors > 1)
	{
		return TEXT("문 회로가 둘 이상이다");
	}

	if (Doors == 1 && !OpenedFact.IsValid())
	{
		return TEXT("문 회로가 있는데 열림 사실이 비었다");
	}

	if (const FVBPowerCircuit* DoorCircuit = FindDoorCircuit();
		DoorCircuit && !Allocations.ContainsByPredicate(
			[DoorCircuit](const FVBPowerAllocation& A) { return A.Powered.Contains(DoorCircuit->Id); }))
	{
		return TEXT("문 회로에 전기가 들어가는 배분이 없다 - 문을 열 길이 없다");
	}

	for (int32 Index = 0; Index < Allocations.Num(); ++Index)
	{
		const TArray<FName>& Powered = Allocations[Index].Powered;
		if (Powered.Num() > Supply || Powered.ContainsByPredicate([&Ids](const FName& Id) { return !Ids.Contains(Id); }))
		{
			return FString::Printf(TEXT("배분 %d 이 공급을 넘거나 없는 회로를 쓴다"), Index);
		}

		if (FindAllocation(Powered) != Index)
		{
			return FString::Printf(TEXT("배분 %d 이 앞 배분과 같다"), Index);
		}
	}

	for (const AVBPowerSwitch* Switch : Switches)
	{
		if (!Switch || Switch->GetPanel() != this || !Ids.Contains(Switch->GetCircuitId()))
		{
			return TEXT("스위치가 비었거나 다른 제어함·없는 회로를 가리킨다");
		}

		const UVBInteractableComponent* Input = Switch->GetInteractable();
		// 거절된 누름이 보상을 내지 않게 - 상호작용의 완료 효과, 이벤트 태그는 비어 있어야 한다(PP-05).
		if (Input->HasCompletionOutputs() || Input->GetConfig()->Mode != EVBInteractMode::Tap)
		{
			return TEXT("스위치는 누름(Tap)이고 완료 효과·사건 표지가 비어 있어야 한다");
		}
	}

	return FString();
}

#if WITH_EDITOR
EDataValidationResult AVBPowerPanel::IsDataValid(FDataValidationContext& Context) const
{
	return VBReportSetupProblem(Context, *this, ValidateSetup(), Super::IsDataValid(Context));
}
#endif
