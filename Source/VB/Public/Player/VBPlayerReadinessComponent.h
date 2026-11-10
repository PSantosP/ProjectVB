#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Containers/Ticker.h"
#include "GameplayTagContainer.h"
#include "VBPlayerReadinessComponent.generated.h"

class APawn;
class AController;
class APlayerController;
class UWorld;
class UAbilitySystemComponent;
class UVBPlayerRestoreComponent;
class UVBUIManagerSubsystem;
class UVBSaveParticipantSubsystem;
class UVBSaveGameSubsystem;
enum class EVBGameStartIntent : uint8;

enum class EVBPlayerPreparationState : uint8 { Ready, Blocked, Unavailable };

enum class EVBPlayerPreparationReason : uint32
{
	Initialization = 1 << 0, SaveApplication = 1 << 1, Restore = 1 << 2,
	Interaction = 1 << 3, UI = 1 << 4, ExternalHold = 1 << 5,
	Pawn = 1 << 6, Controller = 1 << 7, Authority = 1 << 8,
	AbilitySystem = 1 << 9, SaveSource = 1 << 10, RestoreSource = 1 << 11,
	UISource = 1 << 12, World = 1 << 13, Dead = 1 << 14, HoldOwner = 1 << 15
};

struct FVBPlayerPreparationSnapshot
{
	EVBPlayerPreparationState State = EVBPlayerPreparationState::Unavailable;
	FGuid InitializationId;
	uint32 Revision = 0;
	uint32 BlockedReasons = 0;
	uint32 UnavailableReasons = 0;
};

struct FVBPlayerPreparationHoldToken
{
	FGuid Id;
	FGuid InitializationId;
};

UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBPlayerReadinessComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBPlayerReadinessComponent();
	static FVBPlayerPreparationSnapshot QueryForPawn(const APawn* Pawn);
	FVBPlayerPreparationSnapshot Query() const;
	static EVBPlayerPreparationState EvaluateReasons(uint32 BlockedReasons, uint32 UnavailableReasons);
	// 구독한 뒤 현재 사실을 다시 물어본다 - 이 알림은 Ready 명령이 아니다
	FSimpleMulticastDelegate OnPreparationChanged;

	void BeginInitialization(FGuid Id, EVBGameStartIntent Intent);
	void CompleteInitialization(FGuid Id, bool bContinueDispatched);
	FVBPlayerPreparationHoldToken AcquirePreparationHold(UObject* Owner, FName Reason);
	bool CanFinishPreparationHold(const FVBPlayerPreparationHoldToken& Token) const;
	void ReleasePreparationHold(const FVBPlayerPreparationHoldToken& Token);

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBPlayerReadinessTestAccess;
#endif
	// 폰, 월드, 컨트롤러, 권한의 이유를 더한다.
	void AddActorReasons(const APawn* Pawn, const UWorld* World, const APlayerController* PC, FVBPlayerPreparationSnapshot& Result) const;
	// 시작 초기화 단계의 이유를 더한다.
	void AddInitializationReasons(FVBPlayerPreparationSnapshot& Result) const;
	// 저장 서브시스템과 저장 참여자 등록부의 이유를 더한다.
	void AddSaveReasons(const APawn* Pawn, const UWorld* World, FVBPlayerPreparationSnapshot& Result) const;
	// 복원 컴포넌트와 복원 영수증의 이유를 더한다.
	void AddRestoreReasons(const APawn* Pawn, FVBPlayerPreparationSnapshot& Result) const;
	// ASC 연결, 상호작용, 사망의 이유를 더한다.
	static void AddAbilitySystemReasons(const APawn* Pawn, FVBPlayerPreparationSnapshot& Result);
	// UI 입력 가능 여부의 이유를 더한다.
	static void AddUIReasons(const APlayerController* PC, FVBPlayerPreparationSnapshot& Result);
	// 외부 보류의 이유를 더한다.
	void AddHoldReasons(FVBPlayerPreparationSnapshot& Result) const;

	UFUNCTION()
	void HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);
	void RefreshBindings();
	void DisconnectProviders();
	// ASC 태그 구독은 폰 확장의 연결 통지를 따른다 - 빙의 순간(컨트롤러 변경 통지)에는 ASC 가 아직 이어지지 않아 놓친다.
	void HandleAbilitySystemInitialized(class UVBAbilitySystemComponent& ASC);
	void HandleAbilitySystemUninitialized(class UVBAbilitySystemComponent& ASC, bool bAvatarSession);
	void ReleaseAbilitySystem();
	TWeakObjectPtr<class UVBPawnExtensionComponent> PawnExtension;
	FDelegateHandle AbilitySystemInitializedHandle;
	FDelegateHandle AbilitySystemUninitializedHandle;
	void NotifyChanged();
	void HandleTagChanged(FGameplayTag Tag, int32 Count);
	bool NotifyAfterTagChange(float DeltaSeconds);
	FTSTicker::FDelegateHandle TagNotificationHandle;
	uint32 ProviderEpoch = 0;
	bool bTagNotificationPending = false;
	struct FPreparationHold { TWeakObjectPtr<UObject> Owner; FName Reason; };
	TMap<FGuid, FPreparationHold> Holds;
	FGuid InitializationId;
	EVBGameStartIntent StartIntent{};
	bool bInitializationEnded = false;
	bool bContinueDispatched = false;
	bool bEnding = false;
	uint32 Revision = 0;
	TWeakObjectPtr<APawn> BoundPawn;
	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
	TWeakObjectPtr<UVBPlayerRestoreComponent> Restore;
	TWeakObjectPtr<UVBUIManagerSubsystem> UI;
	TWeakObjectPtr<UVBSaveParticipantSubsystem> Registry;
	TWeakObjectPtr<UVBSaveGameSubsystem> Save;
	FDelegateHandle InteractionHandle;
	FDelegateHandle DeadHandle;
	FDelegateHandle RestoreHandle;
	FDelegateHandle UIHandle;
	FDelegateHandle SaveAppliedHandle;
	FDelegateHandle StartContextHandle;
};
