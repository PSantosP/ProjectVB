// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Enemy/VBEnemyRagdollComponent.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/AnimInstance.h"
#include "Character/VBMeleeTraceComponent.h" // VBCollision::MeleeTrace
#include "CharacterCommon/VBHitPhysicsComponent.h"
#include "CharacterCommon/VBRagdollComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBEnemyConfig.h"
#include "Data/VBRagdollConfig.h"
#include "Enemy/VBEnemyBase.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "TimerManager.h"
#include "VB/VB.h"

UVBEnemyRagdollComponent::UVBEnemyRagdollComponent()
{
	SetIsReplicatedByDefault(true);
	// 따라가기 동안만 켠다. 다음 메시 포즈 평가 전에 마지막 물리 포즈를 따라간다 - EndPhysics 뒤에 캡슐을 옮기면 뼈가 튄다
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

UVBEnemyRagdollComponent* UVBEnemyRagdollComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVBEnemyRagdollComponent>() : nullptr;
}

void UVBEnemyRagdollComponent::OnRegister()
{
	Super::OnRegister();
	// multicast 와 OnRep 가 BeginPlay 보다 먼저 올 수 있어 형제 컴포넌트는 등록 때 찾는다.
	Body = Cast<AVBEnemyBase>(GetOwner());
	PhysicsControl = GetOwner() ? GetOwner()->FindComponentByClass<UVBRagdollComponent>() : nullptr;
	HitPhysics = GetOwner() ? GetOwner()->FindComponentByClass<UVBHitPhysicsComponent>() : nullptr;
}

void UVBEnemyRagdollComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!Body || !PhysicsControl || !HitPhysics)
	{
		VB_LOG(Error, "%s: 적 래그돌 조립이 빠졌다 (몸 %d, 물리 제어 %d, 피격 물리 %d) - 쓰러짐과 시체 물리가 돌지 않는다",
			*GetNameSafe(GetOwner()), Body != nullptr, PhysicsControl != nullptr, HitPhysics != nullptr);
	}

	if (GetOwner() && GetOwner()->HasAuthority() && !GetIsReplicated())
	{
		VB_LOG(Error, "%s: 적 래그돌 컴포넌트가 복제되지 않는다 - 원격 클라에서 쓰러짐이 보이지 않는다", *GetNameSafe(GetOwner()));
	}
}

void UVBEnemyRagdollComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UVBEnemyRagdollComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBEnemyRagdollComponent, ReplicatedRecoverable);
}

const UVBEnemyConfig* UVBEnemyRagdollComponent::GetConfig() const
{
	return Body ? Body->GetEnemyConfig() : nullptr;
}

bool UVBEnemyRagdollComponent::IsBodyDead() const
{
	return Body && Body->IsDead();
}

void UVBEnemyRagdollComponent::MarkBodyReady()
{
	bBodyReady = true;
	if (ReplicatedRecoverable.Phase != EVBRecoverableRagdollPhase::None)
	{
		OnRep_RecoverableState();
	}
}

void UVBEnemyRagdollComponent::OnRep_RecoverableState()
{
	if (!bBodyReady || IsBodyDead())
	{
		return;
	}

	const FVBEnemyRecoverableRagdollState& State = ReplicatedRecoverable;
	if (State.Phase == EVBRecoverableRagdollPhase::None)
	{
		ResetRecoverable();
		return;
	}

	if (!Recoverable.bActive)
	{
		// RepNotify 가 multicast 보다 먼저 올 수 있다 - 서버 시작 근처에서만 첫 충격을 쓰고, 늦은 클라는 복제된 캡슐에서 시작한다
		const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
		const double Age = GameState ? GameState->GetServerWorldTimeSeconds() - State.ServerStartTime : 1.0;
		const bool bRecent = State.Phase == EVBRecoverableRagdollPhase::Ragdoll && GameState &&
			Age >= -0.05 && Age < 0.25;
		ApplyRecoverable(bRecent ? FVector(State.LaunchVelocity) : FVector::ZeroVector);
	}

	if (State.Phase == EVBRecoverableRagdollPhase::GettingUp && Follow.bActive)
	{
		ApplyGetUp();
	}
}

bool UVBEnemyRagdollComponent::StartRecoverable(const FVector& LaunchVelocity)
{
	const UVBEnemyConfig* Config = GetConfig();
	if (!GetOwner() || !GetOwner()->HasAuthority() || IsBodyDead() || bEnteredCorpse || Recoverable.bActive ||
		!Config || !Config->bUseDrivenRagdoll || !Config->PoweredRagdoll || !PhysicsControl)
	{
		return false;
	}

	if (!Config->PoweredRagdoll->RecoverableGetUpChooser)
	{
		return false;
	}

	ReplicatedRecoverable.Phase = EVBRecoverableRagdollPhase::Ragdoll;
	ReplicatedRecoverable.LaunchVelocity = LaunchVelocity;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	ReplicatedRecoverable.ServerStartTime = GameState
		? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	GetOwner()->ForceNetUpdate();
	MulticastStartRecoverable(LaunchVelocity);
	if (!Recoverable.bActive)
	{
		ReplicatedRecoverable.Phase = EVBRecoverableRagdollPhase::None;
		GetOwner()->ForceNetUpdate();
	}
	// 쓰러진 동안과 기상 동안은 '쓰러진 상태' 다: 피격 반응, 넘어짐, 경직이 몽타주를 얹지 않고, 허브는 넉백 대신
	//   충격으로 돌리고, 처형 창은 일어날 때까지 기다린다. 떼는 곳 = ResetRecoverable(기상 끝, 실패). 쓰러진 채 죽으면 남긴다(시체다).
	UAbilitySystemComponent* ASC = Body->GetAbilitySystemComponent();
	if (Recoverable.bActive && ASC && !Recoverable.bDownedTagGranted)
	{
		ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_Downed, 1, EGameplayTagReplicationState::CountToOwner);
		Recoverable.bDownedTagGranted = true;
	}

	return Recoverable.bActive;
}

void UVBEnemyRagdollComponent::MulticastStartRecoverable_Implementation(const FVector& LaunchVelocity)
{
	ApplyRecoverable(LaunchVelocity);
}

void UVBEnemyRagdollComponent::ApplyRecoverable(const FVector& LaunchVelocity)
{
	USkeletalMeshComponent* MeshComp = Body ? Body->GetMesh() : nullptr;
	UCapsuleComponent* Capsule = Body ? Body->GetCapsuleComponent() : nullptr;
	if (Recoverable.bActive || bEnteredCorpse || !MeshComp || !Capsule || !HitPhysics)
	{
		return;
	}

	HitPhysics->StopAll();
	Recoverable.CapsuleCollision.Capture(*Capsule);
	Recoverable.MeshCollision.Capture(*MeshComp);
	Recoverable.bActive = true;
	// 무엇을 막고 취소할지는 전투 규칙 표의 State.Combat.Ragdoll 줄이 정한다.
	if (UAbilitySystemComponent* ASC = Body->GetAbilitySystemComponent())
	{
		ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Combat_Ragdoll, 1);
	}

	if (UCharacterMovementComponent* Movement = Body->GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_None);
	}

	MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
	MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	// 쓰러진 캐릭터도 칼에 맞는다. Ragdoll 프로필은 판정 채널에 응답이 없어 전역 프로필을 고치지 않고
	//   이 메시에만 켠다(시체, 플레이어 래그돌이 같은 프로필이다). 프로필을 건 뒤여야 남는다. 되돌리기는 ResetRecoverable 의 응답 복원이 한다.
	MeshComp->SetCollisionResponseToChannel(VBCollision::MeleeTrace, ECR_Overlap);
	const FName RootBone = GetConfig()->RagdollRootBone;
	if (!RootBone.IsNone() && MeshComp->GetBoneIndex(RootBone) != INDEX_NONE)
	{
		MeshComp->SetAllBodiesBelowSimulatePhysics(RootBone, true, true);
		MeshComp->bBlendPhysics = true;
	}
	else
	{
		MeshComp->SetAllBodiesSimulatePhysics(true);
		// Character mesh 를 붙여 둬 저작한 상대 offset 이 회복 뒤에도 남게 한다
		MeshComp->bBlendPhysics = true;
	}

	MeshComp->WakeAllRigidBodies();
	if (!LaunchVelocity.IsNearlyZero())
	{
		MeshComp->SetAllPhysicsLinearVelocity(LaunchVelocity);
	}

	BeginFollow(LaunchVelocity);
	if (!Follow.bActive)
	{
		VB_LOG(Error, "%s: recoverable ragdoll could not start; restored animation", *GetNameSafe(GetOwner()));
		if (GetOwner()->HasAuthority())
		{
			FinishGetUp();
		}
		else
		{
			ResetRecoverable();
		}
	}
}

void UVBEnemyRagdollComponent::MulticastStartGetUp_Implementation()
{
	ApplyGetUp();
}

void UVBEnemyRagdollComponent::ApplyGetUp()
{
	if (!Recoverable.bActive || !Follow.bActive || !PhysicsControl)
	{
		return;
	}

	UAnimInstance* Anim = Body->GetMesh() ? Body->GetMesh()->GetAnimInstance() : nullptr;
	const FAnimNode_PoseSearchHistoryCollector_Base* History = Anim
		? UPoseSearchLibrary::FindPoseHistoryNode(TEXT("PoseHistory"), Anim) : nullptr;
	if (!History || !History->GetPoseHistoryPtr())
	{
		VB_LOG(Error, "GASPGetUp: %s has no active PoseHistory", *GetNameSafe(GetOwner()));
		if (GetOwner()->HasAuthority())
		{
			FinishGetUp();
		}

		return;
	}
	// pose 생산자를 끄거나 physics 를 바꾸기 전에 물리 mesh 를 저장한다
	VBRagdollBody::SaveGetUpPose(*Anim);
	if (!PhysicsControl->PrepareForGetUp())
	{
		VB_LOG(Error, "GASPGetUp: %s could not prepare physics handoff", *GetNameSafe(GetOwner()));
		if (GetOwner()->HasAuthority())
		{
			FinishGetUp();
		}

		return;
	}

	Follow.bActive = false;
	SetComponentTickEnabled(false);
	Recoverable.CapsuleCollision.Apply(*Body->GetCapsuleComponent());
	if (UCharacterMovementComponent* Movement = Body->GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
	}

	UAbilitySystemComponent* ASC = Body->GetAbilitySystemComponent();
	if (GetOwner()->HasAuthority() && ASC)
	{
		FGameplayEventData Payload;
		Payload.EventTag = VBGameplayTags::Event_Combat_FinisherSurvived;
		Payload.Target = GetOwner();
		Payload.OptionalObject = GetConfig()->PoweredRagdoll;
		if (ASC->HandleGameplayEvent(Payload.EventTag, &Payload) == 0)
		{
			VB_LOG(Error, "%s: recoverable ragdoll settled but GA_GetUp did not activate", *GetNameSafe(GetOwner()));
			FinishGetUp();
		}
	}
}

void UVBEnemyRagdollComponent::FinishGetUp()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Recoverable.bActive)
	{
		return;
	}

	ReplicatedRecoverable.Phase = EVBRecoverableRagdollPhase::None;
	GetOwner()->ForceNetUpdate();
	MulticastFinishGetUp();
}

void UVBEnemyRagdollComponent::MulticastFinishGetUp_Implementation()
{
	ResetRecoverable();
}

void UVBEnemyRagdollComponent::ResetRecoverable()
{
	if (!Recoverable.bActive)
	{
		return;
	}

	const UVBEnemyConfig* Config = GetConfig();
	const bool bKeepPhysicalAnimation = !IsBodyDead() && !Follow.bActive && PhysicsControl && Config &&
		Config->PoweredRagdoll &&
		Config->PoweredRagdoll->DefaultPhysicsProfile == EVBRagdollPhysicsProfile::PhysicalAnimation &&
		PhysicsControl->CompleteGetUpProfile();
	Follow.bActive = false;
	if (PhysicsControl && !bKeepPhysicalAnimation)
	{
		PhysicsControl->EndPoweredRagdoll();
	}

	if (USkeletalMeshComponent* MeshComp = Body->GetMesh())
	{
		MeshComp->RemoveTickPrerequisiteComponent(this);
		if (!bKeepPhysicalAnimation)
		{
			MeshComp->SetAllBodiesSimulatePhysics(false);
			MeshComp->bBlendPhysics = false;
		}

		Recoverable.MeshCollision.Apply(*MeshComp);
	}

	if (UCapsuleComponent* Capsule = Body->GetCapsuleComponent())
	{
		Recoverable.CapsuleCollision.Apply(*Capsule);
	}

	if (UCharacterMovementComponent* Movement = Body->GetCharacterMovement())
	{
		Movement->SetMovementMode(Follow.bHasFloor ? MOVE_Walking : MOVE_Falling);
	}

	Recoverable.bActive = false;
	if (UAbilitySystemComponent* ASC = Body->GetAbilitySystemComponent())
	{
		ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Combat_Ragdoll, 0);
		if (GetOwner()->HasAuthority() && Recoverable.bDownedTagGranted)
		{
			ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Downed, 1,
				EGameplayTagReplicationState::CountToOwner);
			Recoverable.bDownedTagGranted = false;
		}
	}

	SetComponentTickEnabled(false);
}

bool UVBEnemyRagdollComponent::IsDownedInPhysics() const
{
	// 기상 중(GettingUp)은 아니다 - 메시 제어를 애니메이션으로 되돌리는 중이라(ApplyGetUp 의 자세 스냅샷) 충격을 주면 되돌린 자세가 튄다.
	return Recoverable.bActive && Follow.bActive && !IsBodyDead();
}

float UVBEnemyRagdollComponent::ReceiveDownedImpact(const FHitResult* Hit, const FVector& PushDir, EVBHitImpact Impact, float KnockbackScale)
{
	const UVBEnemyConfig* Config = GetConfig();
	const UVBRagdollConfig* Ragdoll = Config ? Config->PoweredRagdoll.Get() : nullptr;
	USkeletalMeshComponent* MeshComp = Body ? Body->GetMesh() : nullptr;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsDownedInPhysics() || !Ragdoll || !MeshComp || !GetWorld())
	{
		return 0.0f;
	}
	// 상한: 이 시간이 지나면 밀지 않는다 - 물리 바디가 멈춰 다음 기상 판정에서 일어난다(설정 주석).
	const double DownedFor = GetWorld()->GetTimeSeconds() - Follow.StartTime;
	if (DownedFor > Ragdoll->DownedHitWindowSeconds)
	{
		VB_LOG(Log, "쓰러진 몸 맞음[%s]: 충격 없음(창 지남 - 쓰러진 지 %.1f초, 창 %.1f초)",
			*GetNameSafe(GetOwner()), DownedFor, Ragdoll->DownedHitWindowSeconds);
		return 0.0f;
	}

	const float* Horizontal = Ragdoll->DownedHitVelocity.Find(Impact);
	const float Scale = FMath::IsFinite(KnockbackScale) ? FMath::Max(0.0f, KnockbackScale) : 1.0f;
	FVector Velocity = PushDir.GetSafeNormal2D() * (Horizontal ? FMath::Max(0.0f, *Horizontal) : 0.0f) * Scale;
	if (Velocity.IsNearlyZero())
	{
		VB_LOG(Log, "쓰러진 몸 맞음[%s]: 충격 없음(등급 %s 의 값이 0 이거나 방향 없음)", *GetNameSafe(GetOwner()), VBHitImpactName(Impact));
		return 0.0f;
	}

	Velocity.Z += Ragdoll->DownedHitUpVelocity;

	// 맞은 자리: 판정이 준 뼈가 시뮬레이션 중인 바디면 그 접점, 아니면(구 스윕, 비근접 피해, 키네마틱 뿌리 바디) 쫓는 뼈(골반).
	FName Bone = Hit ? Hit->BoneName : NAME_None;
	FVector Point = Hit ? FVector(Hit->ImpactPoint) : FVector::ZeroVector;
	// 시뮬레이션 여부는 메시에 묻는다 - FBodyInstance::IsInstanceSimulatingPhysics 는 PhysicsCore 의 함수를 불러 이 모듈에서 링크가 안 된다.
	if (Bone.IsNone() || !MeshComp->GetBodyInstance(Bone) || !MeshComp->IsSimulatingPhysics(Bone) || Point.IsZero())
	{
		Bone = Follow.Bone;
		Point = MeshComp->GetSocketLocation(Bone);
	}

	VB_LOG(Log, "쓰러진 몸 맞음[%s]: 등급 %s 뼈 %s 속도 %.0f cm/s / 쓰러진 지 %.1f초(창 %.1f초)",
		*GetNameSafe(GetOwner()), VBHitImpactName(Impact), *Bone.ToString(), Velocity.Size(), DownedFor, Ragdoll->DownedHitWindowSeconds);
	MulticastDownedImpact(Point, Bone, Velocity);
	return Velocity.Size();
}

void UVBEnemyRagdollComponent::MulticastDownedImpact_Implementation(FVector_NetQuantize Point, FName Bone, FVector_NetQuantize Velocity)
{
	USkeletalMeshComponent* MeshComp = Body ? Body->GetMesh() : nullptr;
	if (!Recoverable.bActive || !Follow.bActive || !MeshComp)
	{
		return;
	}
	// 질량과 무관한 속도 충격 - 설정 값이 곧 그 뼈가 받는 속도다. 구속으로 이어진 나머지 바디는 따라 끌린다.
	MeshComp->AddVelocityChangeImpulseAtLocation(Velocity, Point, Bone);
}

void UVBEnemyRagdollComponent::StopBodyForDeath()
{
	if (!Body)
	{
		return;
	}

	Body->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (UCharacterMovementComponent* Move = Body->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}
}

bool UVBEnemyRagdollComponent::EnterCorpse(const FVBCorpseLaunch& Launch, double ElapsedSinceDeath)
{
	if (!Body || !HitPhysics)
	{
		return false;
	}

	HitPhysics->StopAll();
	const bool bKeepDownedBody = ReleaseRecoverableForDeath();
	if (bEnteredCorpse)
	{
		return false;
	}

	bEnteredCorpse = true;
	CorpseLaunch = Launch;
	StopBodyForDeath();
	if (bKeepDownedBody)
	{
		HandDownedBodyToDeath(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
		return true;
	}

	SwitchMeshToRagdoll();
	ApplyCorpseEntryVelocity();
	VB_LOG(Log, "래그돌 진입: %s (경과 %.2f초, 운동량 %s)",
		*GetNameSafe(GetOwner()), ElapsedSinceDeath,
		ShouldPreserveCorpseMomentum() ? TEXT("남긴다") : TEXT("지운다"));

	// 지우기를 다음 틱에 한 번 더 한다. 켠 프레임에 읽은 속도는 언제나 0 이라 이 자리의 지우기만으로는
	//   '지웠다' 를 증명하지도, 실제로 막지도 못한다(진입 시점 0 인데 시체가 튄다).
	FTimerManager& Timers = GetWorld()->GetTimerManager();
	Timers.SetTimerForNextTick(this, &ThisClass::SettleCorpseVelocity);
	FTimerHandle SettleReportHandle;
	Timers.SetTimer(SettleReportHandle, this, &ThisClass::SettleCorpseVelocity, 0.3f, false);

	// 첫 틱이 쫓아갈 목표가 이미 움직이고 있어야 첫 프레임에 캡슐이 뒤처지지 않는다 - 시뮬레이션과 첫 속도 뒤에 시작한다.
	BeginFollow(CorpseLaunch.Velocity);
	return true;
}

bool UVBEnemyRagdollComponent::ReleaseRecoverableForDeath()
{
	// 모든 net role 에서 죽음이 살아남은 피해자 ragdoll 보다 우선한다.
	//   이미 물리로 쓰러져 있으면 다시 만들지 않고 복구 플래그만 내린다. 기상 중이면 Reset 하고 새로 쓰러뜨린다.
	const bool bKeepDownedBody = Recoverable.bActive && Follow.bActive && !bEnteredCorpse;
	if (bKeepDownedBody)
	{
		Recoverable.bActive = false;
	}
	else if (Recoverable.bActive)
	{
		ResetRecoverable();
	}

	if (GetOwner()->HasAuthority() && ReplicatedRecoverable.Phase != EVBRecoverableRagdollPhase::None)
	{
		ReplicatedRecoverable.Phase = EVBRecoverableRagdollPhase::None;
		GetOwner()->ForceNetUpdate();
	}

	return bKeepDownedBody;
}

void UVBEnemyRagdollComponent::HandDownedBodyToDeath(double Now)
{
	// 메시는 이미 Ragdoll 프로필로 시뮬레이션 중이다. 칼 판정 응답만 끈다 - 시체는 안 맞는다(ApplyRecoverable 이 켰던 것).
	//   캡슐은 StopBodyForDeath 가 꺼 뒀으니 따라가기의 값으로 되돌린다. 따라가기는 계속 돌고, 멎음과 상한은 죽은 때부터 다시 잰다.
	Body->GetMesh()->SetCollisionResponseToChannel(VBCollision::MeleeTrace, ECR_Ignore);
	if (UCapsuleComponent* Capsule = Body->GetCapsuleComponent())
	{
		VBRagdollBody::ConfigureCapsule(*Capsule);
	}

	Follow.StartTime = Now;
	Follow.SettledFrames = 0;
	VB_LOG(Log, "래그돌 진입: %s 쓰러진 채 사망 - 누운 몸을 그대로 시체로 넘긴다", *GetNameSafe(GetOwner()));
}

void UVBEnemyRagdollComponent::SwitchMeshToRagdoll()
{
	USkeletalMeshComponent* MeshComp = Body->GetMesh();
	MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
	// 래그돌 메시가 카메라 프로브 채널만 통과하게 한다(시체에 스프링암이 막혀 급줌인). 프로파일 설정 뒤에 둔다 - 그것이 채널을 리셋한다.
	MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	// 시뮬레이션할 바디: RagdollRootBone 이 있으면 그 뼈 아래만. SetSimulatePhysics 는 뿌리 바디를 되살리므로 그 길에서는 부르지 않는다.
	const FName RagdollRoot = GetConfig() ? GetConfig()->RagdollRootBone : NAME_None;
	if (!RagdollRoot.IsNone() && MeshComp->GetBoneIndex(RagdollRoot) != INDEX_NONE)
	{
		MeshComp->SetAllBodiesBelowSimulatePhysics(RagdollRoot, true, /*bIncludeSelf*/ true);
		MeshComp->bBlendPhysics = true;

		// 뿌리 뼈를 잔여 키네마틱 바디에서 떼어낸다 - 구속이 남으면 골반이 서 있는 높이로 끌려 올라가 공중에 걸린다.
		MeshComp->BreakConstraint(FVector::ZeroVector, FVector::ZeroVector, RagdollRoot);

		// 잔여 키네마틱 바디의 충돌은 건드리지 않는다 - 바꾸면 엔진이 바디 전부를 다시 만들어 래그돌이 없던 일이 된다(순회 중 배열도 바뀐다).
		int32 RagdollBodyCount = 0;
		MeshComp->ForEachBodyBelow(RagdollRoot, /*bIncludeSelf*/ true, /*bSkipCustomType*/ false,
			[&RagdollBodyCount](FBodyInstance*) { ++RagdollBodyCount; });
		VB_LOG(Log, "래그돌 범위: %s '%s' 아래 %d 바디 시뮬레이션, 그 밖 %d 바디는 키네마틱(충돌 유지)",
			*GetNameSafe(GetOwner()), *RagdollRoot.ToString(), RagdollBodyCount, MeshComp->Bodies.Num() - RagdollBodyCount);
	}
	else
	{
		if (!RagdollRoot.IsNone())
		{
			VB_LOG(Warning, "%s: RagdollRootBone '%s' 가 골격에 없다 - 바디 전부에 건다. 설정 자산의 뼈 이름을 확인하라.",
				*GetNameSafe(GetOwner()), *RagdollRoot.ToString());
		}

		MeshComp->SetAllBodiesSimulatePhysics(true);
		MeshComp->SetSimulatePhysics(true);
	}

	MeshComp->WakeAllRigidBodies();
}

void UVBEnemyRagdollComponent::ApplyCorpseEntryVelocity()
{
	USkeletalMeshComponent* MeshComp = Body->GetMesh();
	// 던져진 래그돌이면 클립이 정한 속도로 설정한다 - 시뮬레이션을 켜는 호출 뒤여야 남는다. 그 순간 CMC 속도도 찍는다.
	if (CorpseLaunch.bKeepMomentum)
	{
		const FVector CmcVelocity =
			Body->GetCharacterMovement() ? Body->GetCharacterMovement()->Velocity : FVector::ZeroVector;
		if (!CorpseLaunch.Velocity.IsNearlyZero())
		{
			MeshComp->SetAllPhysicsLinearVelocity(CorpseLaunch.Velocity);
		}

		VB_LOG(Log, "래그돌 발사: %s 클립 속도 %.0f cm/s %s (그 순간 CMC 속도 %.0f cm/s)",
			*GetNameSafe(GetOwner()), CorpseLaunch.Velocity.Size(),
			*CorpseLaunch.Velocity.ToCompactString(), CmcVelocity.Size());
	}

	// 속도 지우기는 켜는 호출을 전부 끝낸 뒤 - 켜는 호출마다 직전 애니 운동을 다시 심는다.
	//   지우는 이유: 사망 클립 끝은 이미 바닥이라 남은 운동량은 튀는 힘이다.
	if (!ShouldPreserveCorpseMomentum())
	{
		MeshComp->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
		MeshComp->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	}
}

bool UVBEnemyRagdollComponent::ShouldPreserveCorpseMomentum() const
{
	const UVBEnemyConfig* Config = GetConfig();
	return !CorpseLaunch.bSuppressLaunch && (CorpseLaunch.bKeepMomentum || (Config && Config->bPreserveRagdollMomentum));
}

void UVBEnemyRagdollComponent::SettleCorpseVelocity()
{
	USkeletalMeshComponent* MeshComp = Body ? Body->GetMesh() : nullptr;
	if (!MeshComp)
	{
		return;
	}
	// 최대값을 보는 이유: GetPhysicsLinearVelocity 는 루트 바디(보통 골반) 하나만 답한다. 골반이 멈춰 있어도
	//   팔다리가 튀면 시체는 날아간다 - 어느 뼈가 튀는지가 곧 원인의 방향이다.
	const FVBFastestBody Fastest = VBRagdollBody::FindFastestBody(*MeshComp);
	// 운동량을 남기는 래그돌은 재기만 한다(던져진 것은 그 운동량이 나머지 비행이다). 계측은 남긴다.
	if (ShouldPreserveCorpseMomentum())
	{
		VB_LOG(Log, "래그돌 안정화: %s 최대 바디 속도 %.0f cm/s (%s) - %s 라 지우지 않는다",
			*GetNameSafe(GetOwner()), Fastest.Speed, *Fastest.Bone.ToString(),
			CorpseLaunch.bKeepMomentum ? TEXT("던져진 래그돌") : TEXT("운동량 보존 설정"));
		return;
	}

	VB_LOG(Log, "래그돌 안정화: %s 최대 바디 속도 %.0f cm/s (%s) -> 0",
		*GetNameSafe(GetOwner()), Fastest.Speed, *Fastest.Bone.ToString());
	MeshComp->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
	MeshComp->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
}

void UVBEnemyRagdollComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Follow.bActive)
	{
		TickFollow(DeltaTime);
	}
}

// 캡슐 따라가기: 골반을 목표로 캡슐을 스윕해 옮기고 그 이동량을 속도로 심는다.
// 메시는 물리가 움직인다. 캡슐을 옮겨도 메시는 따로 옮기지 않는다(둘이 옮기면 두 배로 간다).
void UVBEnemyRagdollComponent::BeginFollow(const FVector& EntryVelocity)
{
	if (HitPhysics)
	{
		HitPhysics->StopAll();
	}

	const UVBEnemyConfig* Config = GetConfig();
	if (!Config || !Config->bUseDrivenRagdoll)
	{
		return;
	}

	UCapsuleComponent* Capsule = Body->GetCapsuleComponent();
	USkeletalMeshComponent* MeshComp = Body->GetMesh();
	if (!Capsule || !MeshComp)
	{
		return;
	}

	Follow.Bone = VBRagdollBody::ResolveFollowBone(*MeshComp, Config->DrivenRagdollFollowBone, Config->RagdollRootBone);
	if (Follow.Bone.IsNone())
	{
		// 쫓아갈 뼈가 없으면 따라가기를 포기한다. 조용히 포기하지 않는 이유: 이 경우 시체가 종전처럼
		//   죽은 자리에 캡슐을 남기는데, 화면에서는 「래그돌이 GASP 같지 않다」로만 보인다.
		VB_LOG(Warning, "몰아지는 래그돌: %s 쫓아갈 뼈를 못 찾아 건너뛴다 (설정 '%s' / 뿌리 '%s' / pelvis 모두 골격에 없음)",
			*GetNameSafe(GetOwner()), *Config->DrivenRagdollFollowBone.ToString(), *Config->RagdollRootBone.ToString());
		return;
	}

	VBRagdollBody::ConfigureCapsule(*Capsule);
	MeshComp->AddTickPrerequisiteComponent(this);
	Follow.bActive = true;
	Follow.StartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Follow.StartLocation = Capsule->GetComponentLocation();
	Follow.SettledFrames = 0;
	Follow.NextGetUpCheckTime = Follow.StartTime +
		(Config->PoweredRagdoll ? Config->PoweredRagdoll->NPCGetUpCheckSeconds : 3.0f);
	Follow.bHasFloor = false;
	if (PhysicsControl && Config->PoweredRagdoll)
	{
		if (!PhysicsControl->BeginPoweredRagdoll(MeshComp, Config->PoweredRagdoll))
		{
			VB_LOG(Warning, "%s: powered 래그돌 초기화 실패 (설정 '%s') - 물리 제어와 캡슐 추종을 종료하고 passive 래그돌을 유지한다",
				*GetNameSafe(GetOwner()), *GetNameSafe(Config->PoweredRagdoll));
			EndFollow(TEXT("물리 제어 초기화 실패"));
			return;
		}
	}

	SetComponentTickEnabled(true);
	VB_LOG(Log, "RagdollFollowOrder: %s componentTick=%d meshTick=%d followBeforeMesh=1 meshTeleportGuard=1",
		*GetNameSafe(GetOwner()), int32(PrimaryComponentTick.TickGroup), int32(MeshComp->PrimaryComponentTick.TickGroup));

	VB_LOG(Log, "몰아지는 래그돌 A 시작: %s '%s' 를 쫓는다 (초기 속도 %.0f cm/s, 상한 %.1f초)",
		*GetNameSafe(GetOwner()), *Follow.Bone.ToString(), EntryVelocity.Size(), Config->DrivenRagdollMaxSeconds);
}

void UVBEnemyRagdollComponent::TickFollow(float DeltaTime)
{
	const UVBEnemyConfig* Config = GetConfig();
	UCapsuleComponent* Capsule = Body ? Body->GetCapsuleComponent() : nullptr;
	USkeletalMeshComponent* MeshComp = Body ? Body->GetMesh() : nullptr;
	UCharacterMovementComponent* Movement = Body ? Body->GetCharacterMovement() : nullptr;
	if (!Config || !Capsule || !MeshComp || DeltaTime <= 0.0f)
	{
		EndFollow(TEXT("전제 소실"));
		return;
	}

	// 1. component frame 을 바꾸기 전에 마지막으로 확정된 physics pose 를 읽는다.
	// mesh prerequisite 덕에 다음 animation/physics 평가는 새 frame 을 쓴다.
	const FTransform BoneTransform = MeshComp->GetSocketTransform(Follow.Bone, RTS_World);
	const FVector BoneLocation = BoneTransform.GetLocation();

	const FVector TargetLocation = ResolveFollowTarget(BoneLocation);
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (TryStartGetUp(*Config, Now))
	{
		return;
	}

	// 3. 스윕 이동. Mover 의 Try Safe Move And Slide 자리다 - 벽에 막히면 거기까지만 간다.
	const FVector StartLocation = Capsule->GetComponentLocation();
	const FRotator TargetRotation = VBRagdollBody::ResolveRotation(Recoverable.bActive ? PhysicsControl.Get() : nullptr,
		Config->bDrivenRagdollFollowsRotation, BoneTransform, Capsule->GetComponentRotation());
	VBRagdollBody::MoveCapsule(*Body, Follow.Bone, TargetLocation - StartLocation, TargetRotation, Now);

	// 4. 실제로 간 만큼을 속도로 심는다(목표 델타가 아니라) - 0 이면 카메라 랙, AI 인지가 '안 움직인다' 로 읽는다.
	if (Movement)
	{
		Movement->Velocity = (Capsule->GetComponentLocation() - StartLocation) / DeltaTime;
	}

	// 살아 있는 ragdoll 은 get-up 검사가 통과할 때까지 계속 시뮬레이션한다
	if (Recoverable.bActive)
	{
		return;
	}

	CheckFollowSettled(*Config, Now);
}

FVector UVBEnemyRagdollComponent::ResolveFollowTarget(const FVector& BoneLocation)
{
	Follow.bHasFloor = false;
	if (PhysicsControl)
	{
		PhysicsControl->SetEnvironment(FVector::UpVector, false);
	}

	// 2. 바닥을 찾는다.
	const FVBRagdollGround Ground = VBRagdollBody::FindGround(*Body, *Body->GetCapsuleComponent(), Body->GetMesh(), BoneLocation);
	if (Ground.bHasFloor)
	{
		Follow.bHasFloor = true;
		if (PhysicsControl)
		{
			PhysicsControl->SetEnvironment(Ground.Normal, true);
		}
	}

	return Ground.TargetLocation;
}

bool UVBEnemyRagdollComponent::TryStartGetUp(const UVBEnemyConfig& Config, double Now)
{
	if (!Recoverable.bActive || !GetOwner()->HasAuthority() || !PhysicsControl || !Config.PoweredRagdoll ||
		Now < Follow.NextGetUpCheckTime)
	{
		return false;
	}

	Follow.NextGetUpCheckTime = Now + Config.PoweredRagdoll->NPCGetUpCheckSeconds;
	const float Speed = PhysicsControl->GetAnimationState().Speed;
	VB_LOG(Log, "GASPGetUp check: %s spineSpeed=%.1f floor=%d", *GetNameSafe(GetOwner()), Speed, Follow.bHasFloor);
	const bool bStill = Follow.bHasFloor && Speed <= Config.PoweredRagdoll->NPCGetUpSpeed;
	if (!bStill)
	{
		return false;
	}

	// SavePoseSnapshot/PoseHistory 는 끝난 pose 를 평가한 frame 을 봐야 한다 - 잡기 전에 capsule 을 옮기지 않는다.
	// 누운 시간 - 처형 창의 반응 대기 상한과 겨루는 값.
	VB_LOG(Log, "쓰러진 몸 기상 시작[%s]: 누운 시간 %.1f초", *GetNameSafe(GetOwner()), Now - Follow.StartTime);
	ReplicatedRecoverable.Phase = EVBRecoverableRagdollPhase::GettingUp;
	GetOwner()->ForceNetUpdate();
	MulticastStartGetUp();
	return true;
}

void UVBEnemyRagdollComponent::CheckFollowSettled(const UVBEnemyConfig& Config, double Now)
{
	// 5. 전이 판정. 바디 전부 중 최고 속도를 본다 - 골반이 멎어도 팔다리가 튀면 아직 쓰러지는 중이다.
	const float MaxBodySpeed = VBRagdollBody::FindFastestBody(*Body->GetMesh()).Speed;
	Follow.SettledFrames = VBRagdollBody::NextSettledFrames(Follow.SettledFrames, MaxBodySpeed,
		Config.DrivenRagdollSettleSpeed);

	if (Follow.SettledFrames >= Config.DrivenRagdollSettleFrames)
	{
		EndFollow(TEXT("멎음"));
		return;
	}

	if (Now - Follow.StartTime >= Config.DrivenRagdollMaxSeconds)
	{
		EndFollow(TEXT("상한"));
	}
}

void UVBEnemyRagdollComponent::EndFollow(const TCHAR* Reason)
{
	if (!Follow.bActive)
	{
		return;
	}

	Follow.bActive = false;
	if (Recoverable.bActive)
	{
		ResetRecoverable();
		return;
	}

	if (PhysicsControl)
	{
		PhysicsControl->EndPoweredRagdoll();
	}

	SetComponentTickEnabled(false);

	UCapsuleComponent* Capsule = Body ? Body->GetCapsuleComponent() : nullptr;
	USkeletalMeshComponent* MeshComp = Body ? Body->GetMesh() : nullptr;
	if (MeshComp)
	{
		MeshComp->RemoveTickPrerequisiteComponent(this);
	}

	// 따라가기가 끝나면 캡슐은 다시 아무것도 아니다.
	if (Capsule)
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (UCharacterMovementComponent* Movement = Body ? Body->GetCharacterMovement() : nullptr)
	{
		Movement->Velocity = FVector::ZeroVector;
	}

	// 동작 확인: 캡슐과 래그돌 메시가 갈라졌는지를 숫자로 낸다.
	//   따라가기가 일을 했다면 이 값이 캡슐 반높이 언저리고, 안 했다면 죽은 자리까지의 거리만큼 크다.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const FVector CapsuleAt = Capsule ? Capsule->GetComponentLocation() : FVector::ZeroVector;
	const FVector BodyAt = (MeshComp && !Follow.Bone.IsNone())
		? MeshComp->GetSocketLocation(Follow.Bone)
		: CapsuleAt;
	VB_LOG(Log, "몰아지는 래그돌 A->B: %s 사유 %s, %.2f초 동안 %.0fcm 이동, 캡슐-몸 거리 %.0fcm",
		*GetNameSafe(GetOwner()), Reason, Now - Follow.StartTime,
		FVector::Dist(Follow.StartLocation, CapsuleAt), FVector::Dist(CapsuleAt, BodyAt));
}
