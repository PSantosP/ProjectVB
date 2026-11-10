#include "Interaction/VBGASPSmartObjectTasks.h"

#include "AIController.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Enemy/VBEnemyBase.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameplayInteractionSmartObjectBehaviorDefinition.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchFeatureVectorHelper.h"
#include "SmartObjectComponent.h"
#include "SmartObjectSubsystem.h"
#include "StateTreeExecutionContext.h"

namespace
{
	AActor* PawnOrActor(AActor* Actor)
	{
		if (const AController* Controller = Cast<AController>(Actor))
		{
			if (APawn* Pawn = Controller->GetPawn())
			{
				return Pawn;
			}
		}

		return Actor;
	}

	UVBGASPSmartObjectComponent* SmartComponent(AActor* Actor)
	{
		Actor = PawnOrActor(Actor);
		return Actor ? Actor->FindComponentByClass<UVBGASPSmartObjectComponent>() : nullptr;
	}
}

UVBGASPSmartObjectTask::UVBGASPSmartObjectTask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 엔진 wrapper 가 이 native virtual 을 부르고 Blueprint 이벤트 디스패치는 쓰지 않는다
	bShouldCallTick = true;
}

AActor* UVBGASPSmartObjectTask::ResolveActor(const FStateTreeExecutionContext& Context) const
{
	if (Actor)
	{
		return PawnOrActor(Actor);
	}

	if (Character)
	{
		return PawnOrActor(Character);
	}

	if (AIController)
	{
		return PawnOrActor(AIController);
	}

	if (AIReference)
	{
		return PawnOrActor(AIReference);
	}

	UObject* Owner = Context.GetOwner();
	if (AActor* OwnerActor = Cast<AActor>(Owner))
	{
		return PawnOrActor(OwnerActor);
	}

	if (const UActorComponent* OwnerComponent = Cast<UActorComponent>(Owner))
	{
		return PawnOrActor(OwnerComponent->GetOwner());
	}

	return nullptr;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	RunStatus = EStateTreeRunStatus::Running;
	AnimationSerial = 0;
	bPathFailed = false;
	RuntimeActor = ResolveActor(Context);
	Component = SmartComponent(RuntimeActor);
	if (!RuntimeActor || !RuntimeActor->HasAuthority())
	{
		return RunStatus = EStateTreeRunStatus::Failed;
	}

	USmartObjectSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<USmartObjectSubsystem>();
	using Op = EVBGASPSmartObjectTaskOperation;
	switch (Operation)
	{
	case Op::FindSmartObject:
		return EnterFindSmartObject();
	case Op::ClaimSlot:
		return EnterClaimSlot(Subsystem);
	case Op::ClearHandle:
		return EnterClearHandle();
	case Op::FindSlotTransforms:
		return EnterFindSlotTransforms(Subsystem);
	case Op::PlayAnimMontage:
		return EnterPlayAnimMontage(Subsystem);
	case Op::PlayAnimFromBestCost:
		return EnterPlayAnimFromBestCost();
	case Op::FocusToTarget:
		if (IsValid(AIController) && IsValid(TargetToFocus))
		{
			AIController->SetFocus(TargetToFocus);
		}

		return RunStatus;
	case Op::ClearFocus:
		if (IsValid(AIController))
		{
			AIController->ClearFocus(EAIFocusPriority::Gameplay);
		}

		return RunStatus;
	case Op::IgnoreCollisions:
		return EnterIgnoreCollisions();
	case Op::AddCooldown:
		if (Component)
		{
			Component->AddCooldown(FName(*CooldownName), CooldownTime);
		}

		return RunStatus = EStateTreeRunStatus::Succeeded;
	case Op::UseSmartObject:
		return EnterUseSmartObject(Subsystem);
	case Op::SetCharacterInputState:
		return EnterSetCharacterInputState();
	case Op::FindRandomLocation:
		return RunStatus = FindRandomLocation();
	}

	return RunStatus = EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::EnterFindSmartObject()
{
	CandidateSlot = {};
	AActor* Object = nullptr;
	const bool bFound = Component && Component->FindSlot(SearchBoxExtents, SmartObjectUserTags, SearchType, CandidateSlot, Object);
	SmartObject = Object;
	return RunStatus = bFound ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::EnterClaimSlot(USmartObjectSubsystem* Subsystem)
{
	ClaimedHandle = {};
	if (!Subsystem || !Component || !SlotToBeClaimed.IsValid())
	{
		return RunStatus = EStateTreeRunStatus::Failed;
	}

	ClaimedHandle = Subsystem->MarkSlotAsClaimed(SlotToBeClaimed, ESmartObjectClaimPriority::Normal,
		FConstStructView::Make(FSmartObjectActorUserData(RuntimeActor)));
	if (!ClaimedHandle.IsValid() || !Component->AdoptClaim(ClaimedHandle))
	{
		if (ClaimedHandle.IsValid())
		{
			Subsystem->MarkSlotAsFree(ClaimedHandle);
		}

		ClaimedHandle.Invalidate();
		return RunStatus = EStateTreeRunStatus::Failed;
	}

	// 원본 claim task 는 자식 state 가 출력을 쓰는 동안 계속 살아 있다
	return RunStatus;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::EnterClearHandle()
{
	const bool bOwned = Component && Component->OwnsClaim(ClaimHandle);
	if (bOwned)
	{
		Component->ReleaseClaim();
	}

	return RunStatus = bOwned ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::EnterFindSlotTransforms(USmartObjectSubsystem* Subsystem)
{
	if (!Subsystem || !Subsystem->GetSlotTransform(ClaimHandle, OutSlotTransform))
	{
		return RunStatus = EStateTreeRunStatus::Failed;
	}

	return RunStatus;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::EnterPlayAnimMontage(USmartObjectSubsystem* Subsystem)
{
	// 원본은 montage 가 없으면 성공으로 끝낸다
	if (!MontageToPlay)
	{
		return RunStatus = EStateTreeRunStatus::Succeeded;
	}

	if (!Component)
	{
		return RunStatus = EStateTreeRunStatus::Failed;
	}

	FVBGASPSmartObjectAnimation Payload;
	Payload.MontageToPlay = MontageToPlay;
	Payload.StartTime = StartTime;
	Payload.PlayTime = PlayTime;
	Payload.RandomPlaytimeVariance = PlayTimeVariance;
	Payload.PlayRate = PlayRate;
	Payload.NumLoops = NumberOfLoops;
	Payload.bUseWarpTarget = Subsystem && Subsystem->GetSlotTransform(SlotHandle, Payload.WarpTargetTransform);
	AnimationSerial = Component->PlayAnimation(Payload);
	return RunStatus = AnimationSerial ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::EnterPlayAnimFromBestCost()
{
	if (!Component || !AnimationProxyTable)
	{
		return RunStatus = EStateTreeRunStatus::Failed;
	}

	if (AAIController* Controller = UAIBlueprintHelperLibrary::GetAIController(RuntimeActor))
	{
		PathFollowing = Controller->GetPathFollowingComponent();
		if (PathFollowing)
		{
			MoveCompletedHandle = PathFollowing->OnRequestFinished.AddUObject(this, &ThisClass::OnMoveCompleted);
		}
	}

	// Setup Task 처럼 EnterState 는 구독만 하고 거리, 비용 판정은 Tick 에서 한다
	return RunStatus;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::EnterIgnoreCollisions()
{
	if (!Component || !OtherActor)
	{
		return RunStatus = EStateTreeRunStatus::Failed;
	}

	Component->SetIgnoreActor(OtherActor, ShouldIgnore);
	return RunStatus = EStateTreeRunStatus::Succeeded;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::EnterUseSmartObject(USmartObjectSubsystem* Subsystem)
{
	if (!Subsystem || !Component || !Subsystem->IsClaimedSmartObjectValid(ClaimHandle))
	{
		return RunStatus = EStateTreeRunStatus::Failed;
	}

	if (!Component->OwnsClaim(ClaimHandle) && !Component->AdoptClaim(ClaimHandle))
	{
		return RunStatus = EStateTreeRunStatus::Failed;
	}

	const auto* Definition = Subsystem->MarkSlotAsOccupied<UGameplayInteractionSmartObjectBehaviorDefinition>(ClaimHandle);
	const USmartObjectComponent* Object = Subsystem->GetSmartObjectComponent(ClaimHandle);
	if (!Definition || !Object)
	{
		Component->ReleaseClaim();
		return RunStatus = EStateTreeRunStatus::Failed;
	}

	Interaction = {};
	Interaction.SetContextActor(RuntimeActor);
	Interaction.SetSmartObjectActor(Object->GetOwner());
	Interaction.SetClaimedHandle(ClaimHandle);
	// slot 무효화는 component 가 맡는다. 여기서 callback 을 또 등록하면 그것을 덮어쓴다
	bInteractionActive = true;
	if (!Interaction.Activate(*Definition))
	{
		EndInteraction(true);
		return RunStatus = EStateTreeRunStatus::Failed;
	}

	return RunStatus;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::EnterSetCharacterInputState()
{
	if (Component)
	{
		Component->CaptureInputState();
	}

	if (UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(RuntimeActor))
	{
		Intent->EnterSmartObjectInputState(WantsToWalk);
	}
	else if (AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(RuntimeActor))
	{
		Enemy->UnCrouch();
		Enemy->ApplyGait(WantsToWalk ? EVBGait::Walk : EVBGait::Run);
	}

	return RunStatus;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::FindRandomLocation()
{
	if (!AIReference || !ExcludedSmartObjectClass || !FMath::IsFinite(SearchRadius) || !FMath::IsFinite(SearchExcludeDistance))
	{
		return EStateTreeRunStatus::Failed;
	}

	TArray<AActor*> ExcludedActors;
	UGameplayStatics::GetAllActorsOfClass(RuntimeActor, ExcludedSmartObjectClass, ExcludedActors);
	// 원본은 pawn 이 아니라 controller 의 위치를 쓴다. AController 가 transform helper 를 숨겨서 AActor 로 본다
	const FVector Origin = static_cast<const AActor*>(AIReference.Get())->GetActorLocation();
	// 원본은 끝없이 다시 시도한다. 불가능한 제외 영역이 서버를 멈추지 않게 횟수를 묶는다
	for (int32 Attempt = 0; Attempt < FMath::Max(1, MaxLocationAttempts); ++Attempt)
	{
		if (!UNavigationSystemV1::K2_GetRandomReachablePointInRadius(RuntimeActor, Origin, RandomLocation, SearchRadius))
		{
			RandomLocation = Origin;
		}

		const bool bTooClose = ExcludedActors.ContainsByPredicate([this](const AActor* Object)
		{
			return IsValid(Object) && FVector::Distance(RandomLocation, Object->GetActorLocation()) < SearchExcludeDistance;
		});
		if (!bTooClose)
		{
			return EStateTreeRunStatus::Running;
		}
	}

	return EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::TickAnimation()
{
	if (!Component || AnimationSerial == 0)
	{
		return EStateTreeRunStatus::Failed;
	}

	bool bSucceeded = false;
	return Component->IsAnimationFinished(AnimationSerial, bSucceeded)
		? (bSucceeded ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed)
		: EStateTreeRunStatus::Running;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::TickBestCost()
{
	if (!Component || !RuntimeActor || bPathFailed)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (AnimationSerial)
	{
		return TickAnimation();
	}

	double Distance = FVector::Distance(RuntimeActor->GetActorLocation(), Destination.GetLocation());
	if (AAIController* Controller = UAIBlueprintHelperLibrary::GetAIController(RuntimeActor))
	{
		if (const UNavigationPath* Path = UAIBlueprintHelperLibrary::GetCurrentPath(Controller))
		{
			Distance = Path->GetPathLength();
		}
	}

	if (Distance > MaximumDistanceThreshold || RuntimeActor->GetVelocity().Size() < MinimumVelocityCheck)
	{
		return EStateTreeRunStatus::Running;
	}

	UAnimMontage* Montage = nullptr;
	float Cost = 0.f, SelectedStartTime = 0.f;
	if (!Component->EvaluateAnimation(AnimationProxyTable, Destination, Montage, Cost, SelectedStartTime))
	{
		return EStateTreeRunStatus::Running;
	}
	// 원본의 dedicated server 분기는 거리가 맞는 첫 유효 pose 를 받는다
	if (RuntimeActor->GetNetMode() != NM_DedicatedServer && Cost > CostThreshold)
	{
		return EStateTreeRunStatus::Running;
	}

	FVBGASPSmartObjectAnimation Payload;
	Payload.MontageToPlay = Montage;
	Payload.StartTime = SelectedStartTime;
	if (const USmartObjectSubsystem* Subsystem = RuntimeActor->GetWorld()->GetSubsystem<USmartObjectSubsystem>())
	{
		Payload.bUseWarpTarget = Subsystem->GetSlotTransform(ClaimHandle, Payload.WarpTargetTransform);
	}

	AnimationSerial = Component->PlayAnimation(Payload);
	return AnimationSerial ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus UVBGASPSmartObjectTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime)
{
	if (RunStatus != EStateTreeRunStatus::Running)
	{
		return RunStatus;
	}

	using Op = EVBGASPSmartObjectTaskOperation;
	if (Operation == Op::PlayAnimMontage)
	{
		return RunStatus = TickAnimation();
	}

	if (Operation == Op::PlayAnimFromBestCost)
	{
		return RunStatus = TickBestCost();
	}

	if (Operation == Op::UseSmartObject)
	{
		USmartObjectSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<USmartObjectSubsystem>();
		if (!bInteractionActive || !Component || !Component->OwnsClaim(ClaimHandle) || !Subsystem
			|| !Subsystem->IsClaimedSmartObjectValid(ClaimHandle))
		{
			EndInteraction(true);
			return RunStatus = EStateTreeRunStatus::Failed;
		}

		if (!Interaction.Tick(DeltaTime))
		{
			RunStatus = Interaction.GetLastRunStatus() == EStateTreeRunStatus::Succeeded
				? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
			EndInteraction(RunStatus != EStateTreeRunStatus::Succeeded);
		}
	}

	return RunStatus;
}

void UVBGASPSmartObjectTask::EndInteraction(bool bAborted)
{
	if (!bInteractionActive)
	{
		return;
	}

	bInteractionActive = false;
	if (bAborted)
	{
		FGameplayInteractionAbortContext Abort;
		Abort.Reason = EGameplayInteractionAbortReason::ExternalAbort;
		Interaction.SetAbortContext(Abort);
	}

	Interaction.Deactivate();
	if (Component && Component->OwnsClaim(ClaimHandle))
	{
		Component->ReleaseClaim();
	}

	Interaction = {};
}

void UVBGASPSmartObjectTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	if (PathFollowing && MoveCompletedHandle.IsValid())
	{
		PathFollowing->OnRequestFinished.Remove(MoveCompletedHandle);
	}

	MoveCompletedHandle.Reset();
	PathFollowing = nullptr;
	EndInteraction(RunStatus != EStateTreeRunStatus::Succeeded);
	if (Component)
	{
		if (AnimationSerial && RunStatus != EStateTreeRunStatus::Succeeded)
		{
			Component->StopAnimation(AnimationSerial);
		}

		if (Operation == EVBGASPSmartObjectTaskOperation::ClaimSlot && Component->OwnsClaim(ClaimedHandle))
		{
			Component->ReleaseClaim();
		}
		// 검색은 입력을 잡은 뒤 slot 을 claim 하기 전에 실패할 수 있다
		if (Operation == EVBGASPSmartObjectTaskOperation::SetCharacterInputState && !Component->IsInteracting())
		{
			Component->ReleaseClaim();
		}
	}

	AnimationSerial = 0;
	Component = nullptr;
	RuntimeActor = nullptr;
}

void UVBGASPSmartObjectTask::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	// 원본은 Success 와 Aborted 를 무시한다. 도착 전에 distance matching 이 먼저 끝날 것으로 본다
	bPathFailed |= Result.Code == EPathFollowingResult::Blocked || Result.Code == EPathFollowingResult::OffPath
		|| Result.Code == EPathFollowingResult::Invalid;
}

void UVBGASPGetAIDataEvaluator::TreeStart(FStateTreeExecutionContext& Context)
{
	AIController = UAIBlueprintHelperLibrary::GetAIController(Actor);
}

bool UVBGASPCheckCooldownCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const UVBGASPSmartObjectComponent* Component = SmartComponent(AIController);
	// 원본처럼 controller 나 cooldown 항목이 없으면 선택을 허락한다
	return !Component || Component->HasCooldownExpired(FName(*CooldownName));
}

void UVBGASPSmartObjectDistanceChannel::BuildQuery(UE::PoseSearch::FSearchContext& SearchContext) const
{
	float Distance = 0.f;
	if (const FChooserEvaluationContext* Chooser = SearchContext.GetContext(SampleRole))
	{
		for (const FStructView& Param : Chooser->Params)
		{
			if (const auto* Input = Param.GetPtr<FVBGASPSmartObjectSelectionInputs>())
			{
				Distance = Input->TargetDistance;
				break;
			}
		}
	}

	UE::PoseSearch::FFeatureVectorHelper::EncodeFloat(SearchContext.EditFeatureVector(), ChannelDataOffset, Distance);
}
