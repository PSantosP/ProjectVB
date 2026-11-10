// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "SaveSystem/VBSaveHold.h"
#include "SaveSystem/VBSaveDisk.h"
#include "VBSaveGameSubsystem.generated.h"

class UVBSaveGame;
class APawn;
class APlayerController;
class APlayerState;
class UWorld;
class UVBSaveParticipantSubsystem;
class IVBSaveParticipant;
struct FVBSaveContext;

// 체크포인트가 쓸 슬롯: 활성 슬롯 -> 빈 슬롯 -> 없으면 INDEX_NONE(거절).
//   자동 저장이라 만석이어도 남의 세이브(0번)를 덮지 않는다.
namespace VBSaveSlotPolicy
{
	VB_API int32 ResolveCheckpointSlot(int32 ActiveSlot, const TArray<FVBSaveSlotMeta>& Slots);
}

enum class EVBGameStartIntent : uint8 { Unknown, NewGame, Continue };

struct FVBGameStartContext
{
	FGuid AttemptId;
	EVBGameStartIntent Intent = EVBGameStartIntent::Unknown;
};

// 게임 슬롯 저장/로드 완료 통지. (슬롯 인덱스, 성공 여부)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVBOnGameSaveCompleted, int32, SlotIndex, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVBOnGameLoadCompleted, int32, SlotIndex, bool, bSuccess);

/**
 * 게임 세이브/로드 중앙 API. 레벨 전환에도 활성 세이브가 남도록 GameInstance 수명에 둔다. 슬롯은 "VBGameSave_<N>"(비동기 다중).
 * 사용자 설정은 UVBUserSettingsStore 가 맡고, 여기의 설정 함수는 그쪽으로 전달만 한다.
 */
UCLASS()
class VB_API UVBSaveGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 어디서나 세이브 서브시스템에 접근하는 정적 헬퍼. WorldContext 로 GameInstance 를 찾는다.
	UFUNCTION(BlueprintPure, Category="VB|Save", meta=(WorldContext="WorldContextObject"))
	static UVBSaveGameSubsystem* Get(const UObject* WorldContextObject);

	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// 아래 넷은 UVBUserSettingsStore 로 전달만 한다.
	UFUNCTION(BlueprintCallable, Category="VB|Save")
	UVBSaveGame* LoadOrCreateSettings();

	static const FVBSettingsSaveData& GetSettingsData(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category="VB|Save")
	void UpdateSettings(const FVBSettingsSaveData& NewSettings);

	UFUNCTION(BlueprintCallable, Category="VB|Save")
	void SaveSettings();

	// ---------------------------------------------------------------- Game Slot (비동기)

	// 해당 슬롯에 저장 파일이 존재하는지. 메뉴 "Continue" 활성화 판단용.
	UFUNCTION(BlueprintPure, Category="VB|Save")
	bool DoesGameSaveExist(int32 SlotIndex) const;

	// 활성 세이브를 해당 슬롯에 비동기 저장. 완료 시 OnGameSaveCompleted 브로드캐스트.
	UFUNCTION(BlueprintCallable, Category="VB|Save")
	void SaveGameSlot(int32 SlotIndex);

	// 해당 슬롯을 비동기 로드하여 활성 세이브로 설정. 완료 시 OnGameLoadCompleted 브로드캐스트.
	UFUNCTION(BlueprintCallable, Category="VB|Save")
	void LoadGameSlot(int32 SlotIndex);

	// 해당 슬롯 저장 파일 삭제.
	UFUNCTION(BlueprintCallable, Category="VB|Save")
	void DeleteGameSave(int32 SlotIndex);

	// 새 게임 시작(슬롯 미지정). 빈 활성 세이브만 만들고 디스크에 쓰지 않는다.
	//   눌러본 것만으로 슬롯을 먹지 않게, 슬롯은 실제로 저장할 때 정해진다.
	UFUNCTION(BlueprintCallable, Category="VB|Save")
	void StartNewGame();

	// 슬롯을 명시해 새 게임 시작 + 즉시 점유. 사용자가 슬롯을 직접 고른 경우 전용(덮어쓰기 포함).
	UFUNCTION(BlueprintCallable, Category="VB|Save")
	void StartNewGameInSlot(int32 SlotIndex);

	// 위 둘에 난이도 단계를 싣는다(시작 신호, 슬롯 쓰기 전에 적힌다). 모르는 태그는 기본 단계로.
	//   반환 = 시작이 받아들여졌나 - 화면은 true 일 때만 이동한다.
	UFUNCTION(BlueprintCallable, Category="VB|Save")
	bool StartNewGameWithDifficulty(FGameplayTag Difficulty);

	UFUNCTION(BlueprintCallable, Category="VB|Save")
	bool StartNewGameInSlotWithDifficulty(int32 SlotIndex, FGameplayTag Difficulty);

	// 활성 게임 세이브를 반환(없으면 생성). 게임플레이 중 진행도/빌드 갱신 대상.
	UFUNCTION(BlueprintCallable, Category="VB|Save")
	UVBSaveGame* GetOrCreateActiveGameSave();

	// 활성 게임 세이브(없으면 nullptr). 읽기 전용 조회.
	UFUNCTION(BlueprintPure, Category="VB|Save")
	UVBSaveGame* GetActiveGameSave() const { return ActiveGameSave; }

	// 지금 활성 세이브. 월드나 GameInstance 가 없으면 nullptr 이고 로그는 없다(시험과 초기화 경로).
	static UVBSaveGame* FindActiveGameSave(const UObject* WorldContext);

	// ---------------------------------------------------------------- 복원 핸드오프 (레벨 전환 넘어)

	// 게임플레이 레벨(UVBGameFlowSettings::GameplayLevel)로 이동한다.
	//   새 게임, 불러오기가 같은 맵으로 가도록 세이브 흐름의 종착을 여기 한 곳에 둔다.
	UFUNCTION(BlueprintCallable, Category="VB|Save")
	void TravelToGameplayLevel();

	// 마지막으로 로드(Continue)하거나 새로 만든(New Game) 슬롯 인덱스. 저장이 되쓸 대상.
	UFUNCTION(BlueprintPure, Category="VB|Save")
	int32 GetActiveSlotIndex() const { return ActiveSlotIndex; }

	// 다음 레벨에서 활성 세이브를 적용해야 하는가(Continue)를 한 번만 소비한다. New Game 은 세우지 않는다.
	//   OpenLevel 이 월드를 파괴하므로 GI 수명의 이 서브시스템이 들고 있다가 새 GameMode 가 소비한다.
	UFUNCTION(BlueprintCallable, Category="VB|Save")
	bool ConsumePendingContinueApply();

	// 만든 쪽의 명시적 영수증. 옛 bool 을 소비해도 이 값은 바뀌지 않는다.
	FVBGameStartContext GetGameStartContext() const { return GameStartContext; }
	bool ConsumeInitialNewGameStart(FGuid AttemptId);
	// 메뉴 없이 열린 게임 월드(에디터 Play 등)를 새 게임, 첫 시네마틱 없음으로 기록한다. 기록했으면 true.
	//   시작 기록이 없으면 저장 막기, 준비 완료가 영영 생기지 않는다. 이미 있으면 아무것도 안 한다.
	bool EnsureDirectStartContext();
	FSimpleMulticastDelegate OnGameStartContextChanged;

	FVBSaveHoldToken BeginSaveHold(FName Reason);
	bool EndSaveHold(const FVBSaveHoldToken& Token);
	bool IsGameSaveHeld() const { return SaveHolds.IsHeld(); }
	bool IsSaveHoldActive(const FVBSaveHoldToken& Token) const { return !bStopping && SaveHolds.Contains(Token); }
	FSimpleMulticastDelegate OnSaveHoldsChanged;
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGameSaveRefused, int32, FName);
	FOnGameSaveRefused OnGameSaveRefused;

	// 슬롯을 지정하지 않는 저장(활성 -> 빈 슬롯 -> 0). 사용자가 고를 화면이 없는 경로 전용이다.
	UFUNCTION(BlueprintCallable, Category="VB|Save")
	void SaveCurrentGame(APawn* Pawn);

	// 지정한 슬롯에 현재 상태를 저장(비동기, 단일 저장 경로). 내용은 저장 참여자들이 각자 쓴다. 저장 후 활성 슬롯이 된다.
	//   반환 = 저장을 실제로 걸었는지. false 면 OnGameSaveCompleted 가 오지 않는다.
	UFUNCTION(BlueprintCallable, Category="VB|Save")
	bool SaveCurrentGameToSlot(APawn* Pawn, int32 SlotIndex);

	// 체크포인트 저장: 지금 상태를 쓰되 불러올 때 돌아올 자리는 RestoreAt 이다. 완료는 OnGameSaveCompleted 로 온다.
	//   반환 = 저장을 실제로 걸었는지.
	bool SaveCheckpoint(APawn* Pawn, const FTransform& RestoreAt);

	// 활성 세이브를 문맥 월드의 저장 참여자들에게 적용한다. 상한을 알도록 GE_InitStats 뒤(빙의 직후)에 부른다.
	//   반환 = 적용한 참여자 수. 재진입, 교체면 0 이고, 빈 등록부는 오류 로그와 함께 0 이다.
	int32 ApplyActiveSave(const FVBSaveContext& Context);

	// 슬롯 인덱스 -> 슬롯 문자열 이름 ("VBGameSave_<N>").
	UFUNCTION(BlueprintPure, Category="VB|Save")
	static FString MakeGameSlotName(int32 SlotIndex);

	// ---------------------------------------------------------------- Slot Metadata (멀티 슬롯 UI)

	// 메뉴에 노출할 게임 슬롯 수.
	UFUNCTION(BlueprintPure, Category="VB|Save")
	int32 GetMaxSlotCount() const { return MaxSlotCount; }

	// 단일 슬롯 메타 조회. 슬롯을 동기 로드해 헤더 정보를 추출(목록 표시용).
	// 반환값 = 슬롯 존재 여부(빈 슬롯이면 false, 그래도 OutMeta.SlotIndex 는 채워짐).
	UFUNCTION(BlueprintCallable, Category="VB|Save")
	bool GetSlotMetadata(int32 SlotIndex, FVBSaveSlotMeta& OutMeta) const;

	// 0..MaxSlotCount-1 전체 슬롯 메타 배열. 빈 슬롯도 bExists=false 로 포함.
	UFUNCTION(BlueprintCallable, Category="VB|Save")
	TArray<FVBSaveSlotMeta> GetAllSlotMetadata() const;

	// 가장 최근(Timestamp 최대) 저장 슬롯. 없으면 INDEX_NONE. 슬롯을 동기 로드한다.
	//   메뉴의 Continue 와 게임오버의 계속하기가 같은 세이브를 열도록 정책을 여기 한 곳에 둔다.
	UFUNCTION(BlueprintPure, Category="VB|Save")
	int32 GetMostRecentSlotIndex() const;

	// 저장된 게임이 하나라도 있는지 = GetMostRecentSlotIndex() >= 0. Continue 버튼 활성화 판단용.
	UFUNCTION(BlueprintPure, Category="VB|Save")
	bool HasAnySaveGame() const;

	// ---------------------------------------------------------------- Delegates

	UPROPERTY(BlueprintAssignable, Category="VB|Save")
	FVBOnGameSaveCompleted OnGameSaveCompleted;

	UPROPERTY(BlueprintAssignable, Category="VB|Save")
	FVBOnGameLoadCompleted OnGameLoadCompleted;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBSaveGateTestAccess;
#endif
	FVBSaveDisk Disk = FVBSaveDisk::Platform();
	bool CanWriteSlot(int32 SlotIndex);
	void FlushSaveToSlot(UVBSaveGame* Snapshot, int32 SlotIndex);
	// bSkipOpeningCinematic: 시작 알림 전에 첫 시네마틱을 소비 처리해 동기 관찰자가 시네마틱 대상으로 보지 않게 한다.
	FGuid BeginNewGame(int32 SlotIndex, FGameplayTag Difficulty, bool bSkipOpeningCinematic = false);
	void NotifySaveHoldsChanged();
	FVBSaveHoldSet SaveHolds;
	TMap<int32, FGuid> PendingWrites;
	FGuid PendingLoadId;
	FGuid ConsumedNewGameAttempt;
	uint64 SaveGateRevision = 0;
	bool bSavingSnapshot = false;
	bool bCompletingSave = false;
	bool bChangingStartContext = false;
	bool bStopping = false;
	FVBGameStartContext GameStartContext;
	// 저장의 유일한 공통 구현 - 참여자에게 쓰게 하고 비동기로 내보낸다. SaveCurrentGameToSlot, SaveCheckpoint 가 문맥만 다르게 부른다.
	bool WriteContextToSlot(const FVBSaveContext& Context, int32 SlotIndex);
	struct FVBCapturedSave;
	bool CaptureParticipants(const FVBSaveContext& Context, UVBSaveParticipantSubsystem& Registry,
		const TArray<IVBSaveParticipant*>& Participants, TFunctionRef<bool()> IsCurrent, FVBCapturedSave& OutCapture);
	// 저장 시작 때의 문맥이 지금도 맞는지 본다. 어긋났으면 그 이유를, 맞으면 NAME_None 을 돌려준다.
	FName FindSaveRefusalReason(uint64 Revision, const FGuid& Attempt, const UWorld* World, const APawn* Pawn,
		const APlayerController* Controller, const APlayerState* PlayerState) const;
	// 쓴 슬롯을 활성으로 삼고 사본의 저장 프로퍼티를 활성 세이브에 게시한다.
	void PublishCapturedSave(const FVBCapturedSave& Capture, int32 SlotIndex);

	// 메뉴에 노출할 게임 슬롯 수. 확장 시 UDeveloperSettings 로 외부화.
	static constexpr int32 MaxSlotCount = 3;

	// 로드된 세이브의 SaveVersion 이 낮으면 현재 포맷으로 마이그레이션. 확장 지점.
	UVBSaveGame* MigrateIfNeeded(UVBSaveGame* InSave) const;

	// 슬롯 미지정 저장이 쓸 슬롯: 활성 -> 첫 빈 슬롯 -> 0(경고). 무조건 0 이면 남의 세이브를 덮는다.
	int32 ResolveSaveSlot() const;

	// 현재 플레이 중 게임 세이브 캐시.
	UPROPERTY()
	TObjectPtr<UVBSaveGame> ActiveGameSave;

	// 되쓸/적용할 슬롯. LoadGameSlot(Continue) 성공 콜백과 StartNewGame 이 세팅.
	int32 ActiveSlotIndex = INDEX_NONE;

	// Continue 로드가 끝나 "다음 레벨에서 적용 대기" 상태인지. New Game 은 false 유지.
	bool bPendingContinueApply = false;
};
