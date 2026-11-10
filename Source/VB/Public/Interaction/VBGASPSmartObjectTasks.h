#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "GameplayInteractionContext.h"
#include "Interaction/VBGASPSmartObjectTypes.h"
#include "PoseSearch/PoseSearchFeatureChannel_Distance.h"
#include "VBGASPSmartObjectTasks.generated.h"

class AActor;
class AAIController;
class UProxyTable;
class UPathFollowingComponent;
class USmartObjectSubsystem;
class UVBGASPSmartObjectComponent;
struct FAIRequestID;
struct FPathFollowingResult;

UENUM()
enum class EVBGASPSmartObjectTaskOperation : uint8
{
	FindSmartObject,
	ClaimSlot,
	ClearHandle,
	FindSlotTransforms,
	PlayAnimMontage,
	PlayAnimFromBestCost,
	FocusToTarget,
	ClearFocus,
	IgnoreCollisions,
	AddCooldown,
	UseSmartObject,
	SetCharacterInputState,
	FindRandomLocation
};

/** 샘플 StateTree 의 기존 노드 ID, 바인딩 뒤에 있는 네이티브 구현. */
UCLASS(meta=(DisplayName="GASP Smart Object Task"))
class VB_API UVBGASPSmartObjectTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()
public:
	UVBGASPSmartObjectTask(const FObjectInitializer& ObjectInitializer);
	UPROPERTY(EditAnywhere, Category="Parameter")
	EVBGASPSmartObjectTaskOperation Operation = EVBGASPSmartObjectTaskOperation::FindSmartObject;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AActor> Actor;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AActor> Character;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AAIController> AIController;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AActor> SmartObjectActor;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FSmartObjectSlotHandle SlotToBeClaimed;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FSmartObjectClaimHandle ClaimHandle;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FSmartObjectClaimHandle SlotHandle;
	UPROPERTY(EditAnywhere, Category="Output") FSmartObjectClaimHandle ClaimedHandle;
	UPROPERTY(EditAnywhere, Category="Output") FSmartObjectSlotHandle CandidateSlot;
	UPROPERTY(EditAnywhere, Category="Output") TObjectPtr<AActor> SmartObject;
	UPROPERTY(EditAnywhere, Category="Output") FTransform OutSlotTransform;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FVector SearchBoxExtents = FVector(2000.0);
	// 원본 SmartObjectSearchType: closest / first / random / farthest.
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) uint8 SearchType = 0;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FGameplayTagContainer SmartObjectUserTags;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<UAnimMontage> MontageToPlay;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) float StartTime = 0.f;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) double PlayTime = 0.0;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) double PlayTimeVariance = 0.0;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) float PlayRate = 1.f;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) int32 NumberOfLoops = 0;
	// 원본 자산 매핑용으로만 남긴다 - 샘플 몽타주 태스크는 이 플래그를 읽지 않는다.
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) bool IgnoreCollision = false;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<UProxyTable> AnimationProxyTable;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) double CostThreshold = 0.0;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FTransform Destination;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) double MaximumDistanceThreshold = 300.0;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) double MinimumVelocityCheck = 100.0;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AActor> TargetToFocus;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AActor> OtherActor;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) bool ShouldIgnore = false;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FString CooldownName;
	// STT_AddCooldown 에서 연결되지 않는다 - 원본은 CooldownName 을 map 키로 쓴다.
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FGameplayTag CooldownTag;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) double CooldownTime = 0.0;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) bool WantsToWalk = false;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AAIController> AIReference;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) float SearchRadius = 0.f;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) float SearchExcludeDistance = 0.f;
	UPROPERTY(EditAnywhere, Category="Output") FVector RandomLocation = FVector::ZeroVector;
	// 원본 LoctionAwayFromSmartObject 는 BP_SmartObject_Base 를 박아 두었다 - 그 클래스를 저작에서 넣는다.
	UPROPERTY(EditAnywhere, Category="Parameter") TSubclassOf<AActor> ExcludedSmartObjectClass;
	UPROPERTY(EditAnywhere, Category="Parameter", meta=(ClampMin="1")) int32 MaxLocationAttempts = 32;

protected:
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;

private:
	AActor* ResolveActor(const FStateTreeExecutionContext& Context) const;
	EStateTreeRunStatus TickBestCost();
	EStateTreeRunStatus TickAnimation();
	EStateTreeRunStatus FindRandomLocation();
	// 주변에서 쓸 수 있는 슬롯을 찾아 출력에 적는다.
	EStateTreeRunStatus EnterFindSmartObject();
	// 슬롯을 점유하고 component 에 전달한다. 실패하면 점유를 되돌린다.
	EStateTreeRunStatus EnterClaimSlot(USmartObjectSubsystem* Subsystem);
	// component 가 가진 점유를 해제한다.
	EStateTreeRunStatus EnterClearHandle();
	// 점유한 슬롯의 위치를 출력에 적는다.
	EStateTreeRunStatus EnterFindSlotTransforms(USmartObjectSubsystem* Subsystem);
	// component 에 몽타주 재생을 맡긴다.
	EStateTreeRunStatus EnterPlayAnimMontage(USmartObjectSubsystem* Subsystem);
	// 이동 완료만 구독한다. 비용 판정은 Tick 이 한다.
	EStateTreeRunStatus EnterPlayAnimFromBestCost();
	// 상대 액터와의 이동 충돌 무시를 켜거나 끈다.
	EStateTreeRunStatus EnterIgnoreCollisions();
	// 슬롯을 사용 중으로 바꾸고 Gameplay Interaction 을 시작한다.
	EStateTreeRunStatus EnterUseSmartObject(USmartObjectSubsystem* Subsystem);
	// 입력 상태를 기억하고 걷기, 뛰기 상태로 바꾼다.
	EStateTreeRunStatus EnterSetCharacterInputState();
	void EndInteraction(bool bAborted);
	void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result);
	UPROPERTY(Transient) TObjectPtr<AActor> RuntimeActor;
	UPROPERTY(Transient) TObjectPtr<UVBGASPSmartObjectComponent> Component;
	UPROPERTY(Transient) TObjectPtr<UPathFollowingComponent> PathFollowing;
	UPROPERTY(Transient) FGameplayInteractionContext Interaction;
	FDelegateHandle MoveCompletedHandle;
	uint32 AnimationSerial = 0;
	bool bInteractionActive = false;
	bool bPathFailed = false;
};

UCLASS(meta=(DisplayName="GASP Get AI Data"))
class VB_API UVBGASPGetAIDataEvaluator : public UStateTreeEvaluatorBlueprintBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AActor> Actor;
	UPROPERTY(EditAnywhere, Category="Output") TObjectPtr<AAIController> AIController;
protected:
	virtual void TreeStart(FStateTreeExecutionContext& Context) override;
};

UCLASS(meta=(DisplayName="GASP Check Cooldown"))
class VB_API UVBGASPCheckCooldownCondition : public UStateTreeConditionBlueprintBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) TObjectPtr<AAIController> AIController;
	UPROPERTY(EditAnywhere, Category="Input", meta=(Optional)) FString CooldownName;
protected:
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

/** 네이티브 Chooser 입력을 쓴다. 인덱싱과 가중치는 엔진 Distance 채널이 맡는다. */
UCLASS(EditInlineNew, meta=(DisplayName="GASP Distance To Smart Object"))
class VB_API UVBGASPSmartObjectDistanceChannel : public UPoseSearchFeatureChannel_Distance
{
	GENERATED_BODY()
public:
	virtual void BuildQuery(UE::PoseSearch::FSearchContext& SearchContext) const override;
};
