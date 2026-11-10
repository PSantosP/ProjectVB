// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "Animation/VBAnimInstance.h"
#include "Character/VBCharacter.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "CharacterCommon/VBBodyControlPolicy.h"
#include "Traversal/VBTraversalComponent.h" // 매달림 상태 미러
#include "Data/VBMovementConfig.h"
#include "Data/VBWeaponDefinition.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Animation/VBMotionWarpingNames.h"
#include "MotionWarpingComponent.h"
#include "Character/VBTargetLockComponent.h"
#include "Animation/VBInteractionIK.h"
#include "Animation/VBEnvironmentInteractionIK.h"
#include "Character/VBFinisherComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PoseSearch/MotionMatchingAnimNodeLibrary.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchResult.h"
#include "ChooserFunctionLibrary.h"
#include "VB/VB.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystemGlobals.h"
#include "Animation/VBGASPLocomotionUpdateLibrary.h"
#include "Animation/VBGASPLocomotionLibrary.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "Animation/VBAnimLocomotionRules.h"
#include "Animation/VBAnimMotionMatchingRules.h"
#include "VBAnimLinkedGraph.h"
#include "Chooser.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNodeReference.h"
#include "BoneControllers/AnimNode_OffsetRootBone.h"
#include "UObject/UnrealType.h"

// Master CHT 의 Dense(0)/Sparse(1) 분기 입력. GASP `a.AnimNode.MotionMatching.LOD` 대응.
static TAutoConsoleVariable<int32> CVarMMDatabaseLOD(
	TEXT("vb.AnimNode.MotionMatching.LOD"),
	0,
	TEXT("MMDatabase LOD index used by Master CHT (0=Dense, 1=Sparse)."),
	ECVF_Default
);

void UVBAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	GASPLocomotionInstance = nullptr;
	LinkedInteractionTransform = nullptr;
	GASPSlideInstance = nullptr;
	bUseGuardLocomotionPose = false;
	GuardHandoff.Reset();
	bUseGASPSlidePose = false;
	bUseOriginalGASPUnarmed = !UsesWeaponLocomotion();
	SlideHandoff.ClearEntered();

	// 착지는 소유자의 엔진 델리게이트로만 받는다. JustLanded 를 밖에서 쓰지 않고, 재초기화 때 한 번 다시 바인딩한다.
	ACharacter* Owner = Cast<ACharacter>(TryGetPawnOwner());
	LocomotionIntent = UVBLocomotionIntentComponent::FindFor(Owner);
	WeaponState = UVBWeaponStateComponent::FindFor(Owner);
	TargetLock = UVBTargetLockComponent::FindFor(Owner);
	OwnerTraversal = UVBTraversalComponent::FindFor(Owner);
	if (Owner != LandedSource.Get())
	{
		UnbindOwnerLanded();
		if (Owner)
		{
			Owner->LandedDelegate.AddUniqueDynamic(this, &UVBAnimInstance::HandleOwnerLanded);
			LandedSource = Owner;
		}
	}

	// TrajectoryGenerationData 와 ActivePoseSearchDatabases 는 ABP Class Defaults 가 관리하므로 여기서 덮어쓰지 않는다.
}

void UVBAnimInstance::NativeUninitializeAnimation()
{
	UnbindOwnerLanded();
	LocomotionIntent.Reset();
	WeaponState.Reset();
	TargetLock.Reset();
	OwnerTraversal.Reset();
	Super::NativeUninitializeAnimation();
}

void UVBAnimInstance::UnbindOwnerLanded()
{
	if (ACharacter* Owner = LandedSource.Get())
	{
		Owner->LandedDelegate.RemoveDynamic(this, &UVBAnimInstance::HandleOwnerLanded);
	}

	LandedSource.Reset();
}

// GASP OnLanded 대응 - JustLanded 를 JustLandedGraceDuration 동안 참으로 두고(재시작 가능) NativePostEvaluateAnimation 이 끈다.
void UVBAnimInstance::HandleOwnerLanded(const FHitResult& Hit)
{
	const ACharacter* Owner = LandedSource.Get();
	if (!Owner)
	{
		return;
	}

	JustLanded = true;
	LandVelocity = Owner->GetVelocity();
	if (const UWorld* World = GetWorld())
	{
		JustLandedClearTime = World->GetTimeSeconds() + JustLandedGraceDuration;
	}
}

void UVBAnimInstance::RequestGASPEarlyTransition_Implementation(bool bToLoop)
{
	VBAnimLinkedGraph::RaiseEarlyTransition(*this, bToLoop);
}

bool UVBAnimInstance::ShouldSuppressAttackLocomotion(const ACharacter* Character)
{
	const UCharacterMovementComponent* CMC = Character ? Character->GetCharacterMovement() : nullptr;
	if (!CMC || !CMC->IsMovingOnGround() || CMC->GetCurrentAcceleration().Size2D() > KINDA_SMALL_NUMBER
		|| !UVBAbilitySystemStatics::IsAttacking(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Character)))
	{
		return false;
	}

	const UAnimInstance* Anim = Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		return false;
	}
	// Stop 은 BlendOut 을 알리기 전에 활성 루트모션 참조를 지운다.
	// 가중치가 남은 몽타주는 인스턴스가 끝날 때까지 복귀 포즈로 남는다.
	for (const FAnimMontageInstance* Instance : Anim->MontageInstances)
		if (Instance && Instance->Montage && Instance->Montage->HasRootMotion()
			&& (Instance->IsActive() || Instance->GetWeight() > ZERO_ANIMWEIGHT_THRESH))
		{
			return true;
		}
	return false;
}

bool UVBAnimInstance::CanUseGuardLocomotionPose() const
{
	FVBAnimGuardHandoff::FPoseGate Gate;
	Gate.bEnableGuardTurnPose = bEnableGuardTurnPose;
	Gate.bUseGASPLocomotion = bUseGASPLocomotion;
	Gate.bPoseOwnedByMontage = bPoseOwnedByMontage;
	Gate.bRagdollPowered = RagdollState.bPowered;
	Gate.bUseSlidePose = bUseGASPSlidePose;
	Gate.MoveThreshold = MoveThreshold;
	Gate.WeaponState = WeaponState.Get();
	return FVBAnimGuardHandoff::CanUseGuardPose(*this, Gate);
}

UChooserTable* UVBAnimInstance::GetRequestedGuardLocomotionChooser() const
{
	if (!CanUseGuardLocomotionPose())
	{
		return nullptr;
	}

	return FVBAnimGuardHandoff::FindGuardChooser(WeaponState.Get());
}

void UVBAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	// 프레임 순서가 계약이다. 각 단계의 주석이 무엇의 앞뒤여야 하는지 적는다.
	GatherRagdollAndLinkedGraphs();

	APawn* Pawn = TryGetPawnOwner();
	ACharacter* Character = Cast<ACharacter>(Pawn);
	UCharacterMovementComponent* CMC = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Pawn || !Character || !CMC)
	{
		return;
	}

	// 슬라이드 단계보다 앞 - 슬라이드가 이번 프레임의 bPoseOwnedByMontage 를 다시 쓴다.
	UpdatePoseOverrideState(Pawn);
	AVBCharacter* VBChar = Cast<AVBCharacter>(Character);
	UpdateSlideHandoff(DeltaSeconds, VBChar);
	UpdateGuardHandoff();
	UpdateInteractionHandIK(DeltaSeconds);
	ShiftLastFrameValues();
	DeriveMovementValues(DeltaSeconds, Character, CMC);
	if (VBChar)
	{
		UpdateWeaponPoolAndPins(DeltaSeconds, VBChar, CMC);
		UpdateLockOnFlag();
	}

	PublishLocomotionStates();
}

// Pawn 검사보다 앞에서 돈다 - Pawn 이 없어도 링크 그래프 캐시를 갱신한다.
void UVBAnimInstance::GatherRagdollAndLinkedGraphs()
{
	const APawn* RagdollPawn = TryGetPawnOwner();
	const UVBRagdollComponent* Ragdoll = RagdollPawn ? RagdollPawn->FindComponentByClass<UVBRagdollComponent>() : nullptr;
	RagdollState = Ragdoll ? Ragdoll->GetAnimationState() : FVBRagdollAnimationState();
	if (bUseGASPLocomotion)
	{
		UAnimInstance* Linked = GetLinkedAnimGraphInstanceByTag(UsesWeaponLocomotion() ? TEXT("GASPLocomotion") : TEXT("GASPUnarmed"));
		if (Linked != GASPLocomotionInstance)
		{
			GASPLocomotionInstance = Linked;
			if (Linked && UVBGASPLocomotionLibrary::ConfigureFootPlacementPelvis(
				Linked, FootPlacementPelvisMaxOffset, FootPlacementPelvisStiffness) == 0)
			{
				VB_LOG(Warning, "[FootPlacement] Linked locomotion pelvis settings were not applied");
			}

			LinkedInteractionTransform = VBAnimLinkedGraph::FindInteractionTransformField(Linked);
		}
		// 트래버설이 넣어 준 트랜스폼을 CharacterProperties 처럼 게임 스레드 스냅샷으로 그래프에 전달한다.
		if (Linked && LinkedInteractionTransform)
		{
			LinkedInteractionTransform->CopyCompleteValue(LinkedInteractionTransform->ContainerPtrToValuePtr<void>(Linked),
				&InteractionTransform);
		}
	}
}

// 슬라이드 그래프를 잡고 슬라이드 포즈 사용 여부(bUseGASPSlidePose)를 정한다.
void UVBAnimInstance::UpdateSlideHandoff(float DeltaSeconds, const AVBCharacter* VBCharacter)
{
	UAnimInstance* SlideGraph = bUseGASPLocomotion ? GetLinkedAnimGraphInstanceByTag(TEXT("GASPSlide")) : nullptr;
	GASPSlideInstance = SlideGraph;

	FVBAnimSlideHandoff::FInputs In;
	In.DeltaSeconds = DeltaSeconds;
	In.Character = VBCharacter;
	In.SlideGraph = SlideGraph;
	In.LocomotionGraph = GASPLocomotionInstance;
	In.Intent = LocomotionIntent.Get();
	In.bUseGASPLocomotion = bUseGASPLocomotion;
	In.bPoseOwnedByMontage = bPoseOwnedByMontage;
	In.bTraversalOwningPose = IsTraversalOwningPose();
	In.bRagdollPowered = RagdollState.bPowered;
	bUseGASPSlidePose = SlideHandoff.Update(In, bUseGASPSlidePose);
}

// 가드 chooser 선택 준비 + 기본 블렌드 완료 -> bUseGuardLocomotionPose. 이어서 GASP 루트 오프셋 해제 여부를 정한다.
void UVBAnimInstance::UpdateGuardHandoff()
{
	const UChooserTable* GuardChooser = GetRequestedGuardLocomotionChooser();
	bUseGuardLocomotionPose = GuardHandoff.Update(GuardChooser, GASPLocomotionInstance);
	bReleaseGASPRootOffset = IsTraversalOwningPose()
		|| (IsAnyMontagePlaying() && !bUseGuardLocomotionPose) || RagdollState.bPowered;
}

// 게임 스레드 전용 - 손 IK 가 몽타주 상태를 읽는다.
void UVBAnimInstance::UpdateInteractionHandIK(float DeltaSeconds)
{
	// 상호작용 중이 아니면 꺼진 상태로 돌아오므로 매 프레임 불러도 된다.
	UVBInteractionIKLibrary::Update(this, GetInteractionResult(), DeltaSeconds, InteractionIK);
	VBEnvironmentInteractionIK::Update(this, InteractionIK);
}

// 프레임 시작에 돈다 - 뒤 단계가 이번 프레임에 이 값들을 덮어쓴다.
void UVBAnimInstance::ShiftLastFrameValues()
{
	// 프레임 시작에 옮겨야 한다. 끝에서 옮기면 GetMMInterruptMode 가 항상 같은 값을 비교해 interrupt 를 못 한다.
	MMState_LastFrame = MMState;
	MovementMode_LastFrame = MovementMode;
	RotationMode_LastFrame = RotationMode;
	Stance_LastFrame = Stance;
	Gait_LastFrame = Gait;
	MMWeaponType_LastFrame = MMWeaponType; // 무기 스왑(MM 풀 교체) 감지용
	bFirePoolInterrupt = false; // 1프레임 펄스
	PreviousLocomotionAngle = LocomotionAngle;
}

// 속도, 지면 거리, 궤적, 이동 상태와 Gait, 로코모션 각도, 재생 속도, 앉기 플래그.
void UVBAnimInstance::DeriveMovementValues(float DeltaSeconds, ACharacter* Character, UCharacterMovementComponent* CMC)
{
	// GASP UpdateEssentialValues 이식. ABP 함수들이 읽는 변수를 먼저 계산한다.
	bSuppressAttackLocomotion = ShouldSuppressAttackLocomotion(Character);
	UpdateEssentialValues(DeltaSeconds, Character, CMC);

	// ForwardSpeed / RightSpeed: Velocity 를 로컬 방향으로 분해.
	const FVector CurrentVel = Velocity;
	const FVector ForwardVector = Character->GetActorForwardVector();
	const FVector RightVector = Character->GetActorRightVector();

	ForwardSpeed = FVector::DotProduct(CurrentVel, ForwardVector);
	RightSpeed = FVector::DotProduct(CurrentVel, RightVector);

	// GroundSpeed: XY 평면 속도 크기
	GroundSpeed = CurrentVel.Size2D();

	// IsFalling: 공중 상태
	IsFalling = CMC->IsFalling();

	// 캡슐이 실제로 카메라를 따라 도는가. RotationMode enum 은 비무장에서 일부러 다르므로 CMC 에서 읽는다.
	bCMCStrafeRotation = CMC->bUseControllerDesiredRotation;

	UpdateDistanceToGround(Character);

	// ShouldMove: 실제 이동 중인지
	ShouldMove = GroundSpeed > MoveThreshold;

	// Size2D를 사용하는 이유는 Z축을 제외한 이동 입력만 확인하기 위해서
	const bool bHasMovementInput = CMC->GetCurrentAcceleration().Size2D() > KINDA_SMALL_NUMBER;
	const UVBLocomotionIntentComponent* Intent = LocomotionIntent.Get();
	const bool bSprintActive = Intent && Intent->GetIsSprinting();
	const EVBGait CharacterGait = Intent ? Intent->GetGait() : EVBGait::Run;

	// GASP 궤적 예측 + WorldCollisions + FutureVelocity -> Trajectory/FutureVelocity/TrajectoryCollision.
	UpdateTrajectory(DeltaSeconds, Character);

	// IsMoving 판정 -> MMState/Gait/LastMovingGait (Sprint 홀드 = Run 강제 Run_Loops 풀 라우팅).
	UpdateMovementStateAndGait(bHasMovementInput, bSprintActive, CharacterGait);

	// Velocity 방향 -> LocomotionAngle 보간 (OW 입력).
	UpdateLocomotionAngle(DeltaSeconds, Character);

	// 속도/Gait -> WantedPlayRate (SpeedRemappingCurve).
	UpdatePlayRate();

	// bIsCrouching: ACharacter 내장 변수
	bIsCrouching = Character->bIsCrouched;
}

// 낙하 중이면 LandPredictionHeight 만큼 아래로 라인 트레이스(빗나가면 그 높이), 지상이면 0.
void UVBAnimInstance::UpdateDistanceToGround(const ACharacter* Character)
{
	if (!IsFalling)
	{
		DistanceToGround = 0.0f;
		return;
	}

	FHitResult Hit;
	const FVector Start = Character->GetActorLocation();
	const FVector End = Start - FVector(0.0f, 0.0f, LandPredictionHeight);
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Character);
	DistanceToGround = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params)
		? Hit.Distance : LandPredictionHeight;
}

// 플레이어 전용 - 질주 풀 자격, 모션매칭 풀과 인터럽트 래치, 그래프 핀 인덱스, 전투 플래그.
void UVBAnimInstance::UpdateWeaponPoolAndPins(float DeltaSeconds, AVBCharacter* VBChar, UCharacterMovementComponent* CMC)
{
	bIsSprinting = LocomotionIntent.IsValid() && LocomotionIntent->GetIsSprinting();
	const UVBWeaponStateComponent* WSC = WeaponState.Get();

	// Sprint 풀 합류 자격. 홀드 즉시 켜지고, 해제 후엔 SprintPoolJoinSpeed 아래로 떨어질 때까지 유지.
	const EVBWeaponType SprintPool = WSC ? WSC->GetMMPool() : EVBWeaponType::None;
	const UVBMovementConfig* MovementCfg = VBChar->GetMovementConfig();
	const float JoinSpeed = MovementCfg->ResolveProfile(
		SprintPool, SprintPool != EVBWeaponType::None).SprintPoolJoinSpeed;
	bSprintPoolEligible = bIsSprinting || (bSprintPoolEligible && Speed2D >= JoinSpeed);
	MMSprintGateSpeed = bSprintPoolEligible ? Speed2D : 0.0f; // Dense CHT Sprint 행 전용 컬럼 입력

	if (WSC)
	{
		// 파쿠르 임시수납 시에는 Unarmed로 표시. WSC 가 유일한 작성자라 이 함수 안에서 다시 안 바뀐다.
		LocomotionState = WSC->GetEffectiveLocomotionState();
		CurrentWeaponType = WSC->GetCurrentWeaponType();
		WeaponTypeIndex = PoseSlotLookup.ResolveWeaponPin(WeaponPoseSlots, CurrentWeaponType);

		// 전투 MM 풀은 무기 정의(ArmedMMPool/SheathedMMPool)가 정한다. 전용 풀이 없으면 None(기본 풀).
		MMWeaponType = WSC->GetMMPoolForState(LocomotionState);

		// 풀 교체 interrupt - 수렴 래치(무기 스왑) + 발도<->납도 즉시 래치. -> bFirePoolInterrupt.
		EvaluatePoolInterrupt(DeltaSeconds, CMC, LocomotionState);

		// WantedPlayRate 는 트래버설 몽타주 재생 속도도 읽으므로 여기서 덮어쓰지 않는다.

		UpdateGraphPinIndices();
		// IsInCombat = 상태 2, 3(무기 소환 여부와 무관).
		bIsCombatMode = WSC->IsInCombat();
	}
}

// ABP 의 Blend Poses by int 핀 - 로코모션 갈래, 앉기 포즈, 그 갈래가 상태 머신인지.
//   LocomotionState, CurrentWeaponType, MMWeaponType(8단계), bIsCrouching(7단계)을 읽는다.
void UVBAnimInstance::UpdateGraphPinIndices()
{
	LocomotionStateIndex = FVBAnimPoseSlotLookup::ResolveLocomotionPin(LocomotionState, bIsCrouching, MMWeaponType);
	// crouch BS 포즈: 발도 중에만 무기별 BS, 수납 상태는 0(공용 BS).
	CrouchPoseIndex = (LocomotionState == EVBLocomotionState::ArmedCombat)
		? PoseSlotLookup.ResolveCrouchPin(CrouchPoseSlots, CurrentWeaponType)
		: 0;
	bUsesStateMachinePath = FVBAnimPoseSlotLookup::IsStateMachinePin(LocomotionStateIndex);
}

// 플레이어 전용 - PublishLocomotionStates 의 RotationMode 가 읽는 락온 플래그.
void UVBAnimInstance::UpdateLockOnFlag()
{
	bIsLockedOn = TargetLock.IsValid() && TargetLock->IsLockedOn();
}

// 마지막 단계 - 앞 단계가 이번 프레임에 쓴 값으로 그래프가 읽는 enum 상태 셋을 낸다.
void UVBAnimInstance::PublishLocomotionStates()
{
	// GASP UpdateStates 대응(MovementMode / Stance / RotationMode). RotationMode 가 bIsSprinting, LocomotionState
	//   (UpdateWeaponPoolAndPins)와 bIsLockedOn(UpdateLockOnFlag)을 읽으므로 그 뒤에 돈다.
	bUseOriginalGASPUnarmed = !UsesWeaponLocomotion();
	UpdateStates();
}


void UVBAnimInstance::UpdateTrajectory(float DeltaSeconds, ACharacter* Character)
{
	// GASP GenerateTrajectory 이식. 미래 궤적을 예측해 Trajectory 에 저장한다.
	const FPoseSearchTrajectoryData& TrajData =
		(GroundSpeed > 0.0f) ? TrajectoryGenerationData_Moving : TrajectoryGenerationData_Idle;

	FTransformTrajectory TempTrajectory = Trajectory;
	// 샘플링 값은 GASP 원본 그대로(미래 1.5초). 바꾸면 MM 로코모션 느낌이 전부 바뀐다.
	UVBGASPLocomotionUpdateLibrary::GenerateLocomotionTrajectory(this, TrajData, DeltaSeconds,
		bSuppressAttackLocomotion, TempTrajectory, DesiredControllerYawLastUpdate, Trajectory);

	// GASP 그대로. FloorCollisionsOffset 이 0 이면 trace 가 빗나가 FutureVelocity.Z 가 누적된다.
	{
		FTransformTrajectory CollidedTrajectory;
		TArray<AActor*> ActorsToIgnore;
		if (Character)
		{
			ActorsToIgnore.Add(Character);
		}

		UPoseSearchTrajectoryLibrary::HandleTransformTrajectoryWorldCollisions(
			this, this,
			/*In*/ Trajectory,
			/*bApplyGravity*/ true,
			/*FloorCollisionsOffset*/ 0.01f,
			/*Out*/ CollidedTrajectory,
			/*Out*/ TrajectoryCollision,
			/*TraceChannel*/ ETraceTypeQuery::TraceTypeQuery1,
			/*bTraceComplex*/ false,
			ActorsToIgnore,
			/*DrawDebugType*/ EDrawDebugTrace::None,
			/*bIgnoreSelf*/ true,
			/*MaxObstacleHeight*/ 150.0f);
		Trajectory = CollidedTrajectory;
	}

	// FutureVelocity = 0.4~0.5초 샘플 차이(GASP 그대로).
	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(
		Trajectory, /*Time1*/ 0.4f, /*Time2*/ 0.5f, /*Out*/ FutureVelocity, /*bExtrapolate*/ false);
}

void UVBAnimInstance::UpdateLocomotionAngle(float DeltaSeconds, const ACharacter* Character)
{
	FVBLocomotionAngleInputs In;
	In.CurrentAngle = LocomotionAngle;
	In.bShouldMove = ShouldMove;
	In.GroundSpeed = GroundSpeed;
	In.MinSpeed = LocomotionAngleMinSpeed;
	In.Velocity = Velocity;
	In.ActorRotation = Character->GetActorRotation();
	In.InterpSpeed = LocomotionAngleInterpSpeed;
	In.DeltaSeconds = DeltaSeconds;
	LocomotionAngle = VBAnimLocomotionRules::ComputeLocomotionAngle(In);
}

void UVBAnimInstance::UpdatePlayRate()
{
	FVBPlayRateInputs In;
	In.bShouldMove = ShouldMove;
	In.GroundSpeed = GroundSpeed;
	In.MoveThreshold = MoveThreshold;
	In.Gait = Gait;
	In.SpeedCurve = SpeedToPlayRateCurve.GetRichCurveConst();
	In.MinPlayRate = MinPlayRate;
	In.MaxPlayRate = MaxPlayRate;
	In.WalkRootMotionSpeed = WalkAnimRootMotionSpeed;
	In.RunRootMotionSpeed = RunAnimRootMotionSpeed;
	WantedPlayRate = VBAnimLocomotionRules::ComputeWantedPlayRate(In);
}

void UVBAnimInstance::UpdateMovementStateAndGait(bool bHasMovementInput, bool bSprintActive, EVBGait CharacterGait)
{
	FVBMovementStateInputs In;
	In.Velocity = Velocity;
	In.FutureVelocity = FutureVelocity;
	In.bHasMovementInput = bHasMovementInput;
	In.bSprintActive = bSprintActive;
	In.CharacterGait = CharacterGait;
	In.LastMovingGait = LastMovingGait;
	const FVBMovementStateResult Result = VBAnimLocomotionRules::StepMovementStateAndGait(In);
	MMState = Result.MMState;
	Gait = Result.Gait;
	LastMovingGait = Result.LastMovingGait;
}

// 풀 교체 판정 입력을 모아 래치를 돌린다. 발화하면 bFirePoolInterrupt(1프레임 펄스)를 켠다.
void UVBAnimInstance::EvaluatePoolInterrupt(float DeltaSeconds, const UCharacterMovementComponent* CMC,
	EVBLocomotionState InLocomotionState)
{
	FVBAnimPoolInterruptLatch::FInputs In;
	In.DeltaSeconds = DeltaSeconds;
	In.Movement = CMC;
	In.Speed2D = Speed2D;
	In.Pool = MMWeaponType;
	In.PoolLastFrame = MMWeaponType_LastFrame;
	In.LocomotionState = InLocomotionState;
	In.SpeedSettleTolerance = PoolInterruptSpeedSettleTolerance;
	In.NearStopSpeed = PoolInterruptNearStopSpeed;
	In.TimeoutSeconds = PoolInterruptTimeoutSeconds;
	if (PoolInterruptLatch.Step(In))
	{
		bFirePoolInterrupt = true;
	}
}

void UVBAnimInstance::NativePostEvaluateAnimation()
{
	Super::NativePostEvaluateAnimation();
	if (bUseGASPLocomotion && GASPLocomotionInstance)
	{
		VBAnimLinkedGraph::ReadSelectedDatabase(*GASPLocomotionInstance, CurrentSelectedDatabase, CurrentDatabaseTags);
	}

	// JustLanded 는 유예 시간이 지나면 끈다. 이번 프레임 평가가 끝난 뒤라 같은 프레임 경합이 없다.
	if (JustLanded)
	{
		const UWorld* World = GetWorld();
		if (World && World->GetTimeSeconds() >= JustLandedClearTime)
		{
			JustLanded = false;
		}
	}
}

void UVBAnimInstance::UpdateEssentialValues(float DeltaSeconds, ACharacter* Character, UCharacterMovementComponent* CMC)
{
	// GASP UpdateEssentialValues 이식. 속도/가속 파생값을 매 틱 먼저 계산한다.

	// Master CHT 의 Dense/Sparse 분기 입력.
	MMDatabaseLOD = CVarMMDatabaseLOD.GetValueOnAnyThread();

	// AimOffset / ShouldTurnInPlace 등이 읽는 Transform 캐시.
	CharacterTransform = Character->GetActorTransform();
	if (USkeletalMeshComponent* Mesh = Character->GetMesh())
	{
		// root 본 world transform. mesh 가 -Y forward 라 RootTransformYawCorrection 으로 yaw 를 보정한다.
		static const FName RootBoneName(TEXT("root"));
		const int32 BoneIdx = Mesh->GetBoneIndex(RootBoneName);
		const FTransform RawRoot = (BoneIdx != INDEX_NONE)
			? Mesh->GetSocketTransform(RootBoneName, RTS_World)
			: Mesh->GetComponentTransform();

		RootTransform = VBAnimLocomotionRules::ApplyRootYawCorrection(RawRoot, RootTransformYawCorrection);
	}

	const FVector NewVelocity = bSuppressAttackLocomotion ? FVector::ZeroVector : Character->GetVelocity();
	Velocity = NewVelocity;

	// 프레임간 속도 변화(실제 가속). CMC 가속은 입력 가속이라 별개다.
	if (DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		VelocityAcceleration = (NewVelocity - Velocity_LastFrame) / DeltaSeconds;
	}
	else
	{
		VelocityAcceleration = FVector::ZeroVector;
	}

	Speed2D = NewVelocity.Size2D();
	HasVelocity = !NewVelocity.IsNearlyZero(1.0f); // 1cm/s 미만은 정지로 간주
	if (HasVelocity)
	{
		LastNonZeroVelocity = NewVelocity;
	}

	Acceleration = CMC->GetCurrentAcceleration();
	AccelerationAmount = Acceleration.Size();
	HasAcceleration = !Acceleration.IsNearlyZero();

	Velocity_LastFrame = NewVelocity;

	// 다음 틱 ShouldTurnInPlace 판정용 컨트롤러 yaw.
	if (const AController* Controller = Character->GetController())
	{
		PreviousDesiredControllerYaw = Controller->GetControlRotation().Yaw;
	}
}

void UVBAnimInstance::UpdateStates()
{
	// _LastFrame 은 프레임 시작(ShiftLastFrameValues)에서 옮긴다. 여기서 다시 옮기지 않는다.

	MovementMode = IsFalling ? EVBMovementMode::InAir : EVBMovementMode::Grounded;

	Stance = bIsCrouching ? EVBStance::Crouch : EVBStance::Stand;

	// WantsToStrafe 는 일부러 안 본다. 비무장은 OrientToMovement 여야 MM 이 Run_Turn 을 고른다.
	const bool bArmedCombat = (LocomotionState == EVBLocomotionState::ArmedCombat);
	RotationMode = VBAnimLocomotionRules::ResolveRotationMode(bIsSprinting, bIsLockedOn, bArmedCombat);
}

// 저작된 포즈(사망, 처형, 트래버설)가 로코모션 보정 노드(OffsetRootBone/LegIK)를 이겨야 할 때를 ABP 로 전달한다.
void UVBAnimInstance::UpdatePoseOverrideState(const APawn* OwnerPawn)
{
	const bool bDeadNow = UVBAbilitySystemStatics::IsActorDead(OwnerPawn);

	bIsDead = bDeadNow;

	// 매달림 미러. 값은 트래버설 컴포넌트가 관리한다.
	const UVBTraversalComponent* Traversal = OwnerTraversal.Get();
	bIsHanging = Traversal && Traversal->IsHanging();
	HangMoveAxis = bIsHanging ? Traversal->GetHangMoveAxis() : 0.0f;

	bIsClimbing = Traversal && Traversal->IsClimbing();
	bIsOnLadder = Traversal && Traversal->IsOnLadder();
	const FVector2D ClimbAxis = bIsClimbing ? Traversal->GetClimbMoveAxis() : FVector2D::ZeroVector;
	ClimbMoveAxisX = static_cast<float>(ClimbAxis.X);
	ClimbMoveAxisY = static_cast<float>(ClimbAxis.Y);

	// 매달림/등반의 뼈 공간 보정. 사다리는 현재 포즈를 읽는 전용 노드가 담당한다.
	const FVBHangIKTargets IK = bIsHanging ? Traversal->GetHangIKTargets()
		: (bIsClimbing ? Traversal->GetClimbIKTargets() : FVBHangIKTargets());
	HangHandTargetL = IK.HandL;
	HangHandTargetR = IK.HandR;
	HangFootTargetL = IK.FootL;
	HangFootTargetR = IK.FootR;
	HangHandIKWeightL = IK.HandWeightL;
	HangHandIKWeightR = IK.HandWeightR;
	HangFootIKWeightL = IK.FootLWeight;
	HangFootIKWeightR = IK.FootRWeight;

	const bool bTraversalPose = IsTraversalOwningPose();
	// 사망, 트래버설 중엔 Release(오프셋 즉시 해제). 평소 값은 노드 기본값과 같아야 한다.
	const bool bReleaseOffsetRoot = bDeadNow || bTraversalPose;
	OffsetRootTranslationMode = bReleaseOffsetRoot ? EOffsetRootBoneMode::Release : EOffsetRootBoneMode::Interpolate;
	OffsetRootRotationMode = bReleaseOffsetRoot ? EOffsetRootBoneMode::Release : EOffsetRootBoneMode::Accumulate;

	// 사망, 처형, 트래버설 중엔 LegIK 를 끈다. 저작된 연출 포즈에 지면 적응 IK 를 걸면 다리가 엉뚱하게 끌려간다.
	bInFinisherScene = UVBFinisherComponent::IsActorInScene(OwnerPawn);
	LegIKAlpha = (bDeadNow || bInFinisherScene || bTraversalPose) ? 0.0f : 1.0f;

	// 저작된 포즈가 지금 이 메시 제어를 점유하고 있는지를 한 값으로 낸다. 새 소유자는 FMontagePoseFacts 에 필드를 추가한다.
	VBBodyControlPolicy::FMontagePoseFacts PoseFacts;
	PoseFacts.bDead = bDeadNow;
	PoseFacts.bFinisherScene = bInFinisherScene;
	PoseFacts.bTraversalPose = bTraversalPose;
	bPoseOwnedByMontage = VBBodyControlPolicy::IsPoseOwnedByMontage(PoseFacts);
}

// ========== GASP 핀 바인딩 헬퍼 ==========
// OffsetRootBone 속성은 runtime setter 가 없어 아래 함수들을 AnimGraph 핀에 바인딩해 제어한다.

namespace
{
	// GASP 기본 슬롯 이름 캐시.
	static const FName GASP_DefaultSlotName(TEXT("DefaultSlot"));
}

bool UVBAnimInstance::IsMoving() const
{
	// 지금도 움직이고 예측 궤적상 앞으로도 움직이는가.
	return HasVelocity && !FutureVelocity.IsNearlyZero(1.0f);
}

EOffsetRootBoneMode UVBAnimInstance::GetOffsetRootTranslationMode() const
{
	// 트래버설이 캡슐을 직접 옮기는 동안은 오프셋을 놓는다(노드가 그 이동을 오차로 읽는다).
	// 몽타주 중에도 Release. weight 가 아니라 IsSlotActive 를 써야 블렌드아웃 순간의 캡슐 스냅을 흡수한다.
	if (IsTraversalOwningPose())
	{
		return EOffsetRootBoneMode::Release;
	}

	if (IsSlotActive(GASP_DefaultSlotName))
	{
		return EOffsetRootBoneMode::Release;
	}

	switch (MovementMode)
	{
	case EVBMovementMode::Grounded:
		// 이동 중 Interpolate(점진 복귀), 정지 시 Release.
		return IsMoving() ? EOffsetRootBoneMode::Interpolate : EOffsetRootBoneMode::Release;
	case EVBMovementMode::InAir:
		// 공중에서는 Release(낙하 중 root 이탈 방지).
		return EOffsetRootBoneMode::Release;
	default:
		return EOffsetRootBoneMode::Release;
	}
}

float UVBAnimInstance::GetLegIKAlpha() const
{
	// 값은 UpdatePoseOverrideState 가 정한다. 회전 중 다리가 튀면 IK 를 끄지 말고 클립의 IK 뼈를 먼저 재라.
	return LegIKAlpha;
}

EOffsetRootBoneMode UVBAnimInstance::GetOffsetRootRotationMode() const
{
	// 트래버설, SM 경로, 몽타주 중엔 Release, 그 외 Accumulate(시각 회전은 Steering 이 맡는다).
	// SM 경로는 캐릭터 회전을 스테이트머신 포즈가 담당해서, 여기서 역상쇄하면 이중으로 돈다.
	if (IsTraversalOwningPose())
	{
		return EOffsetRootBoneMode::Release;
	}

	if (bUsesStateMachinePath)
	{
		return EOffsetRootBoneMode::Release;
	}

	return IsSlotActive(GASP_DefaultSlotName)
		? EOffsetRootBoneMode::Release
		: EOffsetRootBoneMode::Accumulate;
}

float UVBAnimInstance::GetOffsetRootTranslationHalfLife() const
{
	switch (MMState)
	{
	case EVBMMState::Idle:
		return OffsetRootTranslationHalfLife_Idle;
	case EVBMMState::Moving:
		return OffsetRootTranslationHalfLife_Moving;
	default:
		return OffsetRootTranslationHalfLife_Default;
	}
}

// ========== GASP MotionMatching 상태 판정 Helpers ==========

bool UVBAnimInstance::IsStarting() const
{
	return VBAnimMotionMatchingRules::IsStarting(IsMoving(), CurrentDatabaseTags, Velocity, FutureVelocity, IsStartingVelocityBias);
}

bool UVBAnimInstance::IsPivoting() const
{
	return VBAnimMotionMatchingRules::IsPivoting(Velocity, FutureVelocity, RotationMode, IsPivotingStrafeThreshold,
		IsPivotingFreeThreshold);
}

bool UVBAnimInstance::ShouldTurnInPlace() const
{
	// RotationMode enum 이 아니라 CMC 상태로 본다(비무장 enum 은 일부러 strafe 가 아니다).
	return VBAnimMotionMatchingRules::ShouldTurnInPlace(RootTransform, CharacterTransform, ShouldTurnInPlaceYawDelta, bCMCStrafeRotation,
		MMState, MMState_LastFrame);
}

bool UVBAnimInstance::ShouldSpinTransition() const
{
	return VBAnimMotionMatchingRules::ShouldSpinTransition(RootTransform, CharacterTransform, ShouldSpinTransitionYawDelta, Speed2D,
		ShouldSpinTransitionMinSpeed, CurrentDatabaseTags);
}

bool UVBAnimInstance::EnableSteering() const
{
	// GASP 그대로. 이동 중이거나 공중일 때 Steering 을 켠다(제자리 회전 지속은 클립 비용 바이어스가 맡는다).
	return MMState == EVBMMState::Moving || MovementMode == EVBMovementMode::InAir;
}

FVector2D UVBAnimInstance::GetAOValue() const
{
	// 조준 회전과 루트 회전의 차이(Yaw, Pitch). GASP 그대로.
	const FRotator RootRot = RootTransform.Rotator();
	FRotator AimRot = FRotator::ZeroRotator;
	if (const APawn* Pawn = Cast<APawn>(GetOwningActor()))
	{
		AimRot = Pawn->GetBaseAimRotation();
	}

	const FRotator Delta = (AimRot - RootRot).GetNormalized();
	return FVector2D(Delta.Yaw, Delta.Pitch);
}

bool UVBAnimInstance::EnableAO() const
{
	// strafe 판정은 enum 이 아니라 CMC 상태로 본다(비무장 enum 은 일부러 Free 라 헤드룩이 꺼진다).
	if (!bCMCStrafeRotation)
	{
		return false;
	}

	const float AbsYaw = FMath::Abs(GetAOValue().X);
	if (AbsYaw > EnableAOYawThreshold)
	{
		return false;
	}

	if (GetSlotMontageLocalWeight(GASP_DefaultSlotName) >= EnableAOSlotWeightThreshold)
	{
		return false;
	}

	return true;
}

float UVBAnimInstance::GetAOValueX() const
{
	return GetAOValue().X;
}

float UVBAnimInstance::GetAOValueY() const
{
	return GetAOValue().Y;
}

float UVBAnimInstance::GetMMBlendTime() const
{
	FVBMMBlendTimeInputs In;
	In.MovementMode = MovementMode;
	In.MovementModeLastFrame = MovementMode_LastFrame;
	In.Velocity = Velocity;
	In.DefaultBlendTime = MMDefaultBlendTime;
	In.FastLandBlendTime = MMFastLandBlendTime;
	In.VeryShortJumpBlendTime = MMVeryShortJumpBlendTime;
	In.InAirDefaultBlendTime = MMInAirDefaultBlendTime;
	In.JumpSpeedThreshold = MMJumpSpeedThreshold;
	return VBAnimMotionMatchingRules::ResolveBlendTime(In);
}

TArray<UPoseSearchDatabase*> UVBAnimInstance::GetAllActivePoseSearchDBs() const
{
	// Master CHT 를 평가해 활성 PSD 목록을 낸다.
	TArray<UPoseSearchDatabase*> Result;
	if (!MasterChooserTable)
	{
		VB_LOG(Warning, "[MM] MasterChooserTable null");
		return Result;
	}

	CollectDatabases(MasterChooserTable, Result);
	return Result;
}

void UVBAnimInstance::CollectDatabases(const UChooserTable* Chooser, TArray<UPoseSearchDatabase*>& OutDatabases) const
{
	// chooser 는 이 AnimInstance 를 컨텍스트로 값을 읽는다.
	// Multi 인 이유: 검색이 여러 DB 를 한 번에 봐야 모션매칭의 선택 폭이 유지된다.
	const TArray<UObject*> Selected = UChooserFunctionLibrary::EvaluateChooserMulti(this, Chooser, UPoseSearchDatabase::StaticClass());
	for (UObject* Entry : Selected)
	{
		if (UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(Entry))
		{
			OutDatabases.AddUnique(Database);
		}
	}
}

EPoseSearchInterruptMode UVBAnimInstance::GetMMInterruptMode() const
{
	FVBMMInterruptInputs In;
	In.MovementMode = MovementMode;
	In.MovementModeLastFrame = MovementMode_LastFrame;
	In.MMState = MMState;
	In.MMStateLastFrame = MMState_LastFrame;
	In.Gait = Gait;
	In.GaitLastFrame = Gait_LastFrame;
	In.Stance = Stance;
	In.StanceLastFrame = Stance_LastFrame;
	In.RotationMode = RotationMode;
	In.RotationModeLastFrame = RotationMode_LastFrame;
	In.bPoolInterrupt = bFirePoolInterrupt;
	In.SelectedDatabase = CurrentSelectedDatabase;
	return VBAnimMotionMatchingRules::ResolveInterruptMode(In);
}

bool UVBAnimInstance::JustLandedLight() const
{
	// 착지 직후 + 수직 속도가 Heavy 임계 미만.
	if (!JustLanded)
	{
		return false;
	}

	return FMath::Abs(LandVelocity.Z) < HeavyLandSpeedThreshold;
}

bool UVBAnimInstance::JustLandedHeavy() const
{
	// 착지 직후 + 수직 속도가 Heavy 임계 이상.
	if (!JustLanded)
	{
		return false;
	}

	return FMath::Abs(LandVelocity.Z) >= HeavyLandSpeedThreshold;
}

bool UVBAnimInstance::JustTraversed() const
{
	// GASP 그대로 ">1.0". 커브 정점이 정확히 1.0 이라 커브 쪽은 사실상 안 켜진다(임계를 낮추면 종료 컷이 생긴다).
	const float CurveVal = GetCurveValue(TEXT("MovingTraversal"));
	const bool bSlotActive = IsSlotActive(GASP_DefaultSlotName);
	return (CurveVal > 1.0f && !bSlotActive) || bJustTraversedFlag;
}

float UVBAnimInstance::TimeToLand() const
{
	return VBAnimLocomotionRules::ComputeTimeToLand(MovementMode, Velocity, LandPredictionHeight);
}

FTransform UVBAnimInstance::GetDesiredFacingTransform() const
{
	return VBAnimMotionMatchingRules::FindDesiredFacing(Trajectory, DesiredFacingLookaheadSeconds, CharacterTransform);
}

FQuat UVBAnimInstance::GetDesiredFacingOrientation() const
{
	// Steering.TargetOrientation 핀 바인딩용.
	return GetDesiredFacingTransform().GetRotation();
}

EOrientationWarpingSpace UVBAnimInstance::GetOrientationWarpingWarpingSpace() const
{
	// OrientationWarping 노드의 WarpingSpace 핀. GASP 그대로.
	return bOffsetRootBoneEnabled
		? EOrientationWarpingSpace::RootBoneTransform
		: EOrientationWarpingSpace::ComponentTransform;
}

FVector UVBAnimInstance::CalculateRelativeAccelerationAmount() const
{
	// 가속을 최대 가속/감속으로 정규화한 캐릭터 로컬 값([-1..1]). GASP 그대로.
	const APawn* Pawn = TryGetPawnOwner();
	if (!Pawn)
	{
		return FVector::ZeroVector;
	}

	const ACharacter* Char = Cast<ACharacter>(Pawn);
	if (!Char)
	{
		return FVector::ZeroVector;
	}

	const UCharacterMovementComponent* CMC = Char->GetCharacterMovement();
	if (!CMC)
	{
		return FVector::ZeroVector;
	}

	FVBRelativeAccelerationInputs In;
	In.Acceleration = Acceleration;
	In.Velocity = Velocity;
	In.VelocityAcceleration = VelocityAcceleration;
	In.CharacterRotation = CharacterTransform.GetRotation();
	In.MaxAcceleration = CMC->GetMaxAcceleration();
	In.MaxBrakingDeceleration = CMC->GetMaxBrakingDeceleration();
	return VBAnimLocomotionRules::ComputeRelativeAcceleration(In);
}

FVector2D UVBAnimInstance::GetLeanAmount() const
{
	return VBAnimLocomotionRules::ComputeLeanAmount(CalculateRelativeAccelerationAmount(), Speed2D, RunAnimRootMotionSpeed);
}

float UVBAnimInstance::GetLeanAmountX() const
{
	return GetLeanAmount().X;
}

void UVBAnimInstance::CacheMMSelectedDatabase(UPARAM(ref) const FAnimNodeReference& Node)
{
	// MM 노드가 고른 DB 와 그 태그를 캐시한다(GASP PostSelection 그대로).
	EAnimNodeReferenceConversionResult ConvertResult;
	const FMotionMatchingAnimNodeReference MMNode =
		UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNode(Node, ConvertResult);
	if (ConvertResult != EAnimNodeReferenceConversionResult::Succeeded)
	{
		return;
	}

	FPoseSearchBlueprintResult SearchResult;
	bool bValid = false;
	UMotionMatchingAnimNodeLibrary::GetMotionMatchingSearchResult(MMNode, SearchResult, bValid);
	if (bValid)
	{
		CurrentSelectedDatabase = SearchResult.SelectedDatabase;
		// worker thread 에서 읽지만 Tags 는 DB 의 고정 값이라 안전하다.
		if (CurrentSelectedDatabase)
		{
			CurrentDatabaseTags = CurrentSelectedDatabase->Tags;
		}
		else
		{
			CurrentDatabaseTags.Reset();
		}
	}
}

// ============================================================================
// 처형 상호작용 (GASP 5.8 Pose Search Interaction)
// ============================================================================

void UVBAnimInstance::EvaluateFinisherDatabases(TArray<UPoseSearchDatabase*>& OutDatabases) const
{
	OutDatabases.Reset();
	if (FinisherChooserTable)
	{
		CollectDatabases(FinisherChooserTable, OutDatabases);
	}
}
