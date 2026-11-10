// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "CoreLoop/VBDifficultyStateComponent.h"

#include "CoreLoop/VBDifficultyConfig.h"
#include "CoreLoop/VBDifficultyRules.h"
#include "CoreLoop/VBDifficultySettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "VB/VB.h"
#include "CoreLoop/VBServerWrite.h"

UVBDifficultyStateComponent::UVBDifficultyStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
}

UVBDifficultyStateComponent* UVBDifficultyStateComponent::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->FindComponentByClass<UVBDifficultyStateComponent>() : nullptr;
}

float UVBDifficultyStateComponent::GetDamageScale(bool bVictimIsPlayer, bool bFinisherDamage) const
{
	return VBDifficultyRules::ResolveDamageScale(GetConfig(), CurrentLevel, bVictimIsPlayer, bFinisherDamage);
}

bool UVBDifficultyStateComponent::SetLevel(FGameplayTag Level)
{
	if (!HasWriteAuthority(TEXT("SetLevel")))
	{
		return false;
	}

	const UVBDifficultyConfig* Levels = GetConfig();
	if (!Levels || !Levels->FindLevel(Level))
	{
		VB_LOG(Warning, "난이도 %s 는 목록에 없다 - 바꾸지 않는다", *Level.ToString());
		return false;
	}

	if (Level == CurrentLevel)
	{
		return false;
	}

	VB_LOG(Log, "난이도 %s -> %s", *CurrentLevel.ToString(), *Level.ToString());
	CurrentLevel = Level;
	// 활성 세이브(메모리 사본)에도 바로 적는다 - 저장 없이 다른 레벨로 가면 새 월드가 InitializeComponent 에서 이 값을 읽는다.
	//   디스크에는 기존 저장 때만 간다. 이어하기는 디스크에서 다시 읽으니 저장 안 한 변경이 이어하기로 새지 않는다.
	if (UVBSaveGame* Active = UVBSaveGameSubsystem::FindActiveGameSave(this))
	{
		Active->Difficulty.Level = CurrentLevel;
	}

	OnDifficultyChanged.Broadcast();
	return true;
}

void UVBDifficultyStateComponent::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	Save.Difficulty.Level = CurrentLevel;
}

void UVBDifficultyStateComponent::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (HasWriteAuthority(TEXT("ReadSave")))
	{
		AdoptSavedLevel(Save.Difficulty.Level, TEXT("세이브 읽기"));
	}
}

void UVBDifficultyStateComponent::AdoptSavedLevel(FGameplayTag Saved, const TCHAR* From)
{
	const FGameplayTag Level = VBDifficultyRules::NormalizeLevel(GetConfig(), Saved);
	VB_CLOG(Saved.IsValid() && Level != Saved, Warning, "난이도: %s 의 단계 %s 가 목록에 없다 - 기본 단계 %s 로 둔다", From, *Saved.ToString(),
		*Level.ToString());
	if (Level != CurrentLevel)
	{
		CurrentLevel = Level;
		VB_LOG(Log, "난이도 %s (%s)", *CurrentLevel.ToString(), From);
		OnDifficultyChanged.Broadcast();
	}
}

void UVBDifficultyStateComponent::InitializeComponent()
{
	Super::InitializeComponent();
	UVBSaveParticipantSubsystem::Register(this);
	// 새 게임에는 ReadSave 가 오지 않는다 - 이 월드를 연 활성 세이브에서 처음 값을 읽는다. 세이브가 없으면(PIE 바로 시작) 기본 단계.
	//   GameState 가 생기는 때라 첫 피해, 시작 시네마틱보다 앞이다.
	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		const UVBSaveGame* Active = UVBSaveGameSubsystem::FindActiveGameSave(this);
		AdoptSavedLevel(Active ? Active->Difficulty.Level : FGameplayTag(), TEXT("월드 시작"));
	}
}

void UVBDifficultyStateComponent::UninitializeComponent()
{
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::UninitializeComponent();
}

void UVBDifficultyStateComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBDifficultyStateComponent, CurrentLevel);
}

void UVBDifficultyStateComponent::OnRep_CurrentLevel()
{
	OnDifficultyChanged.Broadcast();
}

const UVBDifficultyConfig* UVBDifficultyStateComponent::GetConfig() const
{
	// 한 번만 찾는다 - 경로가 깨져 못 읽으면 피해마다 다시 로드하지 않고 배수 1 로 간다(경고는 LoadConfig 가 한 번).
	if (!bConfigLookedUp)
	{
		bConfigLookedUp = true;
		Config = UVBDifficultySettings::LoadConfig();
	}

	return Config;
}

bool UVBDifficultyStateComponent::HasWriteAuthority(const TCHAR* What) const
{
	return VBServerWrite::IsAllowed(*this, TEXT("난이도"), What);
}

#if !UE_BUILD_SHIPPING
// 개발용: 단계를 바로 바꾼다(옵션 화면 없이 d1 을 PIE 로 확인). 서버, 싱글에서만 바뀐다 - SetLevel 의 HasWriteAuthority 를 그대로 지난다.
static void HandleSetDifficultyCommand(const TArray<FString>& Args, UWorld* World)
{
	UVBDifficultyStateComponent* Difficulty = World ? UVBDifficultyStateComponent::Get(World) : nullptr;
	const FGameplayTag Level = Args.Num() > 0
		? FGameplayTag::RequestGameplayTag(FName(*Args[0]), /*ErrorIfNotFound=*/false) : FGameplayTag();
	if (!Difficulty || !Level.IsValid())
	{
		VB_LOG(Warning, "VB.Difficulty.Set: %s", !Difficulty ? TEXT("이 월드에 난이도 주인이 없다") : TEXT("등록되지 않은 태그다(예: Difficulty.Hard)"));
		return;
	}

	Difficulty->SetLevel(Level);
}

static FAutoConsoleCommandWithWorldAndArgs GVBDifficultySetCommand(
	TEXT("VB.Difficulty.Set"),
	TEXT("개발용: 난이도 단계를 바꾼다. 인자 = 태그(Difficulty.Easy|Normal|Hard). 서버·싱글에서만 바뀐다."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleSetDifficultyCommand));
#endif
