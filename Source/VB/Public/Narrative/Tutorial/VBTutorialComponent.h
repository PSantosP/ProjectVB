// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Narrative/Story/VBStoryStateBinding.h"
#include "Narrative/Tutorial/VBTutorialTypes.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "VBTutorialComponent.generated.h"

class APlayerState;
class ACharacter;
class APawn;
class UVBTutorialDefinition;
class UVBStoryStateComponent;
class UVBQuestLogComponent;
class UVBCinematicDirectorSubsystem;
class UVBSaveParticipantSubsystem;
struct FStreamableHandle;
struct FVBCinematicEndResult;
struct FVBRuleContext;

/**
 * 튜토리얼 개인 진행을 관리한다. AVBPlayerState 서브오브젝트, 쓰기는 서버만, 복제는 소유 클라이언트에만.
 * 진행은 서버 사건과 조건 재평가 신호로만 한다(폴링 없음). UI 는 모르고 OnTutorialChanged, 완료 알림 큐만 낸다.
 * 저장 읽기, 첫 복제는 완료 알림을 내지 않는다.
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBTutorialComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	UVBTutorialComponent();
	static UVBTutorialComponent* FindFor(const AActor* Actor);
	bool Start(const UVBTutorialDefinition* Definition);
	void RefreshConditions();
	bool IsReady() const { return bCatalogReady && bProgressReady; }
	bool IsCatalogReady() const { return bCatalogReady; }
	bool HasCatalogFailed() const { return bCatalogFailed; }
	bool RetryCatalog();
	const TArray<FVBTutorialProgress>& GetProgress() const { return Progress; }
	const TArray<TObjectPtr<UVBTutorialDefinition>>& GetDefinitions() const { return Definitions; }
	const UVBTutorialDefinition* ResolveDefinition(FGameplayTag TutorialId) const;
	const FVBTutorialProgress* FindProgress(FGameplayTag TutorialId) const;
	const UVBTutorialDefinition* GetHintDefinition() const;
	TArray<FText> TakeCompletionNotices();
	FSimpleMulticastDelegate OnTutorialChanged;
	FSimpleMulticastDelegate OnCompletionNoticesChanged;

	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	uint32 InvalidateCatalogLoad();
	void LoadCatalog();
	void FinishCatalogLoad(uint32 ExpectedGeneration);
	void ResolveSavedProgress();
	void BindSources();
	void UnbindSources();
	void RefreshMovementSource();
	void UnbindMovementSource();
	UFUNCTION() void HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn);
	UFUNCTION() void HandleMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
	void HandleGameEvent(const FVBGameEvent& Event);
	void HandleSaveApplied();
	void HandleCinematicEnded(const FVBCinematicEndResult& Result, bool bSucceeded);
	void Evaluate(const FVBGameEvent* Event);
	// 입력 하나와 평가 중 쌓인 입력을 모두 처리한다. 진행이 바뀌었으면 true.
	bool DrainEvaluations(const FVBGameEvent* Event, TArray<FText>& OutCompleted);
	// 정의 하나를 시작하거나 진행시킨다. 진행이 바뀌었으면 true. Event 가 null 이면 조건만 다시 본다.
	bool EvaluateDefinition(const UVBTutorialDefinition& Definition, const FVBGameEvent* Event,
		const FVBRuleContext& Context, TArray<FText>& OutCompleted);
	bool CanEvaluate() const;
	APlayerState* GetPlayerState() const;
	bool HasWriteAuthority() const;
	UFUNCTION() void OnRep_Progress();
	UFUNCTION(Client, Reliable) void ClientCompletionNotice(const FText& Title);

	UPROPERTY(ReplicatedUsing=OnRep_Progress)
	TArray<FVBTutorialProgress> Progress;
	UPROPERTY(ReplicatedUsing=OnRep_Progress)
	bool bProgressReady = false;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UVBTutorialDefinition>> Definitions;
	TMap<FGameplayTag, int32> DefinitionIndices;
	TArray<FVBSavedTutorial> UnresolvedSavedProgress;
	TArray<FText> PendingCompletionNotices;
	TArray<FSoftObjectPath> CatalogPaths;
	TSharedPtr<FStreamableHandle> CatalogLoadHandle;
	FVBStoryStateBinding StoryBinding;
	TWeakObjectPtr<ACharacter> MovementCharacter;
	TWeakObjectPtr<UVBQuestLogComponent> QuestLog;
	TWeakObjectPtr<UVBCinematicDirectorSubsystem> Cinematic;
	TWeakObjectPtr<UVBSaveParticipantSubsystem> SaveParticipants;
	FDelegateHandle EventHandle, QuestHandle, CinematicHandle, SaveAppliedHandle;
	uint32 CatalogGeneration = 0;
	bool bCatalogReady = false;
	bool bCatalogFailed = false;
	bool bProcessing = false;
	bool bRefreshPending = false;
	bool bSuppressNextEvaluationNotices = false;
	TArray<FVBGameEvent> PendingEvents;
};
