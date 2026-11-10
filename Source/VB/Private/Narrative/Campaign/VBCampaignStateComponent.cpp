// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Campaign/VBCampaignStateComponent.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "VB/VB.h"
#include "CoreLoop/VBServerWrite.h"

namespace
{
	bool IsNamed(const FVBCampaignSaveData& Campaign)
	{
		return Campaign.CampaignId.IsValid() && Campaign.RunId.IsValid() && Campaign.RunIndex >= 1;
	}
}

UVBCampaignStateComponent::UVBCampaignStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
}

UVBCampaignStateComponent* UVBCampaignStateComponent::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->FindComponentByClass<UVBCampaignStateComponent>() : nullptr;
}

void UVBCampaignStateComponent::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	Save.Campaign = State;
}

bool UVBCampaignStateComponent::RecordClear(const FVBClearSummary& Summary)
{
	if (!HasWriteAuthority(TEXT("RecordClear")))
	{
		return false;
	}

	if (!Summary.IsSet() || !Summary.Ending.IsValid())
	{
		VB_LOG(Warning, "Campaign RecordClear refused: clear id or ending tag is unset");
		return false;
	}

	State.LastClear = Summary;
	State.SeenEndings.AddTag(Summary.Ending);
	if (UVBSaveGame* Active = UVBSaveGameSubsystem::FindActiveGameSave(this))
	{
		Active->Campaign = State;
	}

	return true;
}

void UVBCampaignStateComponent::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (HasWriteAuthority(TEXT("ReadSave")))
	{
		Adopt(Save.Campaign, TEXT("세이브 읽기"));
	}
}

void UVBCampaignStateComponent::Adopt(const FVBCampaignSaveData& Saved, const TCHAR* From)
{
	if (IsNamed(Saved))
	{
		State = Saved;
		// 결말 이름이 빈 앞 회차 기록은 손상이다 - 두면 결말 부정 조건이 참이 된다.
		if (State.Parent.IsSet() && !State.Parent.Ending.IsValid())
		{
			VB_LOG(Warning, "회차: %s 의 앞 회차 기록에 결말 이름이 없다 - 앞 회차 기록을 버린다", From);
			State.Parent = FVBClearSummary();
		}
	}
	else if (!IsNamed(State))
	{
		// 이름 없는 회차에 앞 회차 기록만 있으면 손상이다 - 버리고 1회차로 시작한다.
		VB_CLOG(Saved.Parent.IsSet(), Warning, "회차: %s 의 회차 이름이 비었는데 앞 회차 기록(%s)이 있다 - 1회차로 새로 시작한다",
			From, *Saved.Parent.Ending.ToString());
		State = FVBCampaignSaveData();
		State.CampaignId = FGuid::NewGuid();
		State.RunId = FGuid::NewGuid();
		State.RunIndex = 1;
	}
	// 이미 이름이 있으면 이름 없는 세이브가 지우지 않는다(옛 슬롯 이어하기: InitializeComponent 가 먼저 이름을 붙임).
	// 불러오기는 늘 새 월드라는 전제다 - 깨지면 여기를 다시 본다.

	// 활성 세이브에도 적는다 - 저장 없이 다음 레벨로 가면 새 월드가 InitializeComponent 에서 이 값을 읽는다.
	if (UVBSaveGame* Active = UVBSaveGameSubsystem::FindActiveGameSave(this))
	{
		Active->Campaign = State;
	}

	VB_LOG(Log, "회차 %d (%s), 앞 회차 결말 %s", State.RunIndex, From,
		State.Parent.IsSet() ? *State.Parent.Ending.ToString() : TEXT("없음"));
}

void UVBCampaignStateComponent::InitializeComponent()
{
	Super::InitializeComponent();
	UVBSaveParticipantSubsystem::Register(this);
	// 새 게임에는 ReadSave 가 오지 않는다 - 이 월드를 연 활성 세이브에서 처음 값을 읽는다. 세이브가 없으면(PIE 바로 시작) 1회차.
	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		const UVBSaveGame* Active = UVBSaveGameSubsystem::FindActiveGameSave(this);
		Adopt(Active ? Active->Campaign : FVBCampaignSaveData(), TEXT("월드 시작"));
	}
}

void UVBCampaignStateComponent::UninitializeComponent()
{
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::UninitializeComponent();
}

void UVBCampaignStateComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBCampaignStateComponent, State);
}

bool UVBCampaignStateComponent::HasWriteAuthority(const TCHAR* What) const
{
	return VBServerWrite::IsAllowed(*this, TEXT("회차"), What);
}
