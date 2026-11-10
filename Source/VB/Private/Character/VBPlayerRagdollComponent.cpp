// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "Character/VBPlayerRagdollComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Abilities/VBGA_PlayerRagdoll.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBRagdollGetUpTypes.h"
#include "CharacterCommon/VBRagdollComponent.h"
#include "Character/VBCharacter.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Character/VBSlideMovementComponent.h"
#include "Character/VBTargetLockComponent.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Chooser.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBRagdollConfig.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NativeGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "VB/VB.h" // VB_LOG

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_VBPlayerRagdollRecovery, "Event.Locomotion.Ragdoll.GetUp");
DEFINE_LOG_CATEGORY_STATIC(LogVBPlayerRagdoll, Log, All);

UVBPlayerRagdollComponent* UVBPlayerRagdollComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVBPlayerRagdollComponent>() : nullptr;
}

UVBPlayerRagdollComponent::UVBPlayerRagdollComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	// SandboxCharacter_Mover_Ragdoll CDO 의 DefaultRagdollMontageBlendOut 값을 그대로 쓴다.
	EntryMontageBlendOut.Blend.BlendTime = 0.3f;
	EntryMontageBlendOut.Blend.BlendOption = EAlphaBlendOption::HermiteCubic;
	EntryMontageBlendOut.BlendMode = EMontageBlendMode::Inertialization;
}

FGameplayTag UVBPlayerRagdollComponent::GetRecoveryEventTag()
{
	return TAG_VBPlayerRagdollRecovery;
}

void UVBPlayerRagdollComponent::BeginPlay()
{
	Super::BeginPlay();
	Character = Cast<ACharacter>(GetOwner());
	Physics = GetOwner()->FindComponentByClass<UVBRagdollComponent>();
	OnRep_State();
}

UAbilitySystemComponent* UVBPlayerRagdollComponent::GetASC() const
{
	return UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
}

void UVBPlayerRagdollComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBPlayerRagdollComponent, State);
	DOREPLIFETIME_CONDITION(UVBPlayerRagdollComponent, RollInput, COND_SkipOwner);
}

void UVBPlayerRagdollComponent::RequestRagdoll()
{
	if (!Character || !Character->IsLocallyControlled() || IsRagdollActive())
	{
		return;
	}

	if (Character->HasAuthority())
	{
		StartAuthority();
	}
	else
	{
		ServerRequestRagdoll();
	}
}

void UVBPlayerRagdollComponent::ServerRequestRagdoll_Implementation()
{
	StartAuthority();
}

bool UVBPlayerRagdollComponent::StartAuthority()
{
	UAbilitySystemComponent* ASC = GetASC();
	const UVBGASPSmartObjectComponent* SmartObject = GetOwner()->FindComponentByClass<UVBGASPSmartObjectComponent>();
	if (SmartObject && SmartObject->IsInteracting())
	{
		return false;
	}

	if (!Character || !Character->HasAuthority() || !ASC || IsRagdollActive() ||
		UVBAbilitySystemStatics::IsDead(ASC) ||
		ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Finishing) ||
		ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_GettingUp) ||
		(Character->GetCharacterMovement()->MovementMode != MOVE_Walking &&
			Character->GetCharacterMovement()->MovementMode != MOVE_NavWalking &&
			Character->GetCharacterMovement()->MovementMode != MOVE_Falling))
	{
		return false;
	}
	// 회복 ability 가 부여되지 않았으면 플레이어를 physics 상태에 버려두지 않도록 들어가지 않는다.
	if (!ASC->FindAbilitySpecFromClass(UVBGA_PlayerRagdoll::StaticClass()))
	{
		UE_LOG(LogVBPlayerRagdoll, Error, TEXT("%s: missing VBGA_PlayerRagdoll ability"), *Character->GetName());
		return false;
	}

	const FVector EntryVelocity = Character->GetVelocity();
	if (!BeginRagdoll(EntryVelocity))
	{
		return false;
	}

	State.EntryVelocity = EntryVelocity;
	State.Phase = EVBPlayerRagdollPhase::Ragdoll;
	Character->ForceNetUpdate();
	return true;
}

bool UVBPlayerRagdollComponent::BeginRagdoll(const FVector& EntryVelocity)
{
	if (IsRagdollActive())
	{
		return IsRagdolling();
	}

	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Mesh || !Anim || !Movement || !Physics || !Config || !Config->RecoverableGetUpChooser ||
		Mesh->GetPhysicsAsset() != Config->ExpectedPhysicsAsset || !Mesh->GetBodyInstance(TEXT("pelvis")) ||
		!UPoseSearchLibrary::FindPoseHistoryNode(TEXT("PoseHistory"), Anim))
	{
		UE_LOG(LogVBPlayerRagdoll, Error, TEXT("%s: invalid player ragdoll config, mesh, PhysicsControl or PoseHistory"),
			*GetNameSafe(Character));
		return false;
	}

	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	SaveBodyState(*Capsule, *Mesh, *Movement);
	LocalPhase = EVBPlayerRagdollPhase::Ragdoll;
	ClearAimAndLockOn();
	SetRagdollStateTag(true);
	CancelAnimatingAbility();
	Anim->Montage_StopWithBlendSettings(EntryMontageBlendOut);
	StopBodyMovement(*Movement);
	SwitchBodyToPhysics(*Capsule, *Mesh, EntryVelocity);
	if (!Physics->BeginPoweredRagdoll(Mesh, Config))
	{
		Restore();
		return false;
	}

	LocalRollInput = RollInput = FVector::ZeroVector;
	bHasFloor = false;
	LastRollInputSendTime = LastServerRollInputTime = -1.0;
	SetComponentTickEnabled(true);
	UE_LOG(LogVBPlayerRagdoll, Log, TEXT("%s: entered powered ragdoll"), *Character->GetName());
	return true;
}

void UVBPlayerRagdollComponent::SaveBodyState(const UCapsuleComponent& Capsule, const USkeletalMeshComponent& Mesh,
	const UCharacterMovementComponent& Movement)
{
	CapsuleCollision.Capture(Capsule);
	MeshCollision.Capture(Mesh);
	MeshRelativeTransform = Mesh.GetRelativeTransform();
	bMovementTickWasEnabled = Movement.IsComponentTickEnabled();
	bReplicateMovementWasEnabled = Character->IsReplicatingMovement();
	bUseControllerYawWasEnabled = Character->bUseControllerRotationYaw;
	SavedMovementMode = Movement.MovementMode;
	SavedCustomMovementMode = Movement.CustomMovementMode;
	bSavedState = true;
}

void UVBPlayerRagdollComponent::ClearAimAndLockOn()
{
	AVBCharacter* Player = Cast<AVBCharacter>(Character);
	if (!Player || (!Player->HasAuthority() && !Player->IsLocallyControlled()))
	{
		return;
	}

	if (UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(Player))
	{
		Intent->SetWantsToAim(false);
	}

	if (UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(Player))
	{
		Lock->ClearLockOn();
	}
}

void UVBPlayerRagdollComponent::CancelAnimatingAbility()
{
	// montage task 만 멈추지 않고 그것을 가진 ability 도 끝낸다.
	UAbilitySystemComponent* ASC = GetASC();
	if (!ASC || !Character->HasAuthority())
	{
		return;
	}

	if (UGameplayAbility* AnimatingAbility = ASC->GetAnimatingAbility())
	{
		ASC->CancelAbility(AnimatingAbility);
	}
}

void UVBPlayerRagdollComponent::StopBodyMovement(UCharacterMovementComponent& Movement)
{
	Character->StopJumping();
	Character->bUseControllerRotationYaw = false;
	Movement.SetMovementMode(MOVE_None);
	// 회전 방식을 정한 뒤 이동 틱을 멈춘다 - 멈춘 동안 아무도 다시 정하지 않는다(래그돌 중에는 이동 방향 판정이 켜져 있어 둘 다 꺼진다).
	if (UVBSlideMovementComponent* SlideMove = Cast<UVBSlideMovementComponent>(&Movement))
	{
		SlideMove->StopMovementTick();
	}
	else
	{
		// 생성자가 UVBSlideMovementComponent 로 바꾼다 - 블루프린트가 다른 이동 컴포넌트를 쓰면 회전 방식을 정하지 않고 멈춘다.
		VB_LOG(Warning, "%s: 이동 컴포넌트가 UVBSlideMovementComponent 가 아니다 - 회전 방식을 정하지 않고 틱만 멈춘다", *GetNameSafe(Character));
		Movement.SetComponentTickEnabled(false);
	}

	if (Character->HasAuthority())
	{
		Character->SetReplicateMovement(false);
	}
}

void UVBPlayerRagdollComponent::SwitchBodyToPhysics(UCapsuleComponent& Capsule, USkeletalMeshComponent& Mesh, const FVector& EntryVelocity)
{
	VBRagdollBody::ConfigureCapsule(Capsule);
	Mesh.SetCollisionProfileName(TEXT("Ragdoll"));
	Mesh.SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Mesh.SetAllBodiesBelowSimulatePhysics(TEXT("pelvis"), true, true);
	Mesh.bBlendPhysics = true;
	Mesh.WakeAllRigidBodies();
	Mesh.SetAllPhysicsLinearVelocity(EntryVelocity);
	Mesh.AddTickPrerequisiteComponent(this);
	AddTickPrerequisiteActor(Character);
}

void UVBPlayerRagdollComponent::RefreshAbilityState()
{
	if (TaggedASC && TaggedASC != GetASC())
	{
		SetRagdollStateTag(false);
	}

	SetRagdollStateTag(IsRagdollActive());
}

void UVBPlayerRagdollComponent::SetRagdollStateTag(bool bOn)
{
	// 무엇을 막고 취소할지는 전투 규칙 표의 State.Combat.Ragdoll 줄이 정한다.
	if (bOn && !TaggedASC)
	{
		TaggedASC = GetASC();
		if (!TaggedASC)
		{
			return;
		}

		TaggedASC->SetLooseGameplayTagCount(VBGameplayTags::State_Combat_Ragdoll, 1);
	}
	else if (!bOn && TaggedASC)
	{
		TaggedASC->SetLooseGameplayTagCount(VBGameplayTags::State_Combat_Ragdoll, 0);
		TaggedASC = nullptr;
	}
}

void UVBPlayerRagdollComponent::SetRollInput(const FVector& WorldInput)
{
	LocalRollInput = WorldInput.ContainsNaN() ? FVector::ZeroVector : FVector(WorldInput.X, WorldInput.Y, 0.0).GetClampedToMaxSize(1.0);
}

void UVBPlayerRagdollComponent::ServerSetRollInput_Implementation(FVector_NetQuantize10 WorldInput)
{
	if (!IsRagdolling() || WorldInput.ContainsNaN())
	{
		return;
	}

	RollInput = FVector(WorldInput.X, WorldInput.Y, 0.0).GetClampedToMaxSize(1.0);
	LastServerRollInputTime = GetWorld()->GetTimeSeconds();
}

bool UVBPlayerRagdollComponent::SweepGetUpFloor(const UWorld& World, const FVector& Pelvis, float Radius, float HalfHeight,
	const FCollisionQueryParams& Params, FHitResult& OutHit)
{
	// 골반 위 반경만큼에서 시작한다 - 누운 골반은 바닥에서 반경보다 낮아, 골반에서 시작한 구는 바닥 안에서 시작해 걸을 수 없는 판정이 된다.
	const FVector Start = Pelvis + FVector(0.0, 0.0, Radius);
	if (!World.SweepSingleByChannel(OutHit, Start, Pelvis - FVector(0.0, 0.0, HalfHeight * 2.0f), FQuat::Identity, ECC_WorldStatic,
		FCollisionShape::MakeSphere(Radius), Params))
	{
		return false;
	}
	// 그래도 무엇 안에서 시작했으면(낮은 천장, 침대 밑) 바닥을 모른다 - 바닥 있음으로 치지 않는다.
	return !OutHit.bStartPenetrating;
}

bool UVBPlayerRagdollComponent::ProbeFloor(FVector& OutLocation, FVector& OutNormal, bool* bOutStartedInside) const
{
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	OutLocation = Mesh->GetSocketLocation(TEXT("pelvis"));
	OutNormal = FVector::UpVector;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBPlayerRagdollFloor), false, Character);
	Params.AddIgnoredComponent(Mesh);
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const bool bSwept = SweepGetUpFloor(*GetWorld(), OutLocation, Capsule->GetScaledCapsuleRadius(), HalfHeight, Params, Hit);
	if (bOutStartedInside)
	{
		*bOutStartedInside = Hit.bStartPenetrating;
	}

	if (!bSwept)
	{
		return false;
	}

	OutLocation.Z = Hit.ImpactPoint.Z + HalfHeight;
	OutNormal = Hit.ImpactNormal;
	return Character->GetCharacterMovement()->IsWalkable(Hit);
}

void UVBPlayerRagdollComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsRagdolling() || !Character || !Physics || DeltaTime <= 0.0f)
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Character->IsLocallyControlled())
	{
		RollInput = LocalRollInput;
		if (!Character->HasAuthority() && Now - LastRollInputSendTime >= 0.05)
		{
			ServerSetRollInput(LocalRollInput);
			LastRollInputSendTime = Now;
		}
	}
	else if (Character->HasAuthority() && Now - LastServerRollInputTime > 0.25)
	{
		RollInput = FVector::ZeroVector;
	}

	FVector TargetLocation, FloorNormal;
	bHasFloor = ProbeFloor(TargetLocation, FloorNormal);
	Physics->SetEnvironment(FloorNormal, bHasFloor, RollInput);
	const FVector BeforeLocation = Character->GetActorLocation();
	VBRagdollBody::MoveCapsule(*Character, TEXT("pelvis"), TargetLocation - BeforeLocation, Physics->GetTargetOrientation().Rotation(), Now);
	Character->GetCharacterMovement()->Velocity = (Character->GetActorLocation() - BeforeLocation) / DeltaTime;
}

void UVBPlayerRagdollComponent::RequestGetUp()
{
	if (!Character || !Character->IsLocallyControlled() || !IsRagdolling())
	{
		return;
	}

	if (Character->HasAuthority())
	{
		StartGetUpAuthority();
	}
	else
	{
		ServerRequestGetUp();
	}
}

void UVBPlayerRagdollComponent::ServerRequestGetUp_Implementation()
{
	StartGetUpAuthority();
}

void UVBPlayerRagdollComponent::StartGetUpAuthority()
{
	UAbilitySystemComponent* ASC = GetASC();
	if (!Character || !Character->HasAuthority() || !IsRagdolling() || !ASC ||
		UVBAbilitySystemStatics::IsDead(ASC))
	{
		return;
	}
	// 아래 세 거절은 요청 한 번에 한 줄씩 남긴다 - 기록 없이 돌아가면 일어나기가 안 되는 원인 자리를 가를 수 없다.
	FVector Location, Normal;
	bool bStartedInside = false;
	bHasFloor = ProbeFloor(Location, Normal, &bStartedInside);
	if (!bHasFloor)
	{
		UE_LOG(LogVBPlayerRagdoll, Warning, TEXT("%s: get-up refused - no walkable WorldStatic floor under pelvis%s"),
			*Character->GetName(),
			bStartedInside ? TEXT(" (the probe started inside geometry - low ceiling?)") : TEXT(""));
		return;
	}

	UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	const FAnimNode_PoseSearchHistoryCollector_Base* History = Anim
		? UPoseSearchLibrary::FindPoseHistoryNode(TEXT("PoseHistory"), Anim) : nullptr;
	if (!History || !History->GetPoseHistoryPtr())
	{
		UE_LOG(LogVBPlayerRagdoll, Warning, TEXT("%s: get-up refused - %s"), *Character->GetName(),
			!Anim ? TEXT("no anim instance") : !History ? TEXT("no pose history node tagged PoseHistory")
			: TEXT("pose history node has no history"));
		return;
	}
	// 캡슐 프레임이나 physics profile 을 바꾸기 전에 포즈를 잡는다. 서버가 고른 montage, 시작 시각은 gameplay event payload 로 소유자에게 간다.
	VBRagdollBody::SaveGetUpPose(*Anim);
	FVBRagdollGetUpInputs Inputs;
	Inputs.MovementMode = MOVE_Walking;
	Inputs.RollingGetup = Physics->ShouldPlayRollingGetUp();
	Inputs.PoseHistory = History->GetPoseHistoryReference();
	FVBRagdollGetUpOutputs Outputs;
	FChooserEvaluationContext Context(Anim);
	Context.AddStructParam(Inputs);
	Context.AddStructParam(Outputs);
	UAnimMontage* Montage = nullptr;
	UChooserTable::EvaluateChooser(Context, Config->RecoverableGetUpChooser,
		FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&Montage](UObject* Result)
		{
			Montage = Cast<UAnimMontage>(Result);
			return Montage ? FObjectChooserBase::EIteratorStatus::Stop : FObjectChooserBase::EIteratorStatus::Continue;
		}));
	if (!Montage || Montage->GetSkeleton() != Character->GetMesh()->GetSkeletalMeshAsset()->GetSkeleton() ||
		!FMath::IsFinite(Outputs.MontageStartTime) || Outputs.MontageStartTime < 0 || Outputs.MontageStartTime >= Montage->GetPlayLength())
	{
		UE_LOG(LogVBPlayerRagdoll, Error, TEXT("%s: no valid native GASP get-up result; ragdoll remains active"), *Character->GetName());
		return;
	}

	if (!PrepareForGetUp())
	{
		UE_LOG(LogVBPlayerRagdoll, Warning, TEXT("%s: get-up refused - ragdoll physics/anim not ready (PrepareForGetUp)"),
			*Character->GetName());
		return;
	}

	State.Phase = EVBPlayerRagdollPhase::GettingUp;
	Character->ForceNetUpdate();
	FGameplayEventData Event;
	Event.EventTag = GetRecoveryEventTag();
	Event.Target = Character;
	Event.OptionalObject = Montage;
	Event.EventMagnitude = static_cast<float>(Outputs.MontageStartTime);
	if (ASC->HandleGameplayEvent(Event.EventTag, &Event) == 0)
	{
		UE_LOG(LogVBPlayerRagdoll, Error, TEXT("%s: recovery ability activation failed"), *Character->GetName());
		FinishRecovery();
	}
}

bool UVBPlayerRagdollComponent::PrepareForGetUp()
{
	if (LocalPhase == EVBPlayerRagdollPhase::GettingUp)
	{
		return true;
	}

	if (!IsRagdolling() || !Character || !Physics)
	{
		return false;
	}

	UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	if (!Anim)
	{
		return false;
	}

	VBRagdollBody::SaveGetUpPose(*Anim);
	if (!Physics->PrepareForGetUp())
	{
		return false;
	}

	LocalPhase = EVBPlayerRagdollPhase::GettingUp;
	SetComponentTickEnabled(false);
	CapsuleCollision.Apply(*Character->GetCapsuleComponent());
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Walking);
	Movement->SetComponentTickEnabled(bMovementTickWasEnabled);
	// physics 가 포즈를 놓으면 root motion 보정, 복제를 다시 켠다.
	if (Character->HasAuthority())
	{
		Character->SetReplicateMovement(bReplicateMovementWasEnabled);
	}

	return true;
}

void UVBPlayerRagdollComponent::OnRep_State()
{
	if (!Character || !Physics)
	{
		return; // BeginPlay replays initial replication.
	}

	if (State.Phase == EVBPlayerRagdollPhase::Ragdoll)
	{
		BeginRagdoll(State.EntryVelocity);
	}
	else if (State.Phase == EVBPlayerRagdollPhase::GettingUp)
	{
		PrepareForGetUp();
	}
	else
	{
		Restore(LocalPhase == EVBPlayerRagdollPhase::GettingUp);
	}
}

void UVBPlayerRagdollComponent::FinishRecovery()
{
	if (!Character || !Character->HasAuthority() || !IsRagdollActive())
	{
		return;
	}

	State.Phase = EVBPlayerRagdollPhase::None;
	Restore(true);
	Character->ForceNetUpdate();
}

void UVBPlayerRagdollComponent::CancelRagdoll()
{
	if (Character && Character->HasAuthority())
	{
		State.Phase = EVBPlayerRagdollPhase::None;
		Character->ForceNetUpdate();
	}

	Restore();
	// 세이브 복원, 죽음 경로가 가져가기 전에 회복 montage 를 놓는다. Restore 가 먼저라 GA 의 끝 콜백이 physics 정리에 다시 들어오지 않는다.
	if (Character && Character->HasAuthority())
	{
		if (UAbilitySystemComponent* ASC = GetASC())
		{
			if (FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(UVBGA_PlayerRagdoll::StaticClass()))
			{
				if (Spec->IsActive())
				{
					ASC->CancelAbilityHandle(Spec->Handle);
				}
			}
		}
	}
}

void UVBPlayerRagdollComponent::Restore(bool bCompletedRecovery)
{
	if (!bSavedState)
	{
		return;
	}

	const bool bWasGettingUp = LocalPhase == EVBPlayerRagdollPhase::GettingUp;
	UAbilitySystemComponent* ASC = GetASC();
	const bool bKeepPhysicalAnimation = bCompletedRecovery && bWasGettingUp && Physics && Config &&
		(!ASC || !UVBAbilitySystemStatics::IsDead(ASC)) &&
		Config->DefaultPhysicsProfile == EVBRagdollPhysicsProfile::PhysicalAnimation && Physics->CompleteGetUpProfile();
	LocalPhase = EVBPlayerRagdollPhase::None;
	SetComponentTickEnabled(false);
	if (Physics && !bKeepPhysicalAnimation)
	{
		Physics->EndPoweredRagdoll();
	}

	if (Character)
	{
		USkeletalMeshComponent* Mesh = Character->GetMesh();
		Mesh->RemoveTickPrerequisiteComponent(this);
		RemoveTickPrerequisiteActor(Character);
		if (!bKeepPhysicalAnimation)
		{
			Mesh->SetAllBodiesSimulatePhysics(false);
			Mesh->bBlendPhysics = false;
		}

		Mesh->SetRelativeTransform(MeshRelativeTransform, false, nullptr, ETeleportType::TeleportPhysics);
		MeshCollision.Apply(*Mesh);
		CapsuleCollision.Apply(*Character->GetCapsuleComponent());
		Character->bUseControllerRotationYaw = bUseControllerYawWasEnabled;
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		Movement->SetComponentTickEnabled(bMovementTickWasEnabled);
		Movement->SetMovementMode(
			bWasGettingUp ? (bHasFloor ? MOVE_Walking : MOVE_Falling) : static_cast<EMovementMode>(SavedMovementMode),
			SavedCustomMovementMode);
		if (Character->HasAuthority())
		{
			Character->SetReplicateMovement(bReplicateMovementWasEnabled);
		}
		// 회전 방식은 다시 켜진 이동 틱이 지금 의도로 정한다 - 래그돌 전 값을 되돌리지 않는다(그 사이 질주, 락온이 끝났을 수 있다).
	}

	LocalRollInput = RollInput = FVector::ZeroVector;
	bSavedState = false;
	SetRagdollStateTag(false);
}

void UVBPlayerRagdollComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Restore();
	Super::EndPlay(EndPlayReason);
}
