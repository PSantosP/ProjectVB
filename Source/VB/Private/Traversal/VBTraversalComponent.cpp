// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Traversal/VBTraversalComponent.h"

#include "Traversal/VBLadder.h"
#include "Traversal/VBTraversalRules.h"

#include "Traversal/VBLevelBlock_Traversable.h"
#include "Animation/VBAnimInstance.h"   // UVBAnimInstance::InteractionTransform 직접 설정 (ABP 의 BPI_InteractionTransform 백킹 변수)
#include "Data/VBTraversalConfig.h"
#include "Data/VBClimbAnimSet.h"
#include "Components/SplineComponent.h"
#include "Character/VBCharacter.h"
#include "Character/VBHeroComponent.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "CharacterCommon/VBBodyControlPolicy.h"
#include "Interaction/VBElevator.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Interaction/VBInteractorComponent.h"   // 질주 자동 진입 - 상호작용 캐릭터 동작 중이면 안 붙는다
#include "Character/VBSlideMovementComponent.h"   // 질주 자동 진입 - 미끄러지는 중이면 안 붙는다
#include "AbilitySystem/VBAbilitySystemStatics.h" // 질주 자동 진입 - 처형, 죽음
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "MotionWarpingComponent.h"
#include "ChooserFunctionLibrary.h"   // UChooserTable 완전 정의 간접 포함 (-> Chooser.h)
#include "IObjectChooser.h"            // FChooserEvaluationContext
#include "StructUtils/InstancedStruct.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "VB/VB.h"

namespace
{
	// GASP 워프 타겟명 - 트래버설 몽타주의 MotionWarping NotifyState 가 참조하는 이름과 일치해야 함.
	static const FName WT_FrontLedge(TEXT("FrontLedge"));
	static const FName WT_BackLedge(TEXT("BackLedge"));
	static const FName WT_BackFloor(TEXT("BackFloor"));

	// 전방 Capsule 트레이스 거리. 지상은 GASP 처럼 전방 속도에 비례하고, 공중은 먼 벽을 잡지 않게 AirMantleTraceDistance 고정 단거리다.
	float ForwardTraceDistance(const ACharacter& Character, const UVBTraversalConfig& Cfg, bool bAirMode)
	{
		if (bAirMode)
		{
			return Cfg.AirMantleTraceDistance;
		}

		const float ForwardLocalSpeed = static_cast<float>(Character.GetActorRotation().UnrotateVector(Character.GetVelocity()).X);
		return FMath::GetMappedRangeValueClamped(
			FVector2f(Cfg.TraceForwardSpeedMin, Cfg.TraceForwardSpeedMax),
			FVector2f(Cfg.TraceForwardDistanceMin, Cfg.TraceForwardDistanceMax),
			ForwardLocalSpeed);
	}

	// 시작을 막는 캐릭터 상태는 VBBodyControlPolicy 가 정한다(상호작용 중, 래그돌).
	// 다른 소비처 목록과 합치지 않는다. 합치면 처형, 환경 상호작용 중 맨틀이 막힌다.
	bool IsTraversalStartBlocked(const AVBCharacter& Character)
	{
		const UVBGASPSmartObjectComponent* SmartObject = Character.FindComponentByClass<UVBGASPSmartObjectComponent>();
		const UVBPlayerRagdollComponent* Ragdoll = Character.FindComponentByClass<UVBPlayerRagdollComponent>();
		VBBodyControlPolicy::FTraversalStartFacts StartFacts;
		StartFacts.bSmartObjectInteracting = SmartObject && SmartObject->IsInteracting();
		StartFacts.bPlayerRagdoll = Ragdoll && Ragdoll->IsRagdollActive();
		return VBBodyControlPolicy::BlocksTraversalStart(StartFacts);
	}

	// chooser 평가 입력. Hurdle/Vault 행은 속도와 함께 요청 보행(질주 우선)으로 구분한다.
	FVBTraversalChooserParams MakeChooserParams(const FVBTraversalCheckResult& Result, float ObstacleHeight,
		const AVBCharacter& Character, const UAnimInstance* Anim)
	{
		FVBTraversalChooserParams Params;
		Params.ActionType = Result.ActionType;
		Params.ObstacleHeight = ObstacleHeight;
		Params.ObstacleDepth = Result.ObstacleDepth;
		Params.Speed = Character.GetVelocity().Size2D();
		if (const UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(&Character))
		{
			Params.Gait = Intent->GetIsSprinting() ? EVBGait::Sprint : Intent->GetGait();
		}
		else if (const UVBAnimInstance* State = Cast<UVBAnimInstance>(Anim))
		{
			Params.Gait = State->GetCurrentGait();
		}

		return Params;
	}
}

UVBTraversalComponent* UVBTraversalComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVBTraversalComponent>() : nullptr;
}

AVBCharacter* UVBTraversalComponent::GetCharacter() const
{
	return OwnerCharacter.Get();
}

bool UVBTraversalComponent::IsOwningClient() const
{
	const AVBCharacter* Character = OwnerCharacter.Get();
	return Character && !Character->HasAuthority() && Character->IsLocallyControlled();
}

bool UVBTraversalComponent::TryConsumeMoveInput(const FVector2D& Input)
{
	switch (AttachedSurface)
	{
	case EVBTraversalSurface::Hang:
		SetHangMoveInput(Input); // 좌우 = 옆걸음, 앞 = 올라서기, 뒤 = 놓기
		return true;
	case EVBTraversalSurface::Climb:
		SetClimbMoveInput(Input); // 두 축 그대로 벽 위의 이동
		return true;
	case EVBTraversalSurface::Ladder:
		SetLadderMoveInput(static_cast<float>(Input.Y)); // 위아래 하나뿐이다
		return true;
	default:
		return false;
	}
}

UVBTraversalComponent::UVBTraversalComponent()
{
	// 틱은 면에 붙어 있는 동안만 켠다(SetTraversalState). 맨틀류는 몽타주 델리게이트로만 돌아 틱이 필요 없다.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// 사다리 A 단계. 탑승 중에만 켜진다(FVBLadderMode 가 켜고 끈다).
	LadderIntentTick.bCanEverTick = true;
	LadderIntentTick.bStartWithTickEnabled = false;
	LadderIntentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true); // Server/Multicast RPC 용
}

void UVBTraversalComponent::RegisterComponentTickFunctions(bool bRegister)
{
	Super::RegisterComponentTickFunctions(bRegister);
	if (bRegister)
	{
		if (SetupActorComponentTickFunction(&LadderIntentTick))
		{
			LadderIntentTick.Target = this;
		}
	}
	else if (LadderIntentTick.IsTickFunctionRegistered())
	{
		LadderIntentTick.UnRegisterTickFunction();
	}
}

void FVBLadderIntentTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread,
	const FGraphEventRef& MyCompletionGraphEvent)
{
	FActorComponentTickFunction::ExecuteTickHelper(Target, /*bTickInEditor=*/false, DeltaTime, TickType,
		[this](float) { Target->LadderMode.UpdateIntent(Target->LadderHost); });
}

FString FVBLadderIntentTickFunction::DiagnosticMessage()
{
	return Target ? Target->GetFullName() + TEXT("[LadderIntent]") : TEXT("<null>[LadderIntent]");
}

FName FVBLadderIntentTickFunction::DiagnosticContext(bool bDetailed)
{
	return FName(TEXT("VBLadderIntent"));
}

void UVBTraversalComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerCharacter = Cast<AVBCharacter>(GetOwner());

	// 없는 것은 깨진 것이 아니다. 종전 동작으로 떨어지는 것을 한 줄로 밝힌다.
	if (!ClimbAnimSet || !ClimbAnimSet->HangClimbUpMontage)
	{
		VB_LOG(Warning, "Traversal: ClimbAnimSet(올라서기 몽타주) 미할당 - 매달림 비활성, 공중 잡기는 맨틀로만 간다");
	}

	if (ClimbAnimSet && !ClimbAnimSet->ClimbBlendSpace)
	{
		VB_LOG(Warning, "Traversal: 등반 블렌드스페이스 미할당 - 자유 등반 비활성, 렛지 없는 벽은 그냥 벽이다");
	}
	// 대기가 한 벌뿐이면 절반의 경우에 손발이 교차하므로 한 줄로 밝힌다.
	if (ClimbAnimSet && ClimbAnimSet->LadderIdleMontage && !ClimbAnimSet->LadderIdleMirrorMontage)
	{
		VB_LOG(Warning, "Ladder: 반대쪽 대기 몽타주 미할당 - 한 벌로 간다. "
			"오르내리기는 1.2초마다 좌우가 뒤집히므로 절반의 경우에 손발 넷이 40cm 씩 교차한다");
	}
	// 몽타주가 비면 등반이 움직이긴 하지만 미끄러지므로 부팅 때 밝힌다.
	if (ClimbAnimSet)
	{
		int32 Missing = 0;
		for (const UAnimMontage* M : { ClimbAnimSet->ClimbMontageUp.Get(), ClimbAnimSet->ClimbMontageDown.Get(),
			ClimbAnimSet->ClimbMontageLeft.Get(), ClimbAnimSet->ClimbMontageRight.Get(),
			ClimbAnimSet->ClimbMontageUpLeft.Get(), ClimbAnimSet->ClimbMontageUpRight.Get(),
			ClimbAnimSet->ClimbMontageDownLeft.Get(), ClimbAnimSet->ClimbMontageDownRight.Get() })
		{
			if (!M)
			{
				++Missing;
			}
		}

		if (Missing > 0)
		{
			VB_LOG(Warning, "등반 몽타주 %d/8 미할당 - 그 방향은 일정 속도로 밀려 발이 미끄러진다. "
				"DA_ClimbAnimSet 의 ClimbMontage* 를 채워라", Missing);
		}
	}
}

const UVBTraversalConfig* UVBTraversalComponent::GetConfig() const
{
	return Config ? Config : GetDefault<UVBTraversalConfig>();
}

bool UVBTraversalComponent::CapsuleSweep(const FVector& Start, const FVector& End, FHitResult& OutHit) const
{
	const ACharacter* Ch = OwnerCharacter.Get();
	if (!Ch || !Ch->GetCapsuleComponent() || !GetWorld())
	{
		return false;
	}

	const float R = Ch->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfH = Ch->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBTraversalSweep), false, Ch);
	return GetWorld()->SweepSingleByChannel(
		OutHit, Start, End, FQuat::Identity,
		GetConfig()->TraceChannel.GetValue(),
		FCollisionShape::MakeCapsule(R, HalfH), Params);
}

bool UVBTraversalComponent::TraceFloorBelowLedge(const FVector& LedgePoint, const FVector& LedgeNormal, float DownDepth,
	FHitResult& OutFloorHit) const
{
	const ACharacter* Ch = OwnerCharacter.Get();
	if (!Ch || !Ch->GetCapsuleComponent())
	{
		return false;
	}

	const float CapR = Ch->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfH = Ch->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float ProbeSkin = GetConfig()->LedgeProbeSkin;
	const FVector HOffset = LedgeNormal * (CapR + ProbeSkin);
	const FVector DownStart = LedgePoint + HOffset + FVector(0.0f, 0.0f, HalfH + ProbeSkin);
	const FVector DownEnd = LedgePoint + HOffset - FVector(0.0f, 0.0f, DownDepth);
	return CapsuleSweep(DownStart, DownEnd, OutFloorHit);
}

bool UVBTraversalComponent::PerformTraversalCheck(FVBTraversalCheckResult& R, bool bAirMode) const
{
	const ACharacter* Ch = OwnerCharacter.Get();
	if (!Ch || !Ch->GetCapsuleComponent())
	{
		return false;
	}

	const UVBTraversalConfig* C = GetConfig();
	const FVector ActorLoc = Ch->GetActorLocation();
	const FVector Forward = Ch->GetActorForwardVector();
	const float HalfH = Ch->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	// Step 2.1: 전방 Capsule 트레이스 -> LevelBlock_Traversable.
	const float TraceDist = ForwardTraceDistance(*Ch, *C, bAirMode);
	FHitResult FwdHit;
	if (!CapsuleSweep(ActorLoc, ActorLoc + Forward * TraceDist, FwdHit))
	{
		return false;
	}

	AVBLevelBlock_Traversable* Block = Cast<AVBLevelBlock_Traversable>(FwdHit.GetActor());
	if (!Block)
	{
		return false;
	}

	R.HitComponent = FwdHit.GetComponent();

	// Step 2.2: 렛지 transform 질의.
	Block->GetLedgeTransforms(FwdHit.ImpactPoint, ActorLoc, R);

	// Step 3.1: front 렛지 유효?
	if (!R.bHasFrontLedge)
	{
		return false;
	}

	// Step 3.2: 캐릭터->front 렛지 스탠드 위치로 공간 확인(장애물은 무시). 막히면 실패.
	if (!CheckRoomToFrontLedge(R, ActorLoc, HalfH))
	{
		return false;
	}

	// Step 3.3: 장애물 높이 = |front 렛지 Z − 발 Z|. 캡슐 중심으로 재면 절반높이만큼 작게 나온다(GASP 동일).
	const float FootZ = ActorLoc.Z - HalfH;
	R.ObstacleHeight = FMath::Abs(R.FrontLedgeLocation.Z - FootZ);

	MeasureObstacleDepth(R, HalfH);
	FindBackFloor(R, HalfH);
	return true;
}

bool UVBTraversalComponent::CheckRoomToFrontLedge(FVBTraversalCheckResult& R, const FVector& ActorLoc, float HalfH) const
{
	const FVector FrontStand = R.FrontLedgeLocation + FVector(0.0f, 0.0f, HalfH);
	FHitResult RoomHit;
	if (CapsuleSweep(ActorLoc, FrontStand, RoomHit))
	{
		// 장애물 자신이 아닌 무언가에 막힘 -> 공간 없음.
		if (RoomHit.GetComponent() != R.HitComponent)
		{
			R.bHasFrontLedge = false;
			return false;
		}
	}

	return true;
}

void UVBTraversalComponent::MeasureObstacleDepth(FVBTraversalCheckResult& R, float HalfH) const
{
	// Step 3.4/3.5: 상단 sweep(front->back)로 깊이 산출(back 렛지 있을 때).
	if (R.bHasBackLedge)
	{
		const FVector FrontTop = R.FrontLedgeLocation + FVector(0.0f, 0.0f, HalfH);
		const FVector BackTop = R.BackLedgeLocation + FVector(0.0f, 0.0f, HalfH);
		FHitResult TopHit;
		if (CapsuleSweep(FrontTop, BackTop, TopHit) && TopHit.GetComponent() != R.HitComponent)
		{
			// 가로지를 공간 없음 -> 깊이=front<->충돌점, back 렛지 무효화.
			R.ObstacleDepth = FVector::Dist2D(R.FrontLedgeLocation, TopHit.ImpactPoint);
			R.bHasBackLedge = false;
		}
		else
		{
			R.ObstacleDepth = FVector::Dist2D(R.FrontLedgeLocation, R.BackLedgeLocation);
		}
	}
	else
	{
		R.ObstacleDepth = 0.0f; // back 렛지 없음 -> 얇은 장애물.
	}
}

void UVBTraversalComponent::FindBackFloor(FVBTraversalCheckResult& R, float HalfH) const
{
	// Step 3.6: back 렛지에서 하향 트레이스로 바닥을 찾는다(GASP Step 3.6, TraceFloorBelowLedge).
	if (R.bHasBackLedge)
	{
		FHitResult FloorHit;
		if (TraceFloorBelowLedge(R.BackLedgeLocation, R.BackLedgeNormal,
			R.ObstacleHeight - HalfH + GetConfig()->BackFloorProbeExtraDepth, FloorHit))
		{
			R.bHasBackFloor = true;
			R.BackFloorLocation = FloorHit.ImpactPoint;
			R.BackLedgeHeight = FMath::Abs(R.BackLedgeLocation.Z - FloorHit.ImpactPoint.Z);
		}
		else
		{
			R.bHasBackFloor = false;
		}
	}
	else
	{
		R.bHasBackFloor = false;
	}
}

EVBTraversalActionType UVBTraversalComponent::DecideActionType(const FVBTraversalCheckResult& R) const
{
	// GASP Step 4.1 판정(값은 UVBTraversalConfig). 범위는 ObstacleHeight, 임계는 ObstacleDepth.
	if (!R.bHasFrontLedge)
	{
		return EVBTraversalActionType::None;
	}

	const UVBTraversalConfig* C = GetConfig();
	const float H = R.ObstacleHeight;
	const float D = R.ObstacleDepth;
	const bool bThin = D < C->ObstacleDepthThreshold; // <59 -> Vault/Hurdle
	const bool bThick = D >= C->ObstacleDepthThreshold; // >=59 -> Mantle

	// Vault: Height∈[Min,Max] ∧ 얇음 ∧ NOT HasBackFloor
	if (bThin && !R.bHasBackFloor && H >= C->VaultHeightMin && H <= C->VaultHeightMax)
	{
		return EVBTraversalActionType::Vault;
	}
	// Hurdle: Height∈[Min,Max] ∧ 얇음 ∧ HasBackFloor ∧ BackLedgeHeight>Min
	if (bThin && R.bHasBackFloor && H >= C->HurdleHeightMin && H <= C->HurdleHeightMax
		&& R.BackLedgeHeight > C->HurdleMinBackLedgeHeight)
	{
		return EVBTraversalActionType::Hurdle;
	}
	// Mantle: Height∈[Min,Max] ∧ 두꺼움 (chooser가 ≤150 Mantle / 150-275 Climb 분기)
	if (bThick && H >= C->MantleHeightMin && H <= C->MantleHeightMax)
	{
		return EVBTraversalActionType::Mantle;
	}

	return EVBTraversalActionType::None;
}

void UVBTraversalComponent::HandLedgeToAnim(UAnimInstance* Anim, const FVBTraversalCheckResult& Result)
{
	if (UVBAnimInstance* VBAnim = Cast<UVBAnimInstance>(Anim))
	{
		VBAnim->SetInteractionTransform(FTransform(FRotationMatrix::MakeFromZ(Result.FrontLedgeNormal).Rotator(),
			Result.FrontLedgeLocation));
	}
}

bool UVBTraversalComponent::TryTraversalAction(bool bAirMantleOnly)
{
	if (IsDoingTraversal() || !OwnerCharacter.IsValid())
	{
		return false;
	}

	if (IsTraversalStartBlocked(*OwnerCharacter.Get()))
	{
		return false;
	}

	// Step 1-3: 트레이스. (공중 모드는 전방 트레이스가 AirMantleTraceDistance 고정 단거리)
	FVBTraversalCheckResult Result;
	if (!PerformTraversalCheck(Result, bAirMantleOnly))
	{
		// 렛지가 없으면 사다리가 먼저다. 사다리는 전용 액터라 있는지를 정확히 안다.
		if (TryStartLadder())
		{
			return true;
		}
		// 바라보는 면이 등반 가능하면 붙는다(렛지 없는 높은 벽 = 자유 등반).
		return ClimbMode.TryStart(*this);
	}

	// Step 4.1: ActionType 결정.
	Result.ActionType = DecideActionType(Result);
	if (Result.ActionType == EVBTraversalActionType::None)
	{
		return false;
	}

	// 지상에서 항상 매달림 블록을 만나면 몽타주를 고르지 않고 일반 점프로 처리한다.
	// 그래야 공중 체크가 렛지를 잡아 매달린다. 매달림이 꺼져 있으면 그대로 올라간다.
	if (!bAirMantleOnly && ClimbAnimSet && ClimbAnimSet->HangClimbUpMontage && FVBHangMode::IsAlwaysHangBlock(Result))
	{
		VB_LOG(Verbose, "Traversal GROUND: 항상 매달림 블록 - 몽타주 대신 점프로 넘김");
		return false;
	}

	if (bAirMantleOnly && !PassesAirMantleGate(Result))
	{
		return false;
	}

	return StartChosenTraversal(Result, bAirMantleOnly);
}

bool UVBTraversalComponent::PassesAirMantleGate(const FVBTraversalCheckResult& Result) const
{
	// 공중 모드 게이트: Mantle(/Climb)만 허용하고, 렛지가 발보다 MantleHeightMin 이상 위일 때만 잡는다.
	// H 는 |렛지Z−발Z| 라 렛지가 발 아래여도 임계를 넘을 수 있어 부호 있는 높이로 거른다.
	// 놓은 직후에는 같은 렛지를 다시 잡지 않는다 - 점프 키를 누른 채 놓으면 다음 틱에 바로 다시 잡힌다.
	if (GetWorld() && GetWorld()->GetTimeSeconds() < Reentry.HangRegrabBlockUntil)
	{
		return false;
	}

	if (Result.ActionType != EVBTraversalActionType::Mantle)
	{
		return false;
	}

	const ACharacter* Ch = OwnerCharacter.Get();
	const float FootZ = Ch->GetActorLocation().Z - Ch->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (Result.FrontLedgeLocation.Z - FootZ < GetConfig()->MantleHeightMin)
	{
		return false;
	}
	// 캡슐 중심이 렛지보다 아래일 때만 잡는다. 상체가 이미 렛지 위면 진입 포즈가 안 맞아 캐릭터가 내려갔다 올라간다.
	if (Ch->GetActorLocation().Z >= Result.FrontLedgeLocation.Z)
	{
		VB_LOG(Verbose, "AirMantle 보류: 캡슐중심(%.0f) >= 렛지(%.0f) — 하강 후 재시도",
			Ch->GetActorLocation().Z, Result.FrontLedgeLocation.Z);
		return false;
	}

	return true;
}

bool UVBTraversalComponent::StartChosenTraversal(FVBTraversalCheckResult& Result, bool bAirMantleOnly)
{
	USkeletalMeshComponent* Mesh = OwnerCharacter->GetMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		return false;
	}
	// 입력이 닿는 플레이어만 여기 온다. 비어 있으면 맨틀이 소리 없이 멈추므로 한 번 크게 밝히고 거절한다.
	if (!ensureMsgf(GetConfig()->TraversalChooserTable, TEXT("Traversal: %s 의 TraversalChooserTable 이 비어 있다"),
		*GetNameSafe(GetConfig())))
	{
		return false;
	}

	// Step 5.2: front 렛지 transform 을 AnimInstance 에 전달(PSS attach 채널, ABP 가 BPI_InteractionTransform 으로 읽는다).
	// 회전은 MakeRotFromZ(normal)로 GASP 와 같다. 워프용 MakeRotFromX(-normal)을 쓰면 MotionMatch 검색이 빈다.
	HandLedgeToAnim(Anim, Result);

	// 공중 모드 chooser 높이 보정: 공중에선 발 기준 높이가 작게 나오므로 애니 선택만 벽 실높이로 평가한다.
	// 발동 판정(DecideActionType)은 발 기준 그대로다.
	float ChooserObstacleHeight = Result.ObstacleHeight;
	if (bAirMantleOnly)
	{
		// 앞면 하향 트레이스로 벽 실높이를 잰다(MantleHeightMax 까지 쓸어내린다).
		FHitResult FrontFloorHit;
		const bool bFrontFloorFound = TraceFloorBelowLedge(Result.FrontLedgeLocation, Result.FrontLedgeNormal,
			GetConfig()->MantleHeightMax + GetConfig()->FrontFloorProbeExtraDepth, FrontFloorHit);

		// 그 깊이 안에 바닥이 없으면 올라갈 벽이 아니라 매달릴 렛지다. 바닥이 있으면 종전 경로다.
		if (HangMode.ShouldHangInsteadOfMantle(*this, Result, bFrontFloorFound))
		{
			return HangMode.TryGrabLedge(*this, Result);
		}

		if (bFrontFloorFound)
		{
			ChooserObstacleHeight = FMath::Abs(Result.FrontLedgeLocation.Z - FrontFloorHit.ImpactPoint.Z);
		}
		else
		{
			// 바닥이 없고 매달림이 꺼져 있으면 최댓값으로 클램프해 가장 묵직한 Climb 이 골라지게 한다.
			ChooserObstacleHeight = GetConfig()->MantleHeightMax;
		}
	}

	FVBTraversalChooserParams Params = MakeChooserParams(Result, ChooserObstacleHeight, *OwnerCharacter.Get(), Anim);
	FPoseSearchBlueprintResult MM;
	UAnimMontage* Selected = SelectTraversalMontage(Params, Anim, MM);
	if (!Selected)
	{
		return false;
	}

	// 발동 1줄 로그(모드, 높이, 몽타주, 진입 시간).
	VB_LOG(Log, "Traversal %s: type=%d H_feet=%.0f H_chooser=%.0f D=%.0f speed=%.0f montage=%s start=%.2f rate=%.2f",
		bAirMantleOnly ? TEXT("AIR") : TEXT("GROUND"), (int32)Result.ActionType,
		Result.ObstacleHeight, ChooserObstacleHeight, Result.ObstacleDepth, Params.Speed,
		*Selected->GetName(), MM.SelectedTime, MM.WantedPlayRate);

	// Step 5.5: 서버 권위로 수행 요청(클라가 선택 -> 서버가 전원 복제).
	ServerPerformTraversal(Result, Selected, MM.SelectedTime, MM.WantedPlayRate);
	return true;
}

UAnimMontage* UVBTraversalComponent::SelectTraversalMontage(FVBTraversalChooserParams& Params, UAnimInstance* Anim,
	FPoseSearchBlueprintResult& OutMatch)
{
	// Step 5.3: CHT_VB_TraversalAnims 평가(struct 컨텍스트) -> 후보 몽타주 배열.
	FChooserEvaluationContext Context = UChooserFunctionLibrary::MakeChooserEvaluationContext();
	Context.AddStructParam(Params); // 참조 저장 - Params 는 본 평가 스코프 내 유지.
	const FInstancedStruct Eval = UChooserFunctionLibrary::MakeEvaluateChooser(GetConfig()->TraversalChooserTable);
	TArray<UObject*> Candidates = UChooserFunctionLibrary::EvaluateObjectChooserBaseMulti(
		Context, Eval, UAnimMontage::StaticClass());
	if (Candidates.Num() == 0)
	{
		VB_LOG(Warning, "Traversal: chooser가 후보 몽타주 0개 반환 (ActionType=%d)", (int32)Params.ActionType);
		return nullptr;
	}

	// Step 5.4: MotionMatch - 현재 포즈/렛지 거리로 최적 몽타주 + 시작시간 선택.
	UPoseSearchLibrary::MotionMatch(
		Anim, Candidates, PoseHistoryName,
		FPoseSearchContinuingProperties(), FPoseSearchFutureProperties(), OutMatch);

	UAnimMontage* Selected = Cast<UAnimMontage>(OutMatch.SelectedAnim);
	if (!Selected)
	{
		VB_LOG(Warning, "Traversal: MotionMatch 결과 무효(몽타주 미선택)");
	}

	return Selected;
}

bool UVBTraversalComponent::ServerPerformTraversal_Validate(const FVBTraversalCheckResult& Result, UAnimMontage* Montage,
	float StartTime, float PlayRate)
{
	// 악성 패킷 차단(실패 = 연결 종료): 몽타주, ActionType, 재생 파라미터, 위치 유효성.
	if (!Montage)
	{
		return false;
	}
	// None 은 따로 거부한다(IsValidEnumValue 는 None 도 통과시킨다).
	// 상한을 Mantle 리터럴로 박지 않는다. 새 값을 추가하는 순간 그 트래버설이 연결 종료가 된다.
	const UEnum* ActionEnum = StaticEnum<EVBTraversalActionType>();
	if (Result.ActionType == EVBTraversalActionType::None
		|| !ActionEnum || !ActionEnum->IsValidEnumValue(static_cast<int64>(Result.ActionType)))
	{
		return false;
	}

	if (!FMath::IsFinite(PlayRate) || PlayRate <= 0.0f)
	{
		return false;
	}
	// StartTime 은 몽타주 길이 내(음수/초과 금지 - Montage 는 위에서 non-null 보장).
	if (!FMath::IsFinite(StartTime) || StartTime < 0.0f || StartTime > Montage->GetPlayLength())
	{
		return false;
	}

	if (Result.FrontLedgeLocation.ContainsNaN() || Result.BackLedgeLocation.ContainsNaN())
	{
		return false;
	}

	return true;
}

void UVBTraversalComponent::ServerPerformTraversal_Implementation(const FVBTraversalCheckResult& Result, UAnimMontage* Montage,
	float StartTime, float PlayRate)
{
	// [서버 권위] 클라가 보낸 좌표를 서버 위치 기준 거리 상한으로만 검증한다(desync 로 정당 동작이 거부되지 않게).
	// 상한은 MaxPlausibleLedgeDistance 에 따로 둔다. 맨틀 높이 튜닝이 서버 검증을 느슨하게 만들지 않게.
	if (OwnerCharacter.IsValid())
	{
		const FVector CharLoc = OwnerCharacter->GetActorLocation();
		const float MaxPlausibleLedgeDist = GetConfig()->MaxPlausibleLedgeDistance;
		// 전방 렛지는 항상, 뒤 렛지, 뒤 바닥은 쓰는 동작에서만(0 이 아닐 때) 검증한다.
		const bool bFrontFar = FVector::Dist(CharLoc, Result.FrontLedgeLocation) > MaxPlausibleLedgeDist;
		const bool bBackLedgeFar = !Result.BackLedgeLocation.IsNearlyZero() && FVector::Dist(CharLoc,
			Result.BackLedgeLocation) > MaxPlausibleLedgeDist;
		const bool bBackFloorFar = !Result.BackFloorLocation.IsNearlyZero() && FVector::Dist(CharLoc,
			Result.BackFloorLocation) > MaxPlausibleLedgeDist;
		if (bFrontFar || bBackLedgeFar || bBackFloorFar)
		{
			VB_LOG(Warning, "Traversal 거부: 서버-클라 워프 타겟 거리 초과 (조작 의심 front=%d backL=%d backF=%d)",
				bFrontFar, bBackLedgeFar, bBackFloorFar);
			return;
		}
	}

	MulticastPerformTraversal(Result, Montage, StartTime, PlayRate);
}

void UVBTraversalComponent::MulticastPerformTraversal_Implementation(const FVBTraversalCheckResult& Result, UAnimMontage* Montage,
	float StartTime, float PlayRate)
{
	PerformTraversalAction(Result, Montage, StartTime, PlayRate);
}

void UVBTraversalComponent::PerformTraversalAction(const FVBTraversalCheckResult& Result, UAnimMontage* Montage, float StartTime,
	float PlayRate)
{
	if (!OwnerCharacter.IsValid() || !Montage)
	{
		return;
	}

	BeginMove();
	// GASP Select 핀 값: Vault -> Falling(낙하로 종료), 그 외 -> Walking.
	Session.EnterMove(*OwnerCharacter.Get(), Result.HitComponent, Result.ActionType == EVBTraversalActionType::Vault ? MOVE_Falling : MOVE_Walking);
	UpdateWarpTargets(Result, Montage);

	// 다른 클라도 FromTraversal 블렌드/채널용으로 InteractionTransform 보유.
	HandLedgeToAnim(OwnerCharacter->GetMesh() ? OwnerCharacter->GetMesh()->GetAnimInstance() : nullptr, Result);
	PlayTraversalMontage(Montage, StartTime, PlayRate);
}

bool UVBTraversalComponent::PlayTraversalMontage(UAnimMontage* Montage, float StartTime, float PlayRate)
{
	USkeletalMeshComponent* Mesh = OwnerCharacter.IsValid() ? OwnerCharacter->GetMesh() : nullptr;
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (Anim && Montage)
	{
		// false = 같은 그룹 몽타주를 끊지 않는다. 한 번 재생하는 동작이라 DriveLoopingMontage 와 달리 그대로 둔다.
		// 재생이 실패하면 콜백이 오지 않으므로 아래와 같이 바로 복구한다.
		if (Anim->Montage_Play(Montage, PlayRate, EMontagePlayReturnType::MontageLength, StartTime, false) <= 0.0f)
		{
			VB_LOG(Warning, "트래버설 몽타주 %s 재생 실패 - 바로 복구한다", *Montage->GetName());
			RecoverFromTraversal();
			return false;
		}
		// 조기 탈출은 BlendingOut, 자연 종료는 Ended 에서 복구한다(RecoverFromTraversal 가 1회만 실행).
		MontageBlendingOutDelegate.BindUObject(this, &UVBTraversalComponent::OnTraversalMontageBlendingOut);
		Anim->Montage_SetBlendingOutDelegate(MontageBlendingOutDelegate, Montage);
		MontageEndedDelegate.BindUObject(this, &UVBTraversalComponent::OnTraversalMontageEnded);
		Anim->Montage_SetEndDelegate(MontageEndedDelegate, Montage);
		return true;
	}
	// 애님이 없으면 콜백을 거치지 않고 직접 복구한다. 콜백은 bInterrupted 로 역할을 나눠 무조건 복구가 안 된다.
	// 복구가 빠지면 Flying, 콜리전 무시, 임시수납이 남고 캡슐 회전이 멈춘다.
	RecoverFromTraversal();
	return false;
}

void UVBTraversalComponent::UpdateWarpTargets(const FVBTraversalCheckResult& Result, UAnimMontage* /*Montage*/)
{
	UMotionWarpingComponent* MW = OwnerCharacter.IsValid() ? OwnerCharacter->FindComponentByClass<UMotionWarpingComponent>() : nullptr;
	if (!MW)
	{
		return;
	}

	// FrontLedge: 항상. 회전 = MakeRotFromX(-frontNormal).
	// 매달림 올라서기만 렛지 안쪽 stand 지점을 쓴다(워프 창이 전 구간이라 타겟이 곧 최종 위치).
	const FRotator FrontRot = FRotationMatrix::MakeFromX(-Result.FrontLedgeNormal).Rotator();
	FVector FrontTarget = Result.FrontLedgeLocation;
	if (Result.ActionType == EVBTraversalActionType::Hang && ClimbAnimSet)
	{
		FrontTarget -= Result.FrontLedgeNormal * ClimbAnimSet->HangClimbUpStandInset;
	}

	MW->AddOrUpdateWarpTargetFromLocationAndRotation(WT_FrontLedge, FrontTarget, FrontRot);

	// BackLedge - Hurdle/Vault 만.
	if (Result.bHasBackLedge &&
		(Result.ActionType == EVBTraversalActionType::Hurdle || Result.ActionType == EVBTraversalActionType::Vault))
	{
		const FRotator BackRot = FRotationMatrix::MakeFromX(-Result.BackLedgeNormal).Rotator();
		MW->AddOrUpdateWarpTargetFromLocationAndRotation(WT_BackLedge, Result.BackLedgeLocation, BackRot);
	}
	else
	{
		MW->RemoveWarpTarget(WT_BackLedge);
	}

	// BackFloor: Hurdle 만.
	if (Result.bHasBackFloor && Result.ActionType == EVBTraversalActionType::Hurdle)
	{
		MW->AddOrUpdateWarpTargetFromLocationAndRotation(WT_BackFloor, Result.BackFloorLocation, FrontRot);
	}
	else
	{
		MW->RemoveWarpTarget(WT_BackFloor);
	}
}

void UVBTraversalComponent::OnTraversalMontageBlendingOut(UAnimMontage* /*Montage*/, bool bInterrupted)
{
	// 조기 탈출만. 블렌드아웃 시작 즉시 복구해야 Walking 으로 흐르듯 이어진다.
	if (bInterrupted)
	{
		RecoverFromTraversal();
	}
}

void UVBTraversalComponent::OnTraversalMontageEnded(UAnimMontage* /*Montage*/, bool bInterrupted)
{
	// 자연 종료(완주)만 - 조기 탈출은 BlendingOut(bInterrupted=true)에서 이미 복구됨.
	if (!bInterrupted)
	{
		RecoverFromTraversal();
	}
}

void UVBTraversalComponent::SetTraversalState(const EVBTraversalPhase NewPhase, const EVBTraversalSurface NewSurface)
{
	// 면은 Attached 일 때만 있다. 어긋난 요청은 값을 바꾸지 않고 매번 Error 로 알린다.
	if ((NewPhase == EVBTraversalPhase::Attached) != (NewSurface != EVBTraversalSurface::None))
	{
		VB_LOG(Error, "Traversal 상태 거절: 단계 %s 와 면 %s 는 함께 있을 수 없다", *UEnum::GetValueAsString(NewPhase),
			*UEnum::GetValueAsString(NewSurface));
		return;
	}

	TraversalPhase = NewPhase;
	AttachedSurface = NewSurface;
	// TickComponent 는 붙어 있는 모드를 갱신하는 일만 한다.
	SetComponentTickEnabled(NewPhase == EVBTraversalPhase::Attached);

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerCharacter.Get());
	if (!ASC)
	{
		if (!bWarnedNoTagTarget)
		{
			bWarnedNoTagTarget = true;
			VB_LOG(Warning, "Traversal: ASC 가 없어 State.Traversal 태그를 쓰지 못한다 - 단계와 면만 바뀐다");
		}

		return;
	}

	bWarnedNoTagTarget = false;
	ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Traversal_Hang, NewSurface == EVBTraversalSurface::Hang ? 1 : 0);
	ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Traversal_Ladder, NewSurface == EVBTraversalSurface::Ladder ? 1 : 0);
	ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Traversal_Climb, NewSurface == EVBTraversalSurface::Climb ? 1 : 0);
	ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Traversal_Move, NewPhase == EVBTraversalPhase::Move ? 1 : 0);
	ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Traversal_Exit, NewPhase == EVBTraversalPhase::Exiting ? 1 : 0);
}

void UVBTraversalComponent::BeginMove() { SetTraversalState(EVBTraversalPhase::Move, EVBTraversalSurface::None); }
void UVBTraversalComponent::Attach(const EVBTraversalSurface NewSurface) { SetTraversalState(EVBTraversalPhase::Attached, NewSurface); }
void UVBTraversalComponent::EndSession() { SetTraversalState(EVBTraversalPhase::None, EVBTraversalSurface::None); }

void UVBTraversalComponent::Detach()
{
	if (TraversalPhase == EVBTraversalPhase::Attached)
	{
		SetTraversalState(EVBTraversalPhase::Exiting, EVBTraversalSurface::None);
	}
}

void UVBTraversalComponent::RecoverFromTraversal()
{
	if (!IsDoingTraversal())
	{
		return; // 이미 복구됨 (양 콜백 중 첫 호출만 유효).
	}
	// 면은 정리 분기가 읽도록 받아 두고 세션을 먼저 끝낸다. 정리 안의 Detach 가 Exiting 으로 되살리지 않는다.
	const EVBTraversalSurface WasSurface = AttachedSurface;
	EndSession();
	// 질주 자동 진입은 끝난 직후 한 주기 쉰다(같은 자리 재판정 방지). C 놓기 잠금은 Request*Drop 이 건다.
	if (const UVBTraversalConfig* AutoCfg = GetConfig(); AutoCfg && GetWorld())
	{
		Reentry.AutoTraversalNextCheckTime = GetWorld()->GetTimeSeconds() + AutoCfg->AutoTraversalCheckInterval;
	}
	// 매달림 흔적도 여기서 지운다. 어느 길로 끝나든 복구의 홈은 이 함수다.
	HangMode.ClearOnExit(*this);

	// 등반 흔적도 여기서 지운다. 루트모션 모드를 되돌리지 않으면 지상 로코모션이 죽는다.
	if (WasSurface == EVBTraversalSurface::Climb)
	{
		ClimbMode.ClearOnExit(*this);
	}

	if (WasSurface == EVBTraversalSurface::Ladder)
	{
		LadderMode.ClearRiding(LadderHost);
	}
	// 꼭대기 올라서기는 탑승만 먼저 끝내므로 막는 상자 무시와 지지 요청은 탑승 여부와 상관없이 여기서 해제한다.
	LadderMode.ReleaseOnRecover(LadderHost);
	Session.Restore(OwnerCharacter.Get());

	if (OwnerCharacter.IsValid())
	{
		if (UMotionWarpingComponent* MW = OwnerCharacter->FindComponentByClass<UMotionWarpingComponent>())
		{
			MW->RemoveWarpTarget(WT_FrontLedge);
			MW->RemoveWarpTarget(WT_BackLedge);
			MW->RemoveWarpTarget(WT_BackFloor);
		}
	}
}

void UVBTraversalComponent::RecoverByFalling()
{
	Session.SetRecoverMode(MOVE_Falling);
	RecoverFromTraversal();
}

// 매달림: 본문은 FVBHangMode. 여기는 RPC 와 공개 진입이다.

bool UVBTraversalComponent::ServerStartHang_Validate(const FVBTraversalCheckResult& Result)
{
	// 패킷 위생만(실패 = 연결 종료). 거리 상한은 _Implementation 이 거부로 처리한다.
	return Result.ActionType == EVBTraversalActionType::Hang
		&& Result.FrontLedgeSpline != nullptr
		&& !Result.FrontLedgeLocation.ContainsNaN() && !Result.FrontLedgeNormal.ContainsNaN();
}

void UVBTraversalComponent::ServerStartHang_Implementation(const FVBTraversalCheckResult& Result)
{
	if (IsDoingTraversal())
	{
		return; // 이미 도는 중(맨틀 몽타주, 매달림) - 원격 클라의 재요청 무시
	}

	if (OwnerCharacter.IsValid()
		&& FVector::Dist(OwnerCharacter->GetActorLocation(), Result.FrontLedgeLocation) > GetConfig()->MaxPlausibleLedgeDistance)
	{
		VB_LOG(Warning, "Traversal 거부: 매달림 렛지 거리 초과 (조작 의심)");
		return;
	}

	MulticastStartHang(Result);
}

void UVBTraversalComponent::MulticastStartHang_Implementation(const FVBTraversalCheckResult& Result)
{
	HangMode.Start(*this, Session, Result);
}

void UVBTraversalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (IsHanging())
	{
		HangMode.Update(*this, DeltaTime);
	}
	else if (IsClimbing())
	{
		ClimbMode.Update(*this, DeltaTime);
	}

	if (IsOnLadder())
	{
		LadderMode.UpdatePlacement(LadderHost, DeltaTime);
	}
}

void UVBTraversalComponent::SetHangMoveInput(const FVector2D& Input)
{
	HangMode.SetMoveInput(*this, Input);
}

bool UVBTraversalComponent::ServerSetHangMoveInputX_Validate(float InputX)
{
	return FMath::IsFinite(InputX);
}

void UVBTraversalComponent::ServerSetHangMoveInputX_Implementation(float InputX)
{
	HangMode.ApplyMoveInputX(InputX);
}

void UVBTraversalComponent::RequestHangClimbUp()
{
	HangMode.RequestClimbUp(*this);
}

void UVBTraversalComponent::RequestHangDrop()
{
	HangMode.RequestDrop(*this);
}

bool UVBTraversalComponent::ServerHangClimbUp_Validate() { return true; }
void UVBTraversalComponent::ServerHangClimbUp_Implementation()
{
	if (IsHanging())
	{
		MulticastHangClimbUp();
	}
}
void UVBTraversalComponent::MulticastHangClimbUp_Implementation()
{
	HangMode.StartClimbUp(*this, Session);
}

bool UVBTraversalComponent::ServerHangDrop_Validate() { return true; }
void UVBTraversalComponent::ServerHangDrop_Implementation()
{
	if (IsHanging())
	{
		MulticastHangDrop();
	}
}
void UVBTraversalComponent::MulticastHangDrop_Implementation()
{
	HangMode.EndByDrop(*this);
}

void UVBTraversalComponent::BlockHangRegrab()
{
	if (const UWorld* World = GetWorld())
	{
		Reentry.HangRegrabBlockUntil = World->GetTimeSeconds() + GetConfig()->HangRegrabDelay;
	}
}

// 사다리: 본문은 FVBLadderMode. 여기는 RPC, 공개 진입과 IVBLadderModeHost 구현이다.

bool UVBTraversalComponent::TryStartLadder()
{
	return LadderMode.TryStart(LadderHost);
}

bool UVBTraversalComponent::ServerStartLadder_Validate(AVBLadder* Ladder, float Height)
{
	return Ladder != nullptr && FMath::IsFinite(Height);
}

void UVBTraversalComponent::ServerStartLadder_Implementation(AVBLadder* Ladder, float Height)
{
	float AcceptedHeight = 0.0f;
	if (Ladder && LadderMode.AcceptServerStart(LadderHost, Ladder, Height, AcceptedHeight))
	{
		MulticastStartLadder(Ladder, AcceptedHeight);
	}
}

void UVBTraversalComponent::MulticastStartLadder_Implementation(AVBLadder* Ladder, float Height)
{
	LadderMode.Start(LadderHost, Session, Ladder, Height);
}

void UVBTraversalComponent::SetLadderMoveInput(float InputY)
{
	LadderMode.SetMoveInput(LadderHost, InputY);
}

void UVBTraversalComponent::RequestLadderDrop()
{
	LadderMode.RequestDrop(LadderHost);
}

bool UVBTraversalComponent::IsBodyBusyForTraversal() const
{
	const AVBCharacter* Player = OwnerCharacter.Get();
	if (!Player)
	{
		return true;
	}

	const UVBGASPSmartObjectComponent* SmartObject = Player->FindComponentByClass<UVBGASPSmartObjectComponent>();
	const UVBPlayerRagdollComponent* Ragdoll = Player->FindComponentByClass<UVBPlayerRagdollComponent>();
	VBBodyControlPolicy::FTraversalStartFacts StartFacts;
	StartFacts.bSmartObjectInteracting = SmartObject && SmartObject->IsInteracting();
	StartFacts.bPlayerRagdoll = Ragdoll && Ragdoll->IsRagdollActive();
	const UVBInteractorComponent* Interactor = Player->FindComponentByClass<UVBInteractorComponent>();
	// 움직이는 승강기 발판 위에서는 시작하지 않는다(워프 목표가 바닥을 따라가지 않는다).
	return VBBodyControlPolicy::BlocksTraversalStart(StartFacts) || (Interactor && Interactor->OwnsCapsuleFacing())
		|| UVBAbilitySystemStatics::IsActorFinishing(Player) || UVBAbilitySystemStatics::IsActorDead(Player)
		|| AVBElevator::IsBaseMoving(Player);
}

void UVBTraversalComponent::TryAutoTraversal()
{
	const UVBTraversalConfig* Cfg = GetConfig();
	AVBCharacter* Player = OwnerCharacter.Get();
	if (!Cfg || !Cfg->bAutoTraversalOnSprint || !Player || !Player->IsLocallyControlled() || !GetWorld())
	{
		return;
	}

	const UCharacterMovementComponent* CMC = Player->GetCharacterMovement();
	if (!CMC || !(CMC->IsMovingOnGround() || CMC->IsFalling()))
	{
		return; // 트래버설 중(Flying), 수영 등은 이 길이 아니다
	}
	// 의도 = 질주 상태 + 미는 방향이 캐릭터 앞 + 속도. 사다리, 등반 전용 입력은 두지 않는다.
	const FVector InputDir = CMC->GetCurrentAcceleration().GetSafeNormal2D();
	const float ForwardDot = InputDir.IsNearlyZero() ? 0.0f
		: static_cast<float>(FVector::DotProduct(InputDir, Player->GetActorForwardVector().GetSafeNormal2D()));
	// 놓은 뒤 잠금은 이동 키 원래 입력과 질주로만 푼다.
	// 매달림 중 이동 입력은 CMC 로 안 가서 놓은 직후 가속이 0 이라 뗀 것으로 잘못 읽힌다.
	const bool bSprinting = UVBLocomotionIntentComponent::IsSprintingFor(Player);
	if (!VBTraversalRules::Auto::HoldsAutoTraversalInput(bSprinting, UVBHeroComponent::GetRawMoveInputFor(Player)))
	{
		Reentry.bAutoNeedsFreshIntent = false;
		return;
	}
	// 붙을지는 지금 실제로 가는 쪽(CMC 가속 방향)이 캐릭터 앞인지로 본다 - 입력 축(카메라 기준)과 장애물 방향은 다르다.
	const bool bIntent = VBTraversalRules::Auto::HasAutoTraversalIntent(bSprinting, ForwardDot, static_cast<float>(CMC->Velocity.Size2D()),
		Cfg->AutoTraversalMinForwardDot, Cfg->AutoTraversalMinSpeed);
	if (!bIntent)
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Reentry.bAutoNeedsFreshIntent || IsDoingTraversal() || Now < Reentry.AutoTraversalNextCheckTime)
	{
		return;
	}

	Reentry.AutoTraversalNextCheckTime = Now + Cfg->AutoTraversalCheckInterval;
	// 캐릭터를 다른 기능이 점유하고 있으면 안 붙는다(IsBodyBusyForTraversal + 자동 경로만: 미끄러짐, 몽타주).
	const UVBSlideMovementComponent* SlideCMC = Cast<UVBSlideMovementComponent>(CMC);
	const UAnimInstance* Anim = Player->GetMesh() ? Player->GetMesh()->GetAnimInstance() : nullptr;
	if (IsBodyBusyForTraversal() || (SlideCMC && SlideCMC->IsSliding()) || (Anim && Anim->IsAnyMontagePlaying()))
	{
		return;
	}

	TryTraversalAction( /*bAirMantleOnly=*/CMC->IsFalling());
}

bool UVBTraversalComponent::ServerSetLadderMoveInput_Validate(float InputY)
{
	return FMath::IsFinite(InputY);
}

void UVBTraversalComponent::ServerSetLadderMoveInput_Implementation(float InputY)
{
	LadderMode.ApplyMoveInput(InputY);
}

bool UVBTraversalComponent::ServerLadderDrop_Validate() { return true; }
void UVBTraversalComponent::ServerLadderDrop_Implementation() { MulticastLadderDrop(); }
void UVBTraversalComponent::MulticastLadderDrop_Implementation() { LadderMode.End(LadderHost); }

void UVBTraversalComponent::MulticastLadderTopOut_Implementation(FVector LedgePoint, FVector LedgeNormal)
{
	LadderMode.StartTopOut(LadderHost, LedgePoint, LedgeNormal);
}

AVBCharacter* UVBTraversalComponent::FLadderHost::GetCharacter() const { return Owner.OwnerCharacter.Get(); }
const UVBClimbAnimSet* UVBTraversalComponent::FLadderHost::GetClips() const { return Owner.ClimbAnimSet; }
const UVBTraversalConfig* UVBTraversalComponent::FLadderHost::GetConfig() const { return Owner.GetConfig(); }
void UVBTraversalComponent::FLadderHost::EndSessionFalling() { Owner.RecoverByFalling(); }
void UVBTraversalComponent::FLadderHost::RequireFreshAutoIntent() { Owner.Reentry.bAutoNeedsFreshIntent = true; }

void UVBTraversalComponent::FLadderHost::SetAttached(bool bAttached)
{
	if (bAttached)
	{
		Owner.Attach(EVBTraversalSurface::Ladder);
	}
	else
	{
		Owner.Detach();
	}
}

namespace
{
	// 서버면 멀티캐스트, 클라이언트면 서버 RPC 로 요청을 보낸다. 소유 캐릭터가 없으면 보내지 않는다.
	template <typename TMulticast, typename TServer>
	void RouteByAuthority(const AActor* Character, TMulticast&& Multicast, TServer&& Server)
	{
		if (!Character)
		{
			return;
		}

		if (Character->HasAuthority())
		{
			Multicast();
		}
		else
		{
			Server();
		}
	}
}

// IVBLadderModeHost 구현.

bool UVBTraversalComponent::FLadderHost::IsRidingLadder() const { return Owner.IsOnLadder(); }
bool UVBTraversalComponent::FLadderHost::IsInSurfaceMode() const { return Owner.OwnsCapsuleFacing(); }
bool UVBTraversalComponent::FLadderHost::IsInAnyTraversal() const { return Owner.IsDoingTraversal(); }
bool UVBTraversalComponent::FLadderHost::IsBodyBusyForTraversal() const { return Owner.IsBodyBusyForTraversal(); }

void UVBTraversalComponent::FLadderHost::SendLadderStartRequest(AVBLadder* Ladder, float Height)
{
	RouteByAuthority(Owner.OwnerCharacter.Get(),
		[&] { Owner.MulticastStartLadder(Ladder, Height); },
		[&] { Owner.ServerStartLadder(Ladder, Height); });
}

void UVBTraversalComponent::FLadderHost::SendLadderMoveInput(float InputY)
{
	// 높이는 서버가 계산하므로 소유 클라이언트만 축 입력을 보낸다.
	if (Owner.IsOwningClient())
	{
		Owner.ServerSetLadderMoveInput(InputY);
	}
}

void UVBTraversalComponent::FLadderHost::SendLadderDropRequest()
{
	RouteByAuthority(Owner.OwnerCharacter.Get(),
		[&] { Owner.MulticastLadderDrop(); },
		[&] { Owner.ServerLadderDrop(); });
}

void UVBTraversalComponent::FLadderHost::ConfirmLadderDrop()
{
	// 클라이언트에서 멀티캐스트를 부르면 그 기계에서만 실행되어 상태가 갈리므로 서버가 아니면 무시한다.
	const AVBCharacter* Rider = Owner.OwnerCharacter.Get();
	if (!Rider || !Rider->HasAuthority())
	{
		VB_LOG(Warning, "Ladder: 바닥 내리기 확정은 서버만 한다 - 무시");
		return;
	}

	Owner.MulticastLadderDrop();
}

void UVBTraversalComponent::FLadderHost::ConfirmLadderTopOut(const FVector& LedgePoint, const FVector& LedgeNormal)
{
	const AVBCharacter* Rider = Owner.OwnerCharacter.Get();
	if (!Rider || !Rider->HasAuthority())
	{
		VB_LOG(Warning, "Ladder: 꼭대기 올라서기 확정은 서버만 한다 - 무시");
		return;
	}

	Owner.MulticastLadderTopOut(LedgePoint, LedgeNormal);
}

FVBTopOutResult UVBTraversalComponent::FLadderHost::FindTopOutStand(const FVector& Edge, const FVector& OutwardNormal,
	const FFloatInterval& Band) const
{
	return Owner.FindTopOutStand(Edge, OutwardNormal, Band);
}

void UVBTraversalComponent::FLadderHost::PlayTopOutMontage(const FVector& LedgePoint, const FVector& LedgeNormal, const TCHAR* From)
{
	Owner.PlayTopOutMontage(LedgePoint, LedgeNormal, From);
}

FVBLadderTickTargets UVBTraversalComponent::FLadderHost::GetLadderTickTargets()
{
	FVBLadderTickTargets Targets;
	Targets.Owner = &Owner;
	Targets.IntentTick = &Owner.LadderIntentTick;
	Targets.PlacementTick = &Owner.PrimaryComponentTick;
	if (AVBCharacter* Rider = Owner.OwnerCharacter.Get())
	{
		Targets.Controller = Rider->GetController();
		Targets.Movement = Rider->GetCharacterMovement();
		Targets.Mesh = Rider->GetMesh();
	}

	return Targets;
}

// 자유 등반: 본문은 FVBClimbMode. 여기는 RPC, 공개 진입과 등반, 사다리 공용 꼭대기 찾기다.

void UVBTraversalComponent::SendClimbStart(const FVector& SurfacePoint, const FVector& SurfaceNormal, UPrimitiveComponent* Surface)
{
	RouteByAuthority(OwnerCharacter.Get(),
		[&] { MulticastStartClimb(SurfacePoint, SurfaceNormal, Surface); },
		[&] { ServerStartClimb(SurfacePoint, SurfaceNormal, Surface); });
}

void UVBTraversalComponent::SendClimbDrop()
{
	RouteByAuthority(OwnerCharacter.Get(),
		[&] { MulticastClimbDrop(); },
		[&] { ServerClimbDrop(); });
}

bool UVBTraversalComponent::ServerStartClimb_Validate(FVector SurfacePoint, FVector SurfaceNormal, UPrimitiveComponent* Surface)
{
	// 패킷 위생만(실패 = 연결 종료). 거리 상한은 _Implementation 이 거부로 처리한다.
	return !SurfacePoint.ContainsNaN() && !SurfaceNormal.ContainsNaN() && SurfaceNormal.IsNormalized();
}

void UVBTraversalComponent::ServerStartClimb_Implementation(FVector SurfacePoint, FVector SurfaceNormal, UPrimitiveComponent* Surface)
{
	if (!OwnerCharacter.IsValid())
	{
		return;
	}
	// 클라가 보낸 좌표를 서버가 아는 위치로 검증한다. 매달림 진입과 같은 상한을 쓴다.
	const UVBTraversalConfig* Cfg = GetConfig();
	const float MaxDist = Cfg ? Cfg->MaxPlausibleLedgeDistance : 600.0f;
	const float Dist = static_cast<float>(FVector::Dist(SurfacePoint, OwnerCharacter->GetActorLocation()));
	if (Dist > MaxDist)
	{
		VB_LOG(Warning, "Climb 진입 거부: 면이 %.0fcm 밖이다(상한 %.0f)", Dist, MaxDist);
		return;
	}

	MulticastStartClimb(SurfacePoint, SurfaceNormal, Surface);
}

void UVBTraversalComponent::MulticastStartClimb_Implementation(FVector SurfacePoint, FVector SurfaceNormal, UPrimitiveComponent* Surface)
{
	ClimbMode.Start(*this, Session, SurfacePoint, SurfaceNormal, Surface);
}

void UVBTraversalComponent::SetClimbMoveInput(const FVector2D& Input)
{
	ClimbMode.SetMoveInput(*this, Input);
}

bool UVBTraversalComponent::ServerSetClimbMoveInput_Validate(FVector2D Input)
{
	return !Input.ContainsNaN();
}

void UVBTraversalComponent::ServerSetClimbMoveInput_Implementation(FVector2D Input)
{
	ClimbMode.ApplyMoveInput(Input);
}

void UVBTraversalComponent::RequestClimbDrop()
{
	ClimbMode.RequestDrop(*this);
}

bool UVBTraversalComponent::ServerClimbDrop_Validate() { return true; }
void UVBTraversalComponent::ServerClimbDrop_Implementation() { MulticastClimbDrop(); }
void UVBTraversalComponent::MulticastClimbDrop_Implementation() { ClimbMode.EndByDrop(*this); }

FVBTopOutResult UVBTraversalComponent::FindTopOutStand(const FVector& Edge, const FVector& OutwardNormal, const FFloatInterval& Band) const
{
	FVBTopOutResult Result;
	const float MinZ = Band.Min;
	const float MaxZ = Band.Max;
	const UVBTraversalConfig* Cfg = GetConfig();
	const ACharacter* Ch = OwnerCharacter.Get();
	const UCapsuleComponent* Capsule = Ch ? Ch->GetCapsuleComponent() : nullptr;
	const UCharacterMovementComponent* CMC = Ch ? Ch->GetCharacterMovement() : nullptr;
	if (!Cfg || !Capsule || !CMC || !GetWorld() || !ClimbAnimSet || !ClimbAnimSet->HangClimbUpMontage)
	{
		Result.Refusal = TEXT("재료 없음(설정·캡슐·이동·올라서기 몽타주)");
		return Result;
	}

	const FVector Normal = OutwardNormal.GetSafeNormal2D();
	if (Normal.IsNearlyZero())
	{
		Result.Refusal = TEXT("벽 법선이 수평 성분이 없다");
		return Result;
	}

	const float HalfH = Capsule->GetScaledCapsuleHalfHeight();
	const float Skin = Cfg->LedgeProbeSkin;
	// 서는 자리 = 올라서기 워프 목표와 같은 자리(렛지 안쪽 HangClimbUpStandInset). 거기를 재야 캐릭터가 실제로 갈 곳을 잰다.
	const FVector Stand2D = FVector(Edge.X, Edge.Y, 0.0f) - Normal * ClimbAnimSet->HangClimbUpStandInset;

	// 질의는 캡슐 이동 충돌 그대로. InitSweepCollisionParams 는 지금 벽을 무시 목록에 실어 와 쓰지 않는다.
	const ECollisionChannel Channel = Capsule->GetCollisionObjectType();
	const FCollisionResponseParams Response(Capsule->GetCollisionResponseToChannels());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBClimbTopOut), false, Ch);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), HalfH);

	// 1) 가운데 바닥 - 서는 자리 아래로 선 하나.
	FHitResult Floor;
	if (!GetWorld()->LineTraceSingleByChannel(Floor, FVector(Stand2D.X, Stand2D.Y, MaxZ + Skin), FVector(Stand2D.X, Stand2D.Y, MinZ),
		Channel, Params, Response) || Floor.bStartPenetrating)
	{
		Result.Refusal = FString::Printf(TEXT("윗면 없음 - 안쪽 %.0fcm 아래 바닥이 %.0f~%.0f 에 없다(얇은 벽·바닥 없음·속이 찬 벽)"),
			ClimbAnimSet->HangClimbUpStandInset, MinZ, MaxZ);
		return Result;
	}
	// 2) 서는 캡슐 - 같은 자리에 위에서 내려 앉힌다. 시작부터 겹치면 머리 위가 막힌 것이다.
	FHitResult Room;
	if (!GetWorld()->SweepSingleByChannel(Room, FVector(Stand2D.X, Stand2D.Y, MaxZ + HalfH + Skin),
		FVector(Stand2D.X, Stand2D.Y, MinZ + HalfH),
		FQuat::Identity, Channel, Shape, Params, Response) || Room.bStartPenetrating)
	{
		Result.Refusal = FString::Printf(TEXT("설 공간 없음 - 캡슐이 %s(%s)"), Room.bStartPenetrating ? TEXT("시작부터 겹친다") : TEXT("닿지 않는다"),
			*GetNameSafe(Room.GetComponent()));
		return Result;
	}

	if (!VBTraversalRules::Climb::IsClimbTopOutFloorUsable(static_cast<float>(Floor.ImpactNormal.Z), CMC->GetWalkableFloorZ(),
		static_cast<float>(Floor.ImpactPoint.Z),
		static_cast<float>(Room.ImpactPoint.Z), Band, CMC->MaxStepHeight))
	{
		Result.Refusal = FString::Printf(TEXT("설 바닥 아님 - 법선Z %.2f(걷기 %.2f) 바닥 %.0f 캡슐닿음 %.0f 구간 %.0f~%.0f 턱 %.0f"),
			Floor.ImpactNormal.Z, CMC->GetWalkableFloorZ(), Floor.ImpactPoint.Z, Room.ImpactPoint.Z, MinZ, MaxZ, CMC->MaxStepHeight);
		return Result;
	}
	// 3) 가는 길: 지금 캡슐을 윗면 위로 올린 뒤 서는 자리로. 벽은 무시하지 않는다.
	// 높이는 앉힌 캡슐 기준이라 턱 안의 낮은 것 위를 지나간다.
	const float StandCenterZ = FMath::Max(static_cast<float>(Floor.ImpactPoint.Z) + HalfH, static_cast<float>(Room.Location.Z)) + Skin;
	const FVector Now = Ch->GetActorLocation();
	const FVector Lifted(Now.X, Now.Y, FMath::Max(static_cast<float>(Now.Z), StandCenterZ));
	const FVector StandCenter(Stand2D.X, Stand2D.Y, StandCenterZ);
	FHitResult Path;
	if (GetWorld()->SweepSingleByChannel(Path, Now, Lifted, FQuat::Identity, Channel, Shape, Params, Response))
	{
		Result.Refusal = FString::Printf(TEXT("올라갈 길 막힘 - %s%s"), *GetNameSafe(Path.GetComponent()),
			Path.bStartPenetrating ? TEXT("(시작부터 겹침)") : TEXT(""));
		return Result;
	}

	if (GetWorld()->SweepSingleByChannel(Path, Lifted, StandCenter, FQuat::Identity, Channel, Shape, Params, Response))
	{
		Result.Refusal = FString::Printf(TEXT("넘어갈 길 막힘 - %s"), *GetNameSafe(Path.GetComponent()));
		return Result;
	}

	Result.bFound = true;
	Result.LedgePoint = FVector(Edge.X, Edge.Y, Floor.ImpactPoint.Z);
	return Result;
}

void UVBTraversalComponent::MulticastClimbTopOut_Implementation(FVector LedgePoint, FVector LedgeNormal)
{
	ClimbMode.StartTopOut(*this, LedgePoint, LedgeNormal);
}

void UVBTraversalComponent::PlayTopOutMontage(const FVector& LedgePoint, const FVector& LedgeNormal, const TCHAR* From)
{
	// 재생이 실패하면 아직 허공이라 떨어지기로 복구한다. 걷기는 재생이 걸린 뒤에 정한다.
	Session.SetRecoverMode(MOVE_Falling);
	OwnerCharacter->SetAnimRootMotionTranslationScale(1.0f);

	// 워프는 매달림 올라서기와 같은 Hang 분기다. 워프 창이 전 구간이라 시작 높이가 달라도 끝 자리는 같다.
	FVBTraversalCheckResult R;
	R.ActionType = EVBTraversalActionType::Hang;
	R.bHasFrontLedge = true;
	R.FrontLedgeLocation = LedgePoint;
	R.FrontLedgeNormal = LedgeNormal;
	UpdateWarpTargets(R, ClimbAnimSet->HangClimbUpMontage);

	VB_LOG(Log, "%s 꼭대기 올라서기: montage=%s 렛지=(%.0f,%.0f,%.0f)", From, *ClimbAnimSet->HangClimbUpMontage->GetName(),
		LedgePoint.X, LedgePoint.Y, LedgePoint.Z);
	if (PlayTraversalMontage(ClimbAnimSet->HangClimbUpMontage, 0.0f, 1.0f))
	{
		Session.SetRecoverMode(MOVE_Walking);
	}
}
