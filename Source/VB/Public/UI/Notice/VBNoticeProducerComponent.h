// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Progression/VBGrowthRules.h"
#include "VBNoticeProducerComponent.generated.h"

class APawn;
class APlayerState;
class UVBAbilitySystemComponent;
class UVBAbilityFailureMessageData;
class UVBInteractorComponent;
class UVBInventoryComponent;
class UVBItemDefinition;
class UVBNoticeSubsystem;
class UVBProgressionComponent;
class UVBQuestDefinition;
class UVBQuestLogComponent;
class UVBTutorialComponent;
enum class EVBQuestNotice : uint8;
struct FVBNoticeRequest;
struct FVBAbilityFailureReport;

/**
 * 이 플레이어의 게임 사건(저장, 획득, 퀘스트, 성장, 튜토리얼, 상호작용 거절)을 듣고 알림 문구를 알림 줄에 넣는다.
 * 사건을 내는 객체와 로컬 플레이어를 둘 다 아는 PC 에 붙고 로컬 컨트롤러에서만 동작한다. 그리기는 UVBNoticeSubsystem 이 맡는다.
 */
UCLASS(ClassGroup=(UI))
class VB_API UVBNoticeProducerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBNoticeProducerComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	// 능력 실패 문구 표. 필수 - 비면 IsDataValid 가 오류를 낸다(실패 알림이 하나도 안 뜬다).
	UPROPERTY(EditDefaultsOnly, Category="VB|Notice")
	TObjectPtr<UVBAbilityFailureMessageData> AbilityFailureMessages;

	// 문구. 비우면 그 알림을 끈다. 번역이 붙도록 현지화 키는 예전 VBHUD 키 그대로다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Notice")
	FText SaveCompletedText;
	UPROPERTY(EditDefaultsOnly, Category="VB|Notice", meta=(ToolTip="{0}=아이템 이름, {1}=합친 개수"))
	FText ItemAcquiredFormat;
	UPROPERTY(EditDefaultsOnly, Category="VB|Notice", meta=(ToolTip="{0}=퀘스트 제목"))
	FText QuestStartedFormat;
	UPROPERTY(EditDefaultsOnly, Category="VB|Notice", meta=(ToolTip="{0}=퀘스트 제목"))
	FText QuestAdvancedFormat;
	UPROPERTY(EditDefaultsOnly, Category="VB|Notice", meta=(ToolTip="{0}=퀘스트 제목"))
	FText QuestCompletedFormat;
	UPROPERTY(EditDefaultsOnly, Category="VB|Notice", meta=(ToolTip="{0}=새 레벨"))
	FText LevelUpFormat;
	UPROPERTY(EditDefaultsOnly, Category="VB|Notice", meta=(ToolTip="{0}=스킬 칸 이름"))
	FText SkillUnlockedFormat;
	UPROPERTY(EditDefaultsOnly, Category="VB|Notice", meta=(ToolTip="{0}=안내 제목"))
	FText TutorialCompletedFormat;

private:
	friend class FVBNoticeProducerTestAccess;
	friend struct FVBAbilityFailureTestAccess;

	// 소유 PC 의 로컬 플레이어 알림 줄.
	UVBNoticeSubsystem* GetNoticeSink() const;
	void Push(FVBNoticeRequest&& Request) const;

	void HandlePlayerStateSet(APlayerState* PlayerState);
	void HandlePlayerAbilityFailure(const FVBAbilityFailureReport& Report);
	void BindPlayerStateParts(APlayerState* PlayerState);
	void UnbindPlayerStateParts();

	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);
	void BindPawn(APawn* Pawn);
	void UnbindPawn();

	UFUNCTION()
	void HandleGameSaveCompleted(int32 SlotIndex, bool bSuccess);
	void HandleItemAcquired(const UVBItemDefinition* Item, int32 Count);
	void HandleQuestNotice(const UVBQuestDefinition* Quest, EVBQuestNotice Kind);
	void HandleProgressionChanged();
	void HandleTutorialCompletionNotices();
	void HandleInteractionRefused(const FText& RefusedText);

	// 묶은 소스. PlayerState 가 바뀌면 옛 것에서 떼고 새 것에 묶는다.
	TWeakObjectPtr<APlayerState> BoundPlayerState;
	TWeakObjectPtr<UVBAbilitySystemComponent> BoundFailureASC;
	TWeakObjectPtr<UVBInventoryComponent> BoundInventory;
	TWeakObjectPtr<UVBQuestLogComponent> BoundQuestLog;
	TWeakObjectPtr<UVBProgressionComponent> BoundProgression;
	TWeakObjectPtr<UVBTutorialComponent> BoundTutorial;
	TWeakObjectPtr<UVBInteractorComponent> BoundInteractor;

	FDelegateHandle PlayerStateSetHandle;
	FDelegateHandle AbilityFailureHandle;
	bool bWarnedFailureMessages = false;
	FDelegateHandle ItemAcquiredHandle;
	FDelegateHandle QuestNoticeHandle;
	FDelegateHandle ProgressionChangedHandle;
	FDelegateHandle TutorialNoticeHandle;
	FDelegateHandle InteractionRefusedHandle;
	bool bSaveBound = false;

	// 레벨업, 해금 알림의 기준선. PlayerState 를 새로 묶을 때 새로 잡는다.
	VBGrowth::FNoticeBaseline NoticeBaseline;
};
