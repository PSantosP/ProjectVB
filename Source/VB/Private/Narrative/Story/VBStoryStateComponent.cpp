// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Story/VBStoryStateComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "VB/VB.h"
#include "CoreLoop/VBServerWrite.h"

UVBStoryStateComponent::UVBStoryStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
}

UVBStoryStateComponent* UVBStoryStateComponent::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->FindComponentByClass<UVBStoryStateComponent>() : nullptr;
}

void UVBStoryStateComponent::SetFact(FGameplayTag Fact, bool bOn)
{
	if (HasWriteAuthority(TEXT("SetFact")) && State.SetFact(Fact, bOn))
	{
		VB_LOG(Log, "사실 %s -> %s", *Fact.ToString(), bOn ? TEXT("켬") : TEXT("끔"));
		OnStoryStateChanged.Broadcast();
	}
}

void UVBStoryStateComponent::AddToCounter(FGameplayTag Counter, int32 Delta)
{
	if (!HasWriteAuthority(TEXT("AddToCounter")) || !Counter.IsValid() || Delta == 0)
	{
		return;
	}

	const int32 NewValue = State.AddToCounter(Counter, Delta);
	VB_LOG(Log, "카운터 %s %+d -> %d", *Counter.ToString(), Delta, NewValue);
	OnStoryStateChanged.Broadcast();
}

void UVBStoryStateComponent::SetCounter(FGameplayTag Counter, int32 Value)
{
	if (!HasWriteAuthority(TEXT("SetCounter")) || !State.SetCounter(Counter, Value))
	{
		return;
	}

	VB_LOG(Log, "카운터 %s = %d", *Counter.ToString(), Value);
	OnStoryStateChanged.Broadcast();
}

void UVBStoryStateComponent::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	Save.Progress.Story = State;
}

void UVBStoryStateComponent::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (!HasWriteAuthority(TEXT("ReadSave")))
	{
		return;
	}

	State = Save.Progress.Story;
	++SaveReadSerial;
	VB_LOG(Log, "사실 로드: 사실 %d, 카운터 %d", State.Facts.Num(), State.Counters.Num());
	OnStoryStateChanged.Broadcast();
}

void UVBStoryStateComponent::InitializeComponent()
{
	Super::InitializeComponent();
	UVBSaveParticipantSubsystem::Register(this);
}

void UVBStoryStateComponent::UninitializeComponent()
{
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::UninitializeComponent();
}

void UVBStoryStateComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBStoryStateComponent, State);
	DOREPLIFETIME(UVBStoryStateComponent, SaveReadSerial);
}

void UVBStoryStateComponent::OnRep_State()
{
	OnStoryStateChanged.Broadcast();
}

bool UVBStoryStateComponent::HasWriteAuthority(const TCHAR* What) const
{
	return VBServerWrite::IsAllowed(*this, TEXT("사실 저장소"), What);
}

#if !UE_BUILD_SHIPPING
// 개발용: 사실 하나를 켜고 끈다(챕터 제목 카드, 지역 열림처럼 사실에 기대는 표시를 레벨 배선 없이 확인한다).
//   쓰기는 위 HasWriteAuthority 를 그대로 지난다 - 클라 콘솔에서 치면 경고만 남고 아무것도 안 바뀐다. 기능 코드는 이 길을 쓰지 않는다(규칙의 효과가 유일한 쓰는 길).
static void HandleSetFactCommand(const TArray<FString>& Args, UWorld* World)
{
	UVBStoryStateComponent* Story = World ? UVBStoryStateComponent::Get(World) : nullptr;
	const FGameplayTag Fact = Args.Num() > 0
		? FGameplayTag::RequestGameplayTag(FName(*Args[0]), /*ErrorIfNotFound=*/false) : FGameplayTag();
	if (!Story || !Fact.IsValid())
	{
		VB_LOG(Warning, "VB.Story.SetFact: %s", !Story ? TEXT("이 월드에 사실 저장소가 없다") : TEXT("등록되지 않은 태그다(DefaultGameplayTags.ini)"));
		return;
	}

	const bool bOn = Args.Num() < 2 || Args[1] != TEXT("0");
	Story->SetFact(Fact, bOn);
	VB_LOG(Log, "VB.Story.SetFact: %s = %s", *Fact.ToString(), bOn ? TEXT("켬") : TEXT("끔"));
}

static FAutoConsoleCommandWithWorldAndArgs GVBStorySetFactCommand(
	TEXT("VB.Story.SetFact"),
	TEXT("개발용: 세계 사실 하나를 켜거나 끈다. 인자 = 태그 [1|0] (기본 1). 서버·싱글에서만 바뀐다."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleSetFactCommand));
#endif
