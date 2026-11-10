// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "SaveSystem/VBSaveGameSubsystem.h"

#include "SaveSystem/VBSaveGame.h"
#include "CoreLoop/VBDifficultyRules.h"
#include "CoreLoop/VBDifficultySettings.h"
#include "Data/VBGameFlowSettings.h"
#include "Narrative/Progress/VBChapterDefinition.h"
#include "Narrative/Progress/VBProgressCatalogSubsystem.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "Settings/VBUserSettingsStore.h"
#include "Subsystems/SubsystemCollection.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Components/ActorComponent.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "VB/VB.h"

namespace
{
	// 단일 로컬 플레이어 가정. 분할화면/멀티 로컬 지원 시 확장.
	constexpr int32 GUserIndex = 0;
}

struct UVBSaveGameSubsystem::FVBCapturedSave
{
	TArray<TStrongObjectPtr<UObject>> Writers;
	TStrongObjectPtr<UVBSaveGame> Active;
	TStrongObjectPtr<UVBSaveGame> Game;
};

UVBSaveGame* UVBSaveGameSubsystem::FindActiveGameSave(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UVBSaveGameSubsystem* SaveSubsystem = GameInstance ? GameInstance->GetSubsystem<UVBSaveGameSubsystem>() : nullptr;
	return SaveSubsystem ? SaveSubsystem->GetActiveGameSave() : nullptr;
}

UVBSaveGameSubsystem* UVBSaveGameSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine)
	{
		return nullptr;
	}

	if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			return GameInstance->GetSubsystem<UVBSaveGameSubsystem>();
		}
	}

	return nullptr;
}

void UVBSaveGameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bStopping = false;
	// 설정 로드 시점이 이 서브시스템보다 늦어지지 않게 한다.
	Collection.InitializeDependency<UVBUserSettingsStore>();
}

void UVBSaveGameSubsystem::Deinitialize()
{
	bStopping = true;
	++SaveGateRevision;
	PendingLoadId.Invalidate();
	ActiveGameSave = nullptr;

	Super::Deinitialize();
}

// -------------------------------------------------------------------- Settings

namespace
{
	// 월드 없이도 찾는다 - GameInstance 는 첫 월드가 생기기 전에도 있다.
	UVBUserSettingsStore* FindSettingsStore(const UVBSaveGameSubsystem& Save)
	{
		const UGameInstance* GameInstance = Save.GetGameInstance();
		return GameInstance ? GameInstance->GetSubsystem<UVBUserSettingsStore>() : nullptr;
	}
}

UVBSaveGame* UVBSaveGameSubsystem::LoadOrCreateSettings()
{
	UVBUserSettingsStore* Store = FindSettingsStore(*this);
	return Store ? Store->LoadOrCreateSettings() : nullptr;
}

const FVBSettingsSaveData& UVBSaveGameSubsystem::GetSettingsData(const UObject* WorldContextObject)
{
	return UVBUserSettingsStore::GetSettingsData(WorldContextObject);
}

void UVBSaveGameSubsystem::UpdateSettings(const FVBSettingsSaveData& NewSettings)
{
	if (UVBUserSettingsStore* Store = FindSettingsStore(*this))
	{
		Store->UpdateSettings(NewSettings);
	}
}

void UVBSaveGameSubsystem::SaveSettings()
{
	if (UVBUserSettingsStore* Store = FindSettingsStore(*this))
	{
		Store->SaveSettings();
	}
}

// -------------------------------------------------------------------- Game Slot

bool UVBSaveGameSubsystem::DoesGameSaveExist(int32 SlotIndex) const
{
	return Disk.Exists(MakeGameSlotName(SlotIndex), GUserIndex);
}

void UVBSaveGameSubsystem::SaveGameSlot(int32 SlotIndex)
{
	if (!CanWriteSlot(SlotIndex))
	{
		return;
	}

	TStrongObjectPtr<ThisClass> KeepAlive(this);
	TGuardValue<bool> Saving(bSavingSnapshot, true);
	FlushSaveToSlot(GetOrCreateActiveGameSave(), SlotIndex);
}

FVBSaveHoldToken UVBSaveGameSubsystem::BeginSaveHold(FName Reason)
{
	// 이미 제출한 기록은 보류로 취소할 수 없다. 장면은 실제 기록 종료 뒤 시작한다.
	if (bStopping || bCompletingSave || !PendingWrites.IsEmpty()) return {};
	const FVBSaveHoldToken Token = SaveHolds.Acquire(Reason);
	if (Token.IsValid())
	{
		NotifySaveHoldsChanged();
	}

	return IsSaveHoldActive(Token) ? Token : FVBSaveHoldToken{};
}

bool UVBSaveGameSubsystem::ConsumeInitialNewGameStart(FGuid AttemptId)
{
	if (bStopping || !AttemptId.IsValid() || GameStartContext.AttemptId != AttemptId
		|| GameStartContext.Intent != EVBGameStartIntent::NewGame || ConsumedNewGameAttempt == AttemptId)
	{
		return false;
	}

	ConsumedNewGameAttempt = AttemptId;
	return true;
}

bool UVBSaveGameSubsystem::EnsureDirectStartContext()
{
	// 유효한 시작 기록 = 번호가 있고 의도가 알려졌다(준비 완료가 요구하는 것과 같다).
	const bool bHasStart = GameStartContext.AttemptId.IsValid() && GameStartContext.Intent != EVBGameStartIntent::Unknown;
	if (bStopping || bHasStart)
	{
		return false;
	}
	// 메뉴의 새 게임과 같은 길(새 세이브, 기본 난이도, 저장 막기 세대)이고, 다른 점은 첫 시네마틱을 이미 쓴 것으로 두는 것 하나다 -
	//   알림이 나가기 전에 표시한다(알림 안에서도 첫 시네마틱 자격이 보이지 않게).
	const FGuid Attempt = BeginNewGame(INDEX_NONE, FGameplayTag(), /*bSkipOpeningCinematic=*/true);
	if (!Attempt.IsValid())
	{
		return false;
	}

	VB_LOG(Log, "게임 시작 기록 없이 월드가 열렸다 - 바로 시작(새 게임, 첫 시네마틱 없음)으로 기록한다");
	return true;
}

bool UVBSaveGameSubsystem::EndSaveHold(const FVBSaveHoldToken& Token)
{
	if (bStopping || !SaveHolds.Release(Token))
	{
		return false;
	}

	NotifySaveHoldsChanged();
	return true;
}

void UVBSaveGameSubsystem::NotifySaveHoldsChanged()
{
	++SaveGateRevision;
	OnSaveHoldsChanged.Broadcast();
}

bool UVBSaveGameSubsystem::CanWriteSlot(int32 SlotIndex)
{
	FName Reason;
	if (bStopping)
	{
		Reason = TEXT("Unavailable");
	}
	else if (SlotIndex < 0 || SlotIndex >= MaxSlotCount)
	{
		Reason = TEXT("InvalidSlot");
	}
	else if (SaveHolds.IsHeld())
	{
		Reason = TEXT("Held");
	}
	else if (bSavingSnapshot || bCompletingSave || bChangingStartContext || PendingWrites.Contains(SlotIndex))
	{
		Reason = TEXT("Busy");
	}

	if (Reason.IsNone())
	{
		return true;
	}

	VB_LOG(Warning, "게임 저장 거절: 슬롯 %d, 이유 %s", SlotIndex, *Reason.ToString());
	// 거절 통지 안의 재요청은 방송을 반복하지 않는다.
	if (!bCompletingSave)
	{
		TGuardValue<bool> Completing(bCompletingSave, true);
		OnGameSaveRefused.Broadcast(SlotIndex, Reason);
	}

	return false;
}

void UVBSaveGameSubsystem::FlushSaveToSlot(UVBSaveGame* Snapshot, int32 SlotIndex)
{
	if (!Snapshot || bStopping)
	{
		return;
	}

	Snapshot->SaveVersion = UVBSaveGame::CurrentSaveVersion;
	Snapshot->SaveTimestamp = FDateTime::UtcNow();
	const FGuid RequestId = FGuid::NewGuid();
	PendingWrites.Add(SlotIndex, RequestId);

	const FString SlotName = MakeGameSlotName(SlotIndex);

	// SlotIndex 를 캡처해 완료 콜백에서 그대로 브로드캐스트. WeakObjectPtr 로 수명 안전.
	FAsyncSaveGameToSlotDelegate Delegate;
	Delegate.BindLambda(
		[WeakThis = TWeakObjectPtr<UVBSaveGameSubsystem>(this), SlotIndex, RequestId]
		(const FString& /*Slot*/, const int32 /*UserIdx*/, bool bSuccess)
		{
			if (UVBSaveGameSubsystem* StrongThis = WeakThis.Get(); StrongThis && !StrongThis->bStopping)
			{
				TStrongObjectPtr<ThisClass> KeepAlive(StrongThis);
				const FGuid* Pending = StrongThis->PendingWrites.Find(SlotIndex);
				if (!Pending || *Pending != RequestId)
				{
					return;
				}

				StrongThis->PendingWrites.Remove(SlotIndex);
				TGuardValue<bool> Completing(StrongThis->bCompletingSave, true);
				StrongThis->OnGameSaveCompleted.Broadcast(SlotIndex, bSuccess);
			}
		});

	Disk.Write(Snapshot, SlotName, GUserIndex, Delegate);
}

void UVBSaveGameSubsystem::LoadGameSlot(int32 SlotIndex)
{
	if (bStopping || bChangingStartContext)
	{
		return;
	}

	const FGuid LoadId = FGuid::NewGuid();
	const FGuid SourceAttempt = GameStartContext.AttemptId;
	PendingLoadId = LoadId;
	const FString SlotName = MakeGameSlotName(SlotIndex);

	// 존재하지 않는 슬롯은 비동기 로드를 걸지 않고 즉시 실패 통지.
	if (!Disk.Exists(SlotName, GUserIndex))
	{
		OnGameLoadCompleted.Broadcast(SlotIndex, false);
		return;
	}

	FAsyncLoadGameFromSlotDelegate Delegate;
	Delegate.BindLambda(
		[WeakThis = TWeakObjectPtr<UVBSaveGameSubsystem>(this), SlotIndex, LoadId, SourceAttempt]
		(const FString& /*Slot*/, const int32 /*UserIdx*/, USaveGame* LoadedData)
		{
			UVBSaveGameSubsystem* StrongThis = WeakThis.Get();
			if (!StrongThis || StrongThis->bStopping)
			{
				return;
			}

			UVBSaveGame* VBSave = Cast<UVBSaveGame>(LoadedData);
			TStrongObjectPtr<ThisClass> KeepAlive(StrongThis);
			if (StrongThis->PendingLoadId != LoadId || StrongThis->GameStartContext.AttemptId != SourceAttempt)
			{
				StrongThis->OnGameLoadCompleted.Broadcast(SlotIndex, false);
				return;
			}

			StrongThis->PendingLoadId.Invalidate();
			if (VBSave)
			{
				TGuardValue<bool> Changing(StrongThis->bChangingStartContext, true);
				VBSave = StrongThis->MigrateIfNeeded(VBSave);
				StrongThis->ActiveGameSave = VBSave;
				// 로드한 슬롯이 곧 활성 슬롯 + Continue 적용 대기. LoadGameSlot(async)의 유일 호출자는 Continue 흐름이다
				// (슬롯 목록 메타는 동기 LoadGameFromSlot 을 쓰므로 이 경로를 타지 않는다). New Game 은 StartNewGame 경유라 여기 안 옴.
				StrongThis->ActiveSlotIndex = SlotIndex;
				StrongThis->bPendingContinueApply = true;
				StrongThis->GameStartContext = {FGuid::NewGuid(), EVBGameStartIntent::Continue};
				StrongThis->SaveHolds.AdvanceGeneration(StrongThis->GameStartContext.AttemptId);
				StrongThis->NotifySaveHoldsChanged();
				StrongThis->OnGameStartContextChanged.Broadcast();
			}
			else
			{
				VB_LOG(Error, "슬롯 %d 의 저장 파일을 읽지 못했다", SlotIndex);
			}

			StrongThis->OnGameLoadCompleted.Broadcast(SlotIndex, VBSave != nullptr);
		});

	Disk.ReadAsync(SlotName, GUserIndex, Delegate);
}

void UVBSaveGameSubsystem::DeleteGameSave(int32 SlotIndex)
{
	FName Reason;
	if (bStopping)
	{
		Reason = TEXT("Unavailable");
	}
	else if (SlotIndex < 0 || SlotIndex >= MaxSlotCount)
	{
		Reason = TEXT("InvalidSlot");
	}
	else if (bSavingSnapshot || bCompletingSave || bChangingStartContext || PendingWrites.Contains(SlotIndex))
	{
		Reason = TEXT("Busy");
	}
	else if (SaveHolds.IsHeld() && ActiveSlotIndex == SlotIndex)
	{
		Reason = TEXT("Held");
	}

	if (!Reason.IsNone())
	{
		VB_LOG(Warning, "게임 저장 삭제 거절: 슬롯 %d, 이유 %s", SlotIndex, *Reason.ToString());
		return;
	}

	Disk.Delete(MakeGameSlotName(SlotIndex), GUserIndex);
}

void UVBSaveGameSubsystem::StartNewGame()
{
	StartNewGameWithDifficulty(FGameplayTag());
}

bool UVBSaveGameSubsystem::StartNewGameWithDifficulty(FGameplayTag Difficulty)
{
	return BeginNewGame(INDEX_NONE, Difficulty).IsValid();
}

FGuid UVBSaveGameSubsystem::BeginNewGame(int32 SlotIndex, FGameplayTag Difficulty, bool bSkipOpeningCinematic)
{
	if (bStopping || bChangingStartContext) return {};
	TGuardValue<bool> Changing(bChangingStartContext, true);
	// 기존 활성 세이브를 버리고 깨끗한 새 세이브로 교체. 기본값은 struct 멤버 초기화로 보장.
	ActiveGameSave = Cast<UVBSaveGame>(UGameplayStatics::CreateSaveGameObject(UVBSaveGame::StaticClass()));
	// 고른 난이도를 시작 신호, 슬롯 쓰기 전에 적는다 - 새 월드의 UVBDifficultyStateComponent 가 InitializeComponent 에서 이 값을 읽는다
	//   (새 게임에는 ReadSave 가 오지 않는다). 정규화해서 적는다 - 저장, 피해, 화면이 같은 태그를 보게.
	ActiveGameSave->Difficulty.Level = VBDifficultyRules::NormalizeLevel(UVBDifficultySettings::LoadConfig(), Difficulty);

	// 슬롯 미확정. 첫 저장 때 정해진다 - 시작만으로 슬롯을 먹지 않는다.
	ActiveSlotIndex = SlotIndex;

	// '적용 대기'는 세우지 않는다 - 새 레벨에서 기본 상태를 유지해야 하므로.
	bPendingContinueApply = false;
	GameStartContext = {FGuid::NewGuid(), EVBGameStartIntent::NewGame};
	if (bSkipOpeningCinematic)
	{
		ConsumedNewGameAttempt = GameStartContext.AttemptId;
	}

	PendingLoadId.Invalidate();
	SaveHolds.AdvanceGeneration(GameStartContext.AttemptId);
	NotifySaveHoldsChanged();
	OnGameStartContextChanged.Broadcast();

	// 디스크에 쓰지 않는다. 새 게임을 시작한 것과 그 진행을 남기겠다는 것은 다른 결정이다.
	return GameStartContext.AttemptId;
}

void UVBSaveGameSubsystem::StartNewGameInSlot(int32 SlotIndex)
{
	StartNewGameInSlotWithDifficulty(SlotIndex, FGameplayTag());
}

bool UVBSaveGameSubsystem::StartNewGameInSlotWithDifficulty(int32 SlotIndex, FGameplayTag Difficulty)
{
	if (SlotIndex < 0 || SlotIndex >= MaxSlotCount)
	{
		return false;
	}

	const FGuid Started = BeginNewGame(SlotIndex, Difficulty);
	if (!Started.IsValid() || bStopping)
	{
		return false;
	}

	// 사용자가 목록에서 직접 고른 슬롯이므로 즉시 점유가 옳다 - 그래야 "Continue" 대상이 바로 생기고
	// 덮어쓰기 의사도 그 자리에서 확정된다.
	SaveGameSlot(SlotIndex);
	return true;
}

void UVBSaveGameSubsystem::TravelToGameplayLevel()
{
	const UVBGameFlowSettings* Settings = GetDefault<UVBGameFlowSettings>();
	if (!Settings || Settings->GameplayLevel.IsNull())
	{
		// 경고 없이 무시하지 않는다 - 증상이 "새 게임 버튼을 눌렀는데 아무 일도 안 남"으로만 나타나기 때문이다.
		VB_LOG(Warning, "GameplayLevel 미지정 - 레벨 전환 불가. Project Settings > Game > VB Game Flow 확인");
		return;
	}

	// WorldContext 로 GameInstance 를 전달한다. 이 서브시스템은 Within=GameInstance 라 소유자가 곧 월드를 관리하는 쪽이고,
	// UGameInstance::GetWorld() 가 기준이다. this 로 전달하면 UObject::GetWorld() 의 Outer 사슬에 의존하게 된다.
	UGameplayStatics::OpenLevelBySoftObjectPtr(GetGameInstance(), Settings->GameplayLevel);
}

bool UVBSaveGameSubsystem::ConsumePendingContinueApply()
{
	const bool bWasPending = bPendingContinueApply;
	bPendingContinueApply = false; // 1회성: 소비 즉시 내려 중복 적용 방지.
	return bWasPending;
}

void UVBSaveGameSubsystem::SaveCurrentGame(APawn* Pawn)
{
	// 슬롯을 고를 화면이 없는 경로. 해석은 ResolveSaveSlot 이 소유하고, 저장 자체는 아래 한 곳뿐이다.
	SaveCurrentGameToSlot(Pawn, ResolveSaveSlot());
}

int32 UVBSaveGameSubsystem::ResolveSaveSlot() const
{
	if (ActiveSlotIndex != INDEX_NONE)
	{
		return ActiveSlotIndex;
	}

	// 빈 슬롯을 먼저 고른다. 어느 것을 덮을지는 사용자가 골라야 하는 문제라
	// 만석이면 0 으로 가되 반드시 경고를 남긴다 - 사용자가 고르는 경로는 슬롯 목록 저장 모드다.
	for (const FVBSaveSlotMeta& Meta : GetAllSlotMetadata())
	{
		if (Meta.IsFree())
		{
			VB_LOG(Log, "ResolveSaveSlot: 활성 슬롯 미확정 -> 빈 슬롯 %d 사용", Meta.SlotIndex);
			return Meta.SlotIndex;
		}
	}

	VB_LOG(Warning, "ResolveSaveSlot: 빈 슬롯이 없어 슬롯 0 을 덮는다 - 슬롯을 고르는 저장 화면을 거쳐야 한다");
	return 0;
}

namespace
{
	// 폰 하나의 저장 문맥. 일반 저장과 체크포인트가 같은 것을 만든다.
	FVBSaveContext MakePawnSaveContext(APawn& Pawn)
	{
		FVBSaveContext Context;
		Context.World = Pawn.GetWorld();
		Context.Pawn = &Pawn;
		Context.PlayerController = Pawn.GetController<APlayerController>();
		Context.PlayerState = Pawn.GetPlayerState();
		return Context;
	}
}

bool UVBSaveGameSubsystem::SaveCurrentGameToSlot(APawn* Pawn, int32 SlotIndex)
{
	if (!Pawn)
	{
		VB_LOG(Warning, "SaveCurrentGameToSlot: Pawn null — 저장 취소");
		return false;
	}

	return WriteContextToSlot(MakePawnSaveContext(*Pawn), SlotIndex);
}

bool UVBSaveGameSubsystem::SaveCheckpoint(APawn* Pawn, const FTransform& RestoreAt)
{
	if (!Pawn)
	{
		VB_LOG(Warning, "SaveCheckpoint: Pawn null — 저장 취소");
		return false;
	}

	FVBSaveContext Context = MakePawnSaveContext(*Pawn);
	Context.RestoreTransformOverride = RestoreAt;
	const int32 Slot = VBSaveSlotPolicy::ResolveCheckpointSlot(ActiveSlotIndex, GetAllSlotMetadata());
	if (Slot == INDEX_NONE)
	{
		// 막을 수 있는 덮어쓰기라 막는다. 슬롯 목록 화면에서 저장해 활성 슬롯이 정해지면 그 뒤 체크포인트는 그 슬롯에 쓴다.
		VB_LOG(Warning, "SaveCheckpoint: 활성 슬롯이 없고 빈 슬롯도 없다 - 기존 세이브를 덮지 않고 체크포인트를 건너뛴다");
		return false;
	}

	VB_LOG(Log, "SaveCheckpoint: 슬롯 %d, 돌아올 자리 %s", Slot, *RestoreAt.GetLocation().ToString());
	return WriteContextToSlot(Context, Slot);
}

int32 VBSaveSlotPolicy::ResolveCheckpointSlot(int32 ActiveSlot, const TArray<FVBSaveSlotMeta>& Slots)
{
	if (ActiveSlot != INDEX_NONE)
	{
		return ActiveSlot;
	}

	for (const FVBSaveSlotMeta& Meta : Slots)
	{
		if (Meta.IsFree())
		{
			return Meta.SlotIndex;
		}
	}

	return INDEX_NONE;
}

bool UVBSaveGameSubsystem::WriteContextToSlot(const FVBSaveContext& Context, int32 SlotIndex)
{
	if (!CanWriteSlot(SlotIndex))
	{
		return false;
	}

	TStrongObjectPtr<ThisClass> KeepAlive(this);
	TGuardValue<bool> Saving(bSavingSnapshot, true);
	const uint64 Revision = SaveGateRevision;
	const FGuid Attempt = GameStartContext.AttemptId;
	TStrongObjectPtr<UWorld> World(Context.World);
	TStrongObjectPtr<APawn> Pawn(Context.Pawn);
	TStrongObjectPtr<APlayerController> Controller(Context.PlayerController);
	TStrongObjectPtr<APlayerState> PlayerState(Context.PlayerState);
	const auto IsCurrent = [&]
	{
		const FName Reason = FindSaveRefusalReason(Revision, Attempt, World.Get(), Pawn.Get(), Controller.Get(), PlayerState.Get());
		if (!Reason.IsNone())
		{
			VB_LOG(Warning, "저장 문맥 거절: 슬롯 %d, 이유 %s", SlotIndex, *Reason.ToString());
		}

		return Reason.IsNone();
	};

	if (!IsCurrent())
	{
		return false;
	}

	// 참여자가 각자 자기 구역을 쓴다(빌드 = AVBPlayerState, 위치 = UVBPlayerRestoreComponent, 문, 스위치 = UVBInteractableSubsystem,
	//   사실 = UVBStoryStateComponent). 하나도 없으면 멈춘다 - 빈 세이브를 저장됐다고 알리면 불러올 때에야 비었다는 것을 안다.
	UVBSaveParticipantSubsystem* Registry = Context.World ? Context.World->GetSubsystem<UVBSaveParticipantSubsystem>() : nullptr;
	const TArray<IVBSaveParticipant*> Participants = Registry ? Registry->GetParticipants() : TArray<IVBSaveParticipant*>();
	if (Participants.Num() == 0)
	{
		VB_LOG(Error, "WriteContextToSlot: 이 월드에 저장 참여자가 없다 - 저장 취소(빈 세이브를 쓰지 않는다)");
		return false;
	}

	FVBCapturedSave Capture;
	if (!CaptureParticipants(Context, *Registry, Participants, IsCurrent, Capture))
	{
		return false;
	}

	VB_LOG(Log, "WriteContextToSlot: 참여자 %d 가 슬롯 %d 에 썼다", Participants.Num(), SlotIndex);
	PublishCapturedSave(Capture, SlotIndex);
	FlushSaveToSlot(Capture.Game.Get(), SlotIndex); // async -> OnGameSaveCompleted
	return true;
}

FName UVBSaveGameSubsystem::FindSaveRefusalReason(uint64 Revision, const FGuid& Attempt, const UWorld* World, const APawn* Pawn,
	const APlayerController* Controller, const APlayerState* PlayerState) const
{
	FName Reason;
	if (bStopping)
	{
		Reason = TEXT("Stopping");
	}
	else if (Revision != SaveGateRevision)
	{
		Reason = TEXT("GateRevisionChanged");
	}
	else if (Attempt != GameStartContext.AttemptId)
	{
		Reason = TEXT("StartAttemptChanged");
	}
	else if (SaveHolds.IsHeld())
	{
		Reason = TEXT("Held");
	}
	else if (!IsValid(World) || World->bIsTearingDown)
	{
		Reason = TEXT("WorldUnavailable");
	}
	else if (World->GetGameInstance() != GetGameInstance() || World->GetNetMode() == NM_Client)
	{
		Reason = TEXT("WorldAuthority");
	}
	else if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || Pawn->GetWorld() != World)
	{
		Reason = TEXT("PawnUnavailable");
	}
	else if (!IsValid(Controller) || Controller->IsActorBeingDestroyed() || Controller->GetWorld() != World)
	{
		Reason = TEXT("ControllerUnavailable");
	}
	else if (Controller->GetPawn() != Pawn || Pawn->GetController() != Controller)
	{
		Reason = TEXT("PossessionChanged");
	}
	else if (!IsValid(PlayerState) || Pawn->GetPlayerState() != PlayerState || Controller->PlayerState != PlayerState)
	{
		Reason = TEXT("PlayerStateChanged");
	}

	return Reason;
}

void UVBSaveGameSubsystem::PublishCapturedSave(const FVBCapturedSave& Capture, int32 SlotIndex)
{
	// 쓴 자리가 곧 활성 슬롯이 된다 - 이후 Continue 와 저장이 같은 자리를 가리켜야 한다.
	ActiveSlotIndex = SlotIndex;
	Capture.Game->SaveVersion = UVBSaveGame::CurrentSaveVersion;
	Capture.Game->SaveTimestamp = FDateTime::UtcNow();
	// 공개 API가 돌려준 객체의 정체성은 유지하고, 검증한 저장 프로퍼티만 한 번 게시한다.
	for (TFieldIterator<FProperty> Property(Capture.Game->GetClass()); Property; ++Property)
	{
		if (Property->HasAnyPropertyFlags(CPF_SaveGame))
		{
			Property->CopyCompleteValue_InContainer(Capture.Active.Get(), Capture.Game.Get());
		}
	}
}

bool UVBSaveGameSubsystem::CaptureParticipants(const FVBSaveContext& Context, UVBSaveParticipantSubsystem& Registry,
	const TArray<IVBSaveParticipant*>& Participants, TFunctionRef<bool()> IsCurrent, FVBCapturedSave& OutCapture)
{
	const UWorld* World = Context.World;
	for (IVBSaveParticipant* Participant : Participants)
	{
		OutCapture.Writers.Emplace(Cast<UObject>(Participant));
	}

	OutCapture.Active.Reset(GetOrCreateActiveGameSave());
	OutCapture.Game.Reset(DuplicateObject(OutCapture.Active.Get(), GetTransientPackage()));
	if (!OutCapture.Game.IsValid() || !IsCurrent())
	{
		return false;
	}

	for (int32 Index = 0; Index < Participants.Num(); ++Index)
	{
		const UObject* Object = OutCapture.Writers[Index].Get();
		const UActorComponent* Component = Cast<UActorComponent>(Object);
		const AActor* Actor = Cast<AActor>(Object);
		if (!IsCurrent() || !IsValid(Object) || Object->GetWorld() != World
			|| (Component && !Component->IsRegistered()) || (Actor && Actor->IsActorBeingDestroyed()))
		{
			return false;
		}

		Participants[Index]->WriteSave(*OutCapture.Game.Get(), Context);
		if (!IsCurrent())
		{
			return false;
		}
	}

	return Registry.GetParticipants() == Participants;
}

int32 UVBSaveGameSubsystem::ApplyActiveSave(const FVBSaveContext& Context)
{
	if (!IsValid(ActiveGameSave))
	{
		VB_LOG(Error, "ApplyActiveSave: 적용할 활성 세이브가 없다");
		return 0;
	}

	const TStrongObjectPtr<UVBSaveGameSubsystem> SubsystemLifetime(this);
	const TStrongObjectPtr<const UVBSaveGame> ReadSave(ActiveGameSave.Get());
	const TStrongObjectPtr<UWorld> World(Context.World);
	const TStrongObjectPtr<APawn> Pawn(Context.Pawn);
	const TStrongObjectPtr<APlayerController> Controller(Context.PlayerController);
	const TStrongObjectPtr<APlayerState> PlayerState(Context.PlayerState);
	const FVBSaveContext ReadContext = Context;
	const FGuid Attempt = GameStartContext.AttemptId;
	const TWeakObjectPtr<UWorld> InstanceWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const auto IsCurrent = [&]
	{
		if (bStopping || !IsValid(this) || ActiveGameSave.Get() != ReadSave.Get() || GameStartContext.AttemptId != Attempt)
		{
			return false;
		}

		if (!IsValid(GetGameInstance()) || GetGameInstance()->GetWorld() != InstanceWorld.Get()
			|| (InstanceWorld.IsValid() && InstanceWorld.Get() != World.Get()))
		{
			return false;
		}

		if (!IsValid(World.Get()) || World->bIsTearingDown || World->GetGameInstance() != GetGameInstance()
			|| World->GetNetMode() == NM_Client)
		{
			return false;
		}

		if (ReadContext.Pawn && (!IsValid(Pawn.Get()) || Pawn->IsActorBeingDestroyed() || Pawn->GetWorld() != World.Get()))
		{
			return false;
		}

		if (ReadContext.PlayerController && (!IsValid(Controller.Get()) || Controller->IsActorBeingDestroyed()
			|| Controller->GetWorld() != World.Get()))
		{
			return false;
		}

		if (ReadContext.PlayerState && (!IsValid(PlayerState.Get()) || PlayerState->IsActorBeingDestroyed()
			|| PlayerState->GetWorld() != World.Get()))
		{
			return false;
		}

		return (!Pawn.Get() || !Controller.Get() || (Controller->GetPawn() == Pawn.Get() && Pawn->GetController() == Controller.Get()))
			&& (!PlayerState.Get() || !Controller.Get() || Controller->PlayerState == PlayerState.Get())
			&& (!PlayerState.Get() || !Pawn.Get() || Pawn->GetPlayerState() == PlayerState.Get());
	};
	if (!IsCurrent())
	{
		return 0;
	}

	UVBSaveParticipantSubsystem* Registry = World->GetSubsystem<UVBSaveParticipantSubsystem>();
	if (!Registry || Registry->GetParticipants().IsEmpty())
	{
		VB_LOG(Error, "ApplyActiveSave: 이 월드에 저장 참여자가 없다 - 세이브가 적용되지 않았다");
		return 0;
	}

	const int32 Delivered = Registry->ReadAll(*ReadSave.Get(), ReadContext, IsCurrent);
	return IsCurrent() ? Delivered : 0;
}

UVBSaveGame* UVBSaveGameSubsystem::GetOrCreateActiveGameSave()
{
	if (!ActiveGameSave)
	{
		ActiveGameSave = Cast<UVBSaveGame>(UGameplayStatics::CreateSaveGameObject(UVBSaveGame::StaticClass()));
	}

	return ActiveGameSave;
}

FString UVBSaveGameSubsystem::MakeGameSlotName(int32 SlotIndex)
{
	return FString::Printf(TEXT("VBGameSave_%d"), SlotIndex);
}

// -------------------------------------------------------------------- Slot Metadata

bool UVBSaveGameSubsystem::GetSlotMetadata(int32 SlotIndex, FVBSaveSlotMeta& OutMeta) const
{
	// 빈 결과로 초기화하되 슬롯 번호는 항상 채워 UI 가 빈 슬롯도 그릴 수 있게 한다.
	OutMeta = FVBSaveSlotMeta();
	OutMeta.SlotIndex = SlotIndex;

	const FString SlotName = MakeGameSlotName(SlotIndex);
	if (!Disk.Exists(SlotName, GUserIndex))
	{
		return false;
	}

	// 목록 표시는 빈도가 낮고 슬롯 수가 적어 동기 로드 허용(메뉴 진입 1회).
	const UVBSaveGame* Save = Cast<UVBSaveGame>(Disk.Read(SlotName, GUserIndex));
	if (!Save)
	{
		// 파일이 있으니 빈 슬롯이 아니다. 비었다고 답하면 자동 저장이 이 파일을 덮는다.
		VB_LOG(Error, "슬롯 %d 의 저장 파일을 읽지 못했다 - 빈 슬롯으로 고르지 않는다", SlotIndex);
		OutMeta.bUnreadable = true;
		return false;
	}

	OutMeta.bExists = true;
	OutMeta.DisplayName = Save->SlotDisplayName;
	OutMeta.Timestamp = Save->SaveTimestamp;
	OutMeta.PlayTimeSeconds = Save->Progress.PlayTimeSeconds;
	OutMeta.LastLevelName = Save->Progress.LastLevelName;
	const UVBProgressCatalogSubsystem* Catalog = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UVBProgressCatalogSubsystem>() : nullptr;
	const UVBChapterDefinition* Chapter = Catalog ? Catalog->ResolveChapter(Save->Progress.Story) : nullptr;
	OutMeta.ChapterTitle = Chapter ? Chapter->Title : FText::GetEmpty();
	return true;
}

TArray<FVBSaveSlotMeta> UVBSaveGameSubsystem::GetAllSlotMetadata() const
{
	TArray<FVBSaveSlotMeta> Result;
	Result.Reserve(MaxSlotCount);

	for (int32 SlotIndex = 0; SlotIndex < MaxSlotCount; ++SlotIndex)
	{
		FVBSaveSlotMeta Meta;
		GetSlotMetadata(SlotIndex, Meta); // 빈 슬롯도 SlotIndex 채워 포함
		Result.Add(Meta);
	}

	return Result;
}

int32 UVBSaveGameSubsystem::GetMostRecentSlotIndex() const
{
	int32 BestSlot = INDEX_NONE;
	FDateTime BestTime = FDateTime::MinValue();

	// 존재하는 슬롯 중 가장 최근 저장 시각을 가진 슬롯을 고른다.
	for (const FVBSaveSlotMeta& Meta : GetAllSlotMetadata())
	{
		if (Meta.bExists && Meta.Timestamp > BestTime)
		{
			BestTime = Meta.Timestamp;
			BestSlot = Meta.SlotIndex;
		}
	}

	return BestSlot;
}

bool UVBSaveGameSubsystem::HasAnySaveGame() const
{
	return GetMostRecentSlotIndex() >= 0;
}

UVBSaveGame* UVBSaveGameSubsystem::MigrateIfNeeded(UVBSaveGame* InSave) const
{
	if (!InSave)
	{
		return nullptr;
	}

	// 확장 지점: 버전이 올라가면 여기서 단계별 변환을 수행한다.
	// 예) if (InSave->SaveVersion < 2) { /* v1 -> v2 필드 보정 */ InSave->SaveVersion = 2; }
	if (InSave->SaveVersion < UVBSaveGame::CurrentSaveVersion)
	{
		// 아직 v1 단일 버전 - 보정할 항목 없음. 버전만 현재로 끌어올린다.
		InSave->SaveVersion = UVBSaveGame::CurrentSaveVersion;
	}

	return InSave;
}
