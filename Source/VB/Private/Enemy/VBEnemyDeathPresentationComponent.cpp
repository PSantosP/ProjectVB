// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Enemy/VBEnemyDeathPresentationComponent.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/VBMeleeTraceComponent.h" // VBCollision::MeleeTrace
#include "CharacterCommon/VBDeathDissolveComponent.h"
#include "CharacterCommon/VBHitFlashComponent.h"
#include "CharacterCommon/VBHitPhysicsComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBDeathDissolveConfig.h"
#include "Data/VBEnemyConfig.h"
#include "Enemy/VBEnemyBase.h"
#include "Enemy/VBGA_Knockdown.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "VB/VB.h"

UVBEnemyDeathPresentationComponent::UVBEnemyDeathPresentationComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

UVBEnemyDeathPresentationComponent* UVBEnemyDeathPresentationComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVBEnemyDeathPresentationComponent>() : nullptr;
}

void UVBEnemyDeathPresentationComponent::OnRegister()
{
	Super::OnRegister();
	// multicast 와 OnRep 가 BeginPlay 보다 먼저 올 수 있어 형제 컴포넌트는 등록 때 찾는다.
	AActor* Owner = GetOwner();
	Body = Cast<AVBEnemyBase>(Owner);
	Ragdoll = UVBEnemyRagdollComponent::FindFor(Owner);
	HitFlash = Owner ? Owner->FindComponentByClass<UVBHitFlashComponent>() : nullptr;
	HitPhysics = Owner ? Owner->FindComponentByClass<UVBHitPhysicsComponent>() : nullptr;
	Dissolve = Owner ? Owner->FindComponentByClass<UVBDeathDissolveComponent>() : nullptr;
}

bool UVBEnemyDeathPresentationComponent::IsAssembled() const
{
	return Body && Ragdoll && HitPhysics;
}

void UVBEnemyDeathPresentationComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!IsAssembled() || !HitFlash || !Dissolve)
	{
		VB_LOG(Error, "%s: 적 죽음 연출 조립이 빠졌다 (몸 %d, 래그돌 %d, 피격 플래시 %d, 피격 물리 %d, 디졸브 %d) - 죽음이 재생되지 않는다",
			*GetNameSafe(GetOwner()), Body != nullptr, Ragdoll != nullptr, HitFlash != nullptr, HitPhysics != nullptr, Dissolve != nullptr);
	}

	if (GetOwner() && GetOwner()->HasAuthority() && !GetIsReplicated())
	{
		VB_LOG(Error, "%s: 적 죽음 연출 컴포넌트가 복제되지 않는다 - 원격 클라에서 죽음이 보이지 않는다", *GetNameSafe(GetOwner()));
	}
}

void UVBEnemyDeathPresentationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Body)
	{
		Body->LandedDelegate.RemoveDynamic(this, &ThisClass::OnAirDeathLanded);
	}

	AirDeath.Instigator.Reset();
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UVBEnemyDeathPresentationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBEnemyDeathPresentationComponent, Snapshot);
}

void UVBEnemyDeathPresentationComponent::KeepMomentum(const FVector& LaunchVelocity)
{
	Launch.bKeepMomentum = true;
	Launch.Velocity = LaunchVelocity;
}

void UVBEnemyDeathPresentationComponent::ClearPendingMomentum()
{
	if (!Ragdoll || !Ragdoll->HasEnteredCorpse())
	{
		Launch.bKeepMomentum = false;
		Launch.Velocity = FVector::ZeroVector;
	}
}

void UVBEnemyDeathPresentationComponent::MarkBodyReady()
{
	Playback.bBodyReady = true;
	if (Snapshot.bDead)
	{
		ApplyState(Playback.bPendingLive);
	}
}

void UVBEnemyDeathPresentationComponent::CaptureAirDeath(const FVBDeathFacts& Facts)
{
	if (!IsAssembled())
	{
		return;
	}

	AirDeath.Instigator.Reset();
	Snapshot.bAirborne = Facts.bAirborneAtDeath;
	if (!Facts.bAirborneAtDeath)
	{
		return;
	}

	Launch.bKeepMomentum = true;
	Launch.Velocity = GetOwner()->GetVelocity();
	if (!Facts.Motion.IsValid())
	{
		return;
	}

	Snapshot.AirMotion = Facts.Motion;
	if (GetOwner()->HasAuthority())
	{
		APawn* Attacker = Cast<APawn>(Facts.Instigator);
		if (!Attacker)
		{
			if (const UAbilitySystemComponent* InstigatorASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Facts.Instigator))
			{
				Attacker = Cast<APawn>(InstigatorASC->GetAvatarActor());
			}
		}

		if (IsValid(Attacker) && Attacker->GetWorld() == GetWorld())
		{
			AirDeath.Instigator = Attacker;
		}
	}

	for (const FGameplayAbilitySpec& Spec : Body->GetAbilitySystemComponent()->GetActivatableAbilities())
	{
		const UVBGA_Knockdown* Reaction = Cast<UVBGA_Knockdown>(Spec.GetPrimaryInstance());
		if (!Reaction || !Reaction->IsActive())
		{
			continue;
		}

		Snapshot.DeathMontage = Reaction->GetAirSlamMontage();
		break;
	}

	if (!Snapshot.DeathMontage)
	{
		VB_LOG(Error, "Airborne lethal finish requires the victim's air-slam reaction: %s", *GetNameSafe(GetOwner()));
	}
}

void UVBEnemyDeathPresentationComponent::ChooseMontage(bool bWasDowned)
{
	if (Snapshot.bAirborne)
	{
		Snapshot.bHasDeathMontage = Snapshot.DeathMontage != nullptr;
		Snapshot.RagdollDelay = Snapshot.AirMotion.MaxAirSeconds;
		return;
	}

	const UVBEnemyConfig* Config = Body->GetEnemyConfig();
	if (bWasDowned && !Config->DownedDeathMontage)
	{
		VB_LOG(Log, "%s died downed; keep the prone pose and skip the death montage", *GetNameSafe(GetOwner()));
	}

	if (Launch.bKeepMomentum)
	{
		return;
	}

	Snapshot.DeathMontage = Config->PickDeathMontage(bWasDowned);
	if (const UAnimMontage* Montage = Snapshot.DeathMontage; Montage && Montage->RateScale > KINDA_SMALL_NUMBER)
	{
		Snapshot.bHasDeathMontage = true;
		Snapshot.RagdollDelay = FMath::Max(0.05f,
			Montage->GetPlayLength() / Montage->RateScale - Montage->GetBlendOutArgs().BlendTime);
	}
}

void UVBEnemyDeathPresentationComponent::Present(const FVBDeathFacts& Facts)
{
	if (!IsAssembled())
	{
		return;
	}

	Snapshot.bDead = true;
	Snapshot.bKeepMomentum = Launch.bKeepMomentum;
	Snapshot.LaunchVelocity = Launch.Velocity;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	Snapshot.ServerDeathTime = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	ChooseMontage(Facts.bDownedAtDeath);
	GetOwner()->ForceNetUpdate();
	MulticastPresent(Snapshot);
}

void UVBEnemyDeathPresentationComponent::MulticastPresent_Implementation(const FVBEnemyDeathState& State)
{
	if (!Snapshot.bDead)
	{
		Snapshot = State;
	}
	// 먼저 도착한 RepNotify의 더 진행된 ragdoll 단계를 이전 RPC로 되돌리지 않는다.
	ApplyState(true);
}

void UVBEnemyDeathPresentationComponent::OnRep_Snapshot()
{
	if (!Snapshot.bDead || !Playback.bBodyReady)
	{
		return;
	}
	// 동일 네트워크 갱신의 빠른 multicast가 먼저 실행될 기회를 준다. 복구는 발사를 재연하지 않는다.
	FTimerManager& Timers = GetWorld()->GetTimerManager();
	Timers.ClearTimer(Playback.ReplayTimer);
	Playback.ReplayTimer = Timers.SetTimerForNextTick(this, &ThisClass::ReplayState);
}

void UVBEnemyDeathPresentationComponent::ReplayState()
{
	ApplyState(false);
}

void UVBEnemyDeathPresentationComponent::ApplyState(bool bFromLiveEvent)
{
	if (!Snapshot.bDead || !IsAssembled())
	{
		return;
	}

	if (!Playback.bBodyReady)
	{
		Playback.bPendingLive |= bFromLiveEvent;
		return;
	}

	if (Playback.bStarted)
	{
		if (Snapshot.bRagdoll && !Ragdoll->HasEnteredCorpse())
		{
			Launch.bKeepMomentum = Snapshot.bKeepMomentum;
			Launch.Velocity = Snapshot.LaunchVelocity;
			EnterCorpse();
		}

		return;
	}

	FTimerManager& Timers = GetWorld()->GetTimerManager();
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const double ServerNow = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const float Elapsed = FMath::Max(0.0, ServerNow - Snapshot.ServerDeathTime);
	if (Snapshot.bHasDeathMontage && !Snapshot.DeathMontage &&
		!Snapshot.bRagdoll && Elapsed < Snapshot.RagdollDelay)
	{
		Timers.SetTimer(Playback.ReplayTimer, this, &ThisClass::ReplayState, 0.05f, false);
		return;
	}

	Timers.ClearTimer(Playback.ReplayTimer);
	Playback.bStarted = true;
	const bool bAuthority = GetOwner()->HasAuthority();
	Launch.bKeepMomentum = Snapshot.bKeepMomentum;
	Launch.bSuppressLaunch = !bFromLiveEvent || (!bAuthority && Snapshot.bRagdoll);
	Launch.Velocity = Launch.bSuppressLaunch ? FVector::ZeroVector : FVector(Snapshot.LaunchVelocity);
	Playback.StartTime = GetWorld()->GetTimeSeconds() - Elapsed;
	// 원격 클라에 State.Dead 를 바로 건다 - 복제 전 한두 틱 동안 시체를 조준하지 않게(태그 자체는 CountToOwner 로 모두에게 간다).
	UAbilitySystemComponent* ASC = Body->GetAbilitySystemComponent();
	if (!bAuthority && ASC && !UVBAbilitySystemStatics::IsDead(ASC))
	{
		ASC->AddLooseGameplayTag(VBGameplayTags::State_Dead, 1, EGameplayTagReplicationState::CountToOwner);
	}

	PlayCosmetic(Elapsed);
}

void UVBEnemyDeathPresentationComponent::PlayCosmetic(float ElapsedSinceDeath)
{
	if (HitFlash)
	{
		HitFlash->ClearFlash();
	}

	if (BeginAirDeath(ElapsedSinceDeath))
	{
		return;
	}

	Ragdoll->StopBodyForDeath();

	// 사망 몽타주가 있으면 끝난 뒤 시체로 전환한다.
	UAnimInstance* AnimInst = Body->GetMesh() ? Body->GetMesh()->GetAnimInstance() : nullptr;
	UAnimMontage* DeathMontage = Snapshot.DeathMontage;
	const float RemainingUntilRagdoll = Snapshot.RagdollDelay - ElapsedSinceDeath;
	// 이미 끝난 연출은 처음부터 재생하지 않는다. 진행 중이면 서버 시각에 해당하는 위치부터 시작한다.
	if (!Launch.bKeepMomentum && !Snapshot.bRagdoll && DeathMontage && AnimInst &&
		RemainingUntilRagdoll > 0.0f && DeathMontage->RateScale > KINDA_SMALL_NUMBER &&
		AnimInst->Montage_Play(DeathMontage, 1.0f, EMontagePlayReturnType::MontageLength,
			ElapsedSinceDeath * DeathMontage->RateScale) > 0.0f)
	{
		VB_LOG(Log, "사망 연출: %s 몽타주 %s (서버 경과 %.2f초, 래그돌까지 %.2f초)",
			*GetNameSafe(GetOwner()), *DeathMontage->GetName(), ElapsedSinceDeath, RemainingUntilRagdoll);
		FOnMontageEnded Ended;
		Ended.BindUObject(this, &ThisClass::OnDeathMontageEnded);
		AnimInst->Montage_SetEndDelegate(Ended, DeathMontage);
		GetWorld()->GetTimerManager().SetTimer(Playback.CorpseTimer, this, &ThisClass::EnterCorpse,
			RemainingUntilRagdoll, false);
		return;
	}

	EnterCorpse();
}

bool UVBEnemyDeathPresentationComponent::BeginAirDeath(float ElapsedSinceDeath)
{
	const FVBDeathMotion& Motion = Snapshot.AirMotion;
	if (!Snapshot.bAirborne || !Motion.IsValid() || Snapshot.bRagdoll
		|| ElapsedSinceDeath >= Motion.MaxAirSeconds)
	{
		return false;
	}

	const bool bAuthority = GetOwner()->HasAuthority();
	HitPhysics->StopAll();
	Body->GetCapsuleComponent()->SetCollisionResponseToChannel(VBCollision::MeleeTrace, ECR_Ignore);
	Body->GetMesh()->SetCollisionResponseToChannel(VBCollision::MeleeTrace, ECR_Ignore);
	UCharacterMovementComponent* Move = Body->GetCharacterMovement();
	Move->SetMovementMode(MOVE_Falling);
	if (bAuthority)
	{
		Move->Velocity = Motion.InitialVelocity;
		Move->UpdateComponentVelocity();
	}

	Body->LandedDelegate.AddUniqueDynamic(this, &ThisClass::OnAirDeathLanded);
	AirDeath.bImpactPending = bAuthority && Motion.GroundImpactCue.IsValid();
	AirDeath.bReactionFinished = true;
	UAnimInstance* Anim = Body->GetMesh()->GetAnimInstance();
	UAnimMontage* Montage = Snapshot.DeathMontage;
	if (Anim && Montage && Montage->RateScale > KINDA_SMALL_NUMBER
		&& ElapsedSinceDeath * Montage->RateScale < Montage->GetPlayLength()
		&& Anim->Montage_Play(Montage, 1.0f, EMontagePlayReturnType::MontageLength,
			ElapsedSinceDeath * Montage->RateScale) > 0.0f)
	{
		AirDeath.bReactionFinished = false;
		FOnMontageEnded Ended;
		Ended.BindUObject(this, &ThisClass::OnAirDeathMontageEnded);
		Anim->Montage_SetEndDelegate(Ended, Montage);
	}

	GetWorld()->GetTimerManager().SetTimer(Playback.CorpseTimer, this, &ThisClass::EnterCorpse,
		Motion.MaxAirSeconds - ElapsedSinceDeath, false);
	VB_LOG(Log, "AirDeath: %s reaction=%s speed=%.1f", *GetNameSafe(GetOwner()), *GetNameSafe(Montage), Motion.InitialVelocity.Z);
	return true;
}

void UVBEnemyDeathPresentationComponent::OnAirDeathMontageEnded(UAnimMontage* /*Montage*/, bool /*bInterrupted*/)
{
	AirDeath.bReactionFinished = true;
	if (GetOwner()->HasAuthority() && Body->GetCharacterMovement()->IsMovingOnGround())
	{
		EnterCorpse();
	}
}

void UVBEnemyDeathPresentationComponent::OnAirDeathLanded(const FHitResult& Hit)
{
	Body->LandedDelegate.RemoveDynamic(this, &ThisClass::OnAirDeathLanded);
	const bool bPlayImpact = AirDeath.bImpactPending;
	AirDeath.bImpactPending = false;
	APawn* ImpactInstigator = AirDeath.Instigator.Get();
	AirDeath.Instigator.Reset();
	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	Launch.Velocity = FVector::ZeroVector;
	Launch.bKeepMomentum = false;
	Snapshot.LaunchVelocity = FVector::ZeroVector;
	Snapshot.bKeepMomentum = false;
	if (bPlayImpact && Hit.bBlockingHit)
	{
		UAbilitySystemComponent* ASC = Body->GetAbilitySystemComponent();
		FGameplayCueParameters Parameters;
		Parameters.EffectContext = ASC->MakeEffectContext();
		Parameters.EffectContext.AddInstigator(ImpactInstigator, GetOwner());
		Parameters.EffectContext.AddHitResult(Hit);
		Parameters.Instigator = ImpactInstigator;
		Parameters.Location = Hit.ImpactPoint;
		Parameters.Normal = Hit.ImpactNormal;
		ASC->ExecuteGameplayCue(Snapshot.AirMotion.GroundImpactCue, Parameters);
		VB_LOG(Log, "AirDeathImpact: %s instigator=%s location=%s", *GetNameSafe(GetOwner()), *GetNameSafe(ImpactInstigator), *Hit.ImpactPoint.ToString());
	}

	GetOwner()->ForceNetUpdate();
	if (AirDeath.bReactionFinished)
	{
		EnterCorpse();
	}
}

void UVBEnemyDeathPresentationComponent::EndAirDeath()
{
	Body->LandedDelegate.RemoveDynamic(this, &ThisClass::OnAirDeathLanded);
	AirDeath.bImpactPending = false;
	AirDeath.Instigator.Reset();
}

void UVBEnemyDeathPresentationComponent::OnDeathMontageEnded(UAnimMontage* /*Montage*/, bool /*bInterrupted*/)
{
	// 끊겼든 끝까지 재생됐든 물리로 전환한다 - 끊긴 자세에서 멈춰 서 있는 것이 더 나쁘다.
	EnterCorpse();
}

void UVBEnemyDeathPresentationComponent::EnterCorpse()
{
	EndAirDeath();
	// 사망 연출에 이미 쓴 시간. 정리 지연은 죽은 순간부터 재는 값이라 몽타주에 쓴 만큼 빼야 한다.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const double ElapsedSinceDeath = Playback.bStarted ? FMath::Max(0.0, Now - Playback.StartTime) : 0.0;
	if (!Ragdoll->EnterCorpse(Launch, ElapsedSinceDeath))
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(Playback.CorpseTimer);
	if (GetOwner()->HasAuthority() && Snapshot.bDead)
	{
		Snapshot.bRagdoll = true;
		GetOwner()->ForceNetUpdate();
	}

	ScheduleCleanup(ElapsedSinceDeath);
}

void UVBEnemyDeathPresentationComponent::ScheduleCleanup(double ElapsedSinceDeath)
{
	// 정리 지연은 사망 몽타주 길이만큼 이미 흘렀으므로 남은 시간으로 잡는다(뺄셈은 여기 한 곳에서만 하고, 디졸브도 이 값을 쓴다).
	const UVBEnemyConfig* Config = Body->GetEnemyConfig();
	const float SecondsUntilCleanup =
		FMath::Max(0.1f, Config->DeathCleanupDelay - static_cast<float>(ElapsedSinceDeath));

	// 사라지는 연출을 정리 시각에 맞춰 예약한다(권한 무관, 래치로 한 번만).
	if (Dissolve)
	{
		Dissolve->ScheduleDissolve(SecondsUntilCleanup, Config->DeathDissolve.Get());
	}

	if (GetOwner()->HasAuthority())
	{
		FTimerHandle DestroyTimerHandle;
		GetWorld()->GetTimerManager().SetTimer(DestroyTimerHandle, this, &ThisClass::DestroyBody,
			SecondsUntilCleanup, false);
	}
}

void UVBEnemyDeathPresentationComponent::DestroyBody()
{
	GetOwner()->Destroy();
}
