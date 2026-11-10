// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_AirCombo.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/Tasks/AbilityTask_ApplyRootMotionMoveToActorForce.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimInstance.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBGameplayEffectContext.h"
#include "Character/VBCombatInputComponent.h"
#include "AbilitySystem/GameplayCues/VBGC_CombatFeedback.h"
#include "Character/VBAirAfterimageComponent.h"
#include "Character/VBTargetLockComponent.h"
#include "Character/Appearance/VBCharacterAppearanceComponent.h"
#include "Data/VBAirComboConfig.h"
#include "Data/VBAirAfterimageConfig.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBWeaponDefinition.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "VB/VB.h"

namespace
{
	const TCHAR* OpeningEndName(EVBAirOpeningEnd Reason)
	{
		switch (Reason)
		{
		case EVBAirOpeningEnd::Timeout: return TEXT("시간 끝");
		case EVBAirOpeningEnd::VictimLanded: return TEXT("대상 착지");
		case EVBAirOpeningEnd::VictimDied: return TEXT("대상 죽음");
		case EVBAirOpeningEnd::ChaseStarted: return TEXT("쫓기 시작");
		case EVBAirOpeningEnd::WeaponRemoved: return TEXT("무기 거둠");
		case EVBAirOpeningEnd::Replaced: return TEXT("새 대상");
		case EVBAirOpeningEnd::VictimGone: return TEXT("대상 사라짐");
		}

		return TEXT("?");
	}

	bool IsDead(const AActor* Actor)
	{
		const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
		return !Actor || (ASC && UVBAbilitySystemStatics::IsDead(ASC));
	}
}

UVBGA_AirCombo::UVBGA_AirCombo()
{
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Attack_Air));
	InputTag = VBGameplayTags::Input_Attack_Light;
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Attacking);
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Attacking_Air);
}

UVBAttackConfig* UVBGA_AirCombo::SelectWeaponLeaf(const UVBWeaponDefinition& Weapon) const
{
	const UVBAirComboConfig* AirCombo = Weapon.GetAirCombo();
	return AirCombo ? AirCombo->StringAttack.Get() : nullptr;
}

const UVBAirComboConfig* UVBGA_AirCombo::ResolveAirCombo() const
{
	const UVBWeaponDefinition* Weapon = GetGrantingWeapon();
	return Weapon ? Weapon->GetAirCombo() : nullptr;
}

void UVBGA_AirCombo::ReleaseAirMovement(ACharacter* Character, bool bWeSetFlying)
{
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (bWeSetFlying && Move && Move->MovementMode == MOVE_Flying)
	{
		Move->SetMovementMode(MOVE_Falling);
	}
}

void UVBGA_AirCombo::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (ASC && ActorInfo->IsNetAuthority() && !LaunchedEventHandle.IsValid())
	{
		LaunchedEventHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combat_LaunchedTarget)
			.AddUObject(this, &ThisClass::HandleLaunchedTarget);
	}
}

void UVBGA_AirCombo::OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	ClearOpening(EVBAirOpeningEnd::WeaponRemoved);
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (ASC && LaunchedEventHandle.IsValid())
	{
		if (FGameplayEventMulticastDelegate* Delegate =
			ASC->GenericGameplayEventCallbacks.Find(VBGameplayTags::Event_Combat_LaunchedTarget))
		{
			Delegate->Remove(LaunchedEventHandle);
		}
	}

	LaunchedEventHandle.Reset();
	Super::OnRemoveAbility(ActorInfo, Spec);
}

void UVBGA_AirCombo::HandleLaunchedTarget(const FGameplayEventData* Payload)
{
	OpenOpening(Payload ? Cast<ACharacter>(const_cast<AActor*>(Payload->Target.Get())) : nullptr);
}

void UVBGA_AirCombo::OpenOpening(ACharacter* Victim)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const UVBAirComboConfig* AirCombo = ResolveAirCombo();
	UWorld* World = GetWorld();
	if (!Victim || !ASC || !World)
	{
		return;
	}

	if (!AirCombo)
	{
		VB_LOG(Verbose, "[공중] %s 를 띄웠지만 이 무기에 공중 콤보가 없다", *GetNameSafe(Victim));
		return;
	}

	ClearOpening(EVBAirOpeningEnd::Replaced);
	bOpeningOpen = true;
	OpeningVictim = Victim;
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_AirOpening, 1, EGameplayTagReplicationState::CountToOwner);
	World->GetTimerManager().SetTimer(OpeningTimer, this, &ThisClass::OnOpeningTimeout, AirCombo->OpeningSeconds, false);
	Victim->LandedDelegate.AddDynamic(this, &ThisClass::OnOpeningVictimLanded);
	Victim->OnEndPlay.AddDynamic(this, &ThisClass::OnOpeningVictimEndPlay);
	if (UAbilitySystemComponent* VictimASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Victim))
	{
		OpeningDeadTagHandle = VictimASC->RegisterGameplayTagEvent(VBGameplayTags::State_Dead, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ThisClass::OnOpeningVictimDeadTag);
	}

	VB_LOG(Log, "[공중] 쫓을 기회 열림 - %s (%.2f초)", *GetNameSafe(Victim), AirCombo->OpeningSeconds);
}

void UVBGA_AirCombo::ClearOpening(EVBAirOpeningEnd Reason)
{
	if (!bOpeningOpen)
	{
		return;
	}

	bOpeningOpen = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(OpeningTimer);
	}

	UnbindOpeningVictim();
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_AirOpening, 1, EGameplayTagReplicationState::CountToOwner);
	}

	VB_LOG(Log, "[공중] 쫓을 기회 닫힘 - %s", OpeningEndName(Reason));
}

void UVBGA_AirCombo::UnbindOpeningVictim()
{
	if (ACharacter* Victim = OpeningVictim.Get())
	{
		Victim->LandedDelegate.RemoveDynamic(this, &ThisClass::OnOpeningVictimLanded);
		Victim->OnEndPlay.RemoveDynamic(this, &ThisClass::OnOpeningVictimEndPlay);
		if (UAbilitySystemComponent* VictimASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Victim))
		{
			VictimASC->RegisterGameplayTagEvent(VBGameplayTags::State_Dead,
				EGameplayTagEventType::NewOrRemoved).Remove(OpeningDeadTagHandle);
		}
	}

	OpeningDeadTagHandle.Reset();
	OpeningVictim.Reset();
}

void UVBGA_AirCombo::OnOpeningVictimLanded(const FHitResult& Hit)
{
	ClearOpening(EVBAirOpeningEnd::VictimLanded);
}

void UVBGA_AirCombo::OnOpeningVictimEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
	ClearOpening(EVBAirOpeningEnd::VictimGone);
}

void UVBGA_AirCombo::OnOpeningVictimDeadTag(const FGameplayTag Tag, int32 NewCount)
{
	if (NewCount > 0)
	{
		ClearOpening(EVBAirOpeningEnd::VictimDied);
	}
}

void UVBGA_AirCombo::OnOpeningTimeout()
{
	ClearOpening(EVBAirOpeningEnd::Timeout);
}

const TCHAR* UVBGA_AirCombo::FindChaseRefusal(const UVBAirComboConfig* AirCombo, const AActor* Attacker, const AActor* Victim)
{
	if (!AirCombo || !Attacker)
	{
		return TEXT("무기에 공중 콤보 설정이 없다");
	}

	if (!Victim || IsDead(Victim))
	{
		return TEXT("쫓을 대상이 없거나 죽었다");
	}

	if (FVector::Dist(Attacker->GetActorLocation(), Victim->GetActorLocation()) > AirCombo->MaxChaseDistance)
	{
		return TEXT("대상이 너무 멀다");
	}

	return nullptr;
}

bool UVBGA_AirCombo::PrepareActivation(const FVBActivationRequest& Request, FGameplayEventData& OutPayload) const
{
	// 공중 연타는 첫 섹션부터 시작하며 활성화 데이터에는 추격 대상만 담는다.
	if (!bOpeningOpen || !OpeningVictim.IsValid())
	{
		return false;
	}

	OutPayload.Instigator = GetAvatarActorFromActorInfo();
	OutPayload.Target = OpeningVictim.Get();
	return true;
}

void UVBGA_AirCombo::ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	const bool bIsServer = ActorInfo && ActorInfo->IsNetAuthority();
	ACharacter* Victim = TriggerEventData ? Cast<ACharacter>(const_cast<AActor*>(TriggerEventData->Target.Get())) : nullptr;
	if (!Victim && bIsServer)
	{
		Victim = OpeningVictim.Get();
	}

	const UVBAirComboConfig* AirCombo = ResolveAirCombo();
	ACharacter* Attacker = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* Move = Attacker ? Attacker->GetCharacterMovement() : nullptr;
	const TCHAR* Refusal = !Move ? TEXT("이동 컴포넌트가 없다") : FindChaseRefusal(AirCombo, Attacker, Victim);
	if (Refusal)
	{
		VB_LOG(Log, "[공중] %s 쫓기 거절 - %s", *GetNameSafe(Attacker), Refusal);
		EndAbility(Handle, ActorInfo, ActivationInfo, bIsServer, true);
		return;
	}

	// 실제 추격을 시작할 때만 기회를 소비한다.
	if (bIsServer)
	{
		ClearOpening(EVBAirOpeningEnd::ChaseStarted);
	}

	ChaseVictim = Victim;
	if (bIsServer)
	{
		if (UAbilitySystemComponent* VictimASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Victim))
		{
			ChaseDeadTagHandle = VictimASC->RegisterGameplayTagEvent(VBGameplayTags::State_Dead, EGameplayTagEventType::NewOrRemoved)
				.AddUObject(this, &ThisClass::OnChaseVictimDeadTag);
		}
	}

	DeferredTrigger = TriggerEventData ? *TriggerEventData : FGameplayEventData();
	GetWorld()->GetTimerManager().SetTimer(AttackerAirTimer, this, &ThisClass::OnAttackerAirExpired,
		AirCombo->AttackerMaxAirSeconds, false);
	VB_LOG(Log, "[공중] %s -> %s 쫓기 시작", *GetNameSafe(Attacker), *GetNameSafe(Victim));

	BeginChase(*AirCombo, *Attacker, *Victim);
	if (IsActive())
	{
		RequestVictimAirControl(VBGameplayTags::Event_Combat_AirHold, AirCombo);
	}
}

void UVBGA_AirCombo::BeginChase(const UVBAirComboConfig& Config, ACharacter& Attacker, ACharacter& Victim)
{
	Phase = EPhase::Chasing;
	UCharacterMovementComponent* Move = Attacker.GetCharacterMovement();
	bWeSetFlying = Move->MovementMode != MOVE_Flying;
	Move->SetMovementMode(MOVE_Flying);

	UAbilityTask_PlayMontageAndWait* Takeoff = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, TEXT("AirTakeoff"), Config.ChaseTakeoffMontage);
	Takeoff->OnCompleted.AddDynamic(this, &ThisClass::OnTakeoffEnded);
	Takeoff->OnInterrupted.AddDynamic(this, &ThisClass::OnTakeoffInterrupted);
	Takeoff->OnCancelled.AddDynamic(this, &ThisClass::OnTakeoffInterrupted);
	Takeoff->ReadyForActivation();
	if (!IsActive() || Phase != EPhase::Chasing)
	{
		return;
	}

	UAbilityTask_ApplyRootMotionMoveToActorForce* Chase = CreateChaseMovement(Config, Victim, false);
	Chase->OnFinished.AddDynamic(this, &ThisClass::OnChaseFinished);
	Chase->ReadyForActivation();
	if (IsActive() && Phase == EPhase::Chasing)
	{
		BeginStaging(Config, Attacker);
	}
}

UAbilityTask_ApplyRootMotionMoveToActorForce* UVBGA_AirCombo::CreateChaseMovement(
	const UVBAirComboConfig& Config, ACharacter& Victim, bool bFollow)
{
	return UAbilityTask_ApplyRootMotionMoveToActorForce::ApplyRootMotionMoveToActorForce(
		this, bFollow ? TEXT("AirStringFollow") : TEXT("AirChaseMove"), &Victim,
		FVector(Config.ChaseStopDistance, 0.0f, Config.ChaseHeightOffset),
		ERootMotionMoveToActorTargetOffsetType::AlignFromTargetToSource,
		bFollow ? Config.AttackerMaxAirSeconds : Config.ChaseSeconds, nullptr, nullptr,
		/*bSetNewMovementMode=*/false, MOVE_Flying, /*bRestrictSpeedToExpected=*/false, nullptr,
		bFollow ? Config.StringFollowTimeMapping.Get() : nullptr,
		bFollow ? ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity : ERootMotionFinishVelocityMode::SetVelocity,
		FVector::ZeroVector, 0.0f, /*bDisableDestinationReachedInterrupt=*/true, Config.ChaseArrivalTolerance);
}

void UVBGA_AirCombo::StopFollowing()
{
	if (UAbilityTask_ApplyRootMotionMoveToActorForce* Task = StringFollowTask.Get())
	{
		Task->EndTask();
	}

	StringFollowTask.Reset();
}

void UVBGA_AirCombo::BeginStaging(const UVBAirComboConfig& AirCombo, ACharacter& Attacker)
{
	StagingLease.Release();
	if (!Attacker.IsLocallyControlled() || !AirCombo.Staging)
	{
		return;
	}

	const UVBAirAfterimageConfig* Staging = AirCombo.Staging;
	const TArray<FText> Problems = Staging->FindProblems();
	if (!Problems.IsEmpty())
	{
		for (const FText& Problem : Problems)
		{
			VB_LOG(Error, "Air staging %s: %s", *GetNameSafe(Staging), *Problem.ToString());
		}

		return;
	}

	if (Staging->GhostMaterial)
	{
		if (UVBAirAfterimageComponent* Afterimage = UVBAirAfterimageComponent::FindOrAdd(&Attacker))
		{
			StagingLease.HoldAfterimage(*Afterimage, Afterimage->BeginSampling(Staging));
		}
	}

	if (Staging->BodyFade)
	{
		if (UVBCharacterAppearanceComponent* Appearance = Attacker.FindComponentByClass<UVBCharacterAppearanceComponent>())
		{
			StagingLease.HoldBodyFade(*Appearance, Appearance->BeginBodyFade(Staging->BodyFade, AirCombo.ChaseSeconds));
		}
		else
		{
			VB_LOG(Error, "Air staging requires an appearance owner for body fade");
		}
	}

	if (Staging->WhooshProfile)
	{
		FVBCombatFeedbackEvent Whoosh;
		Whoosh.Profile = Staging->WhooshProfile;
		Whoosh.Recipient = &Attacker;
		Whoosh.Direction = Attacker.GetActorForwardVector();
		Whoosh.bWorldEffects = false;
		UVBGC_CombatFeedback::PlayFeedback(Attacker.GetWorld(), Whoosh, TEXT("AirChase"));
	}
}

void UVBGA_AirCombo::OnTakeoffEnded()
{
	if (!IsActive() || Phase != EPhase::Chasing)
	{
		return;
	}

	const UVBAirComboConfig* Config = ResolveAirCombo();
	if (!Config || !Config->ChaseMontage)
	{
		StopAirCombo(TEXT("추격 자세가 없다"));
		return;
	}

	UAbilityTask_PlayMontageAndWait* Pose = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, TEXT("AirChase"), Config->ChaseMontage);
	Pose->OnInterrupted.AddDynamic(this, &ThisClass::OnTakeoffInterrupted);
	Pose->OnCancelled.AddDynamic(this, &ThisClass::OnTakeoffInterrupted);
	Pose->ReadyForActivation();
}

void UVBGA_AirCombo::OnTakeoffInterrupted()
{
	if (IsActive() && Phase == EPhase::Chasing)
	{
		StopAirCombo(TEXT("추격 자세가 끊김"));
	}
}

void UVBGA_AirCombo::OnChaseFinished(bool bDestinationReached, bool bTimedOut, FVector FinalTargetLocation)
{
	if (!IsActive() || Phase != EPhase::Chasing)
	{
		return;
	}

	if (!ChaseVictim.IsValid() || IsDead(ChaseVictim.Get()))
	{
		StopAirCombo(TEXT("쫓는 중 대상 잃음"));
		return;
	}

	const UVBAirComboConfig* Config = ResolveAirCombo();
	const AActor* Attacker = GetAvatarActorFromActorInfo();
	if (!Config || !Attacker || !Config->HasReachedChaseTarget(Attacker->GetActorLocation(), FinalTargetLocation))
	{
		VB_LOG(Verbose, "AirChaseRejected: reached=%d timedOut=%d", bDestinationReached, bTimedOut);
		StopAirCombo(TEXT("추격 목표에 도착하지 못함"));
		return;
	}

	VB_LOG(Log, "[공중] 추격 목표 도착");
	BeginString();
}

void UVBGA_AirCombo::BeginString()
{
	StagingLease.Release();
	const UVBAirComboConfig* Config = ResolveAirCombo();
	ACharacter* Victim = ChaseVictim.Get();
	if (!Config || !Config->StringFollowTimeMapping || !Victim)
	{
		VB_LOG(Error, "Air string requires a follow time mapping and a valid target");
		StopAirCombo(TEXT("연타 추적을 시작할 수 없음"));
		return;
	}

	if (ACharacter* Attacker = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		Attacker->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	}

	Phase = EPhase::String;
	StringFollowTask = CreateChaseMovement(*Config, *Victim, true);
	StringFollowTask->ReadyForActivation();
	UAbilityTask_WaitGameplayEvent* Presses = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, VBGameplayTags::Event_Combo_InputReceived, nullptr, /*OnlyTriggerOnce=*/false, /*OnlyMatchExact=*/true);
	Presses->EventReceived.AddDynamic(this, &ThisClass::OnStringPress);
	Presses->ReadyForActivation();
	// 연타는 부모가 친다 - 무기 잎(StringAttack)의 몽타주, 판정, 콤보 창 그대로.
	Super::ExecuteAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, &DeferredTrigger);
	if (IsActive() && GetActorInfo().IsLocallyControlled())
	{
		if (UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(GetAvatarActorFromActorInfo()))
		{
			StagingLease.HoldCameraFraming(*Lock, Lock->BeginCameraFraming(Victim, Config->StringCameraFraming));
		}
	}

	if (IsActive() && !GetActorInfo().IsNetAuthority() && GetActorInfo().IsLocallyControlled())
	{
		UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
		const FPredictionKey Key = CurrentActivationInfo.GetActivationPredictionKey();
		FinalStrikeEventHandle = ASC->AbilityReplicatedEventDelegate(EAbilityGenericReplicatedEvent::GameCustom1, CurrentSpecHandle, Key)
			.AddUObject(this, &ThisClass::OnFinalStrikeConfirmed);
		ASC->CallReplicatedEventDelegateIfSet(EAbilityGenericReplicatedEvent::GameCustom1, CurrentSpecHandle, Key);
	}
}

void UVBGA_AirCombo::OnStringPress(FGameplayEventData Payload)
{
	const UVBAirComboConfig* AirCombo = ResolveAirCombo();
	VB_LOG(Verbose, "AirSlamHold: string=%d slamming=%d config=%s threshold=%.3f", Phase == EPhase::String, Phase == EPhase::Slamming,
		*GetNameSafe(AirCombo), AirCombo ? AirCombo->HoldToSlamSeconds : -1.0f);
	if (Phase != EPhase::String || !AirCombo || !AirCombo->HasSlam())
	{
		return;
	}

	bAttackHeld = true;
	GetWorld()->GetTimerManager().SetTimer(SlamHoldTimer, this, &ThisClass::OnSlamHoldElapsed, AirCombo->HoldToSlamSeconds, false);
}

void UVBGA_AirCombo::InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	VB_LOG(Verbose, "AirSlamRelease: held=%d remaining=%.3f", bAttackHeld, GetWorld()->GetTimerManager().GetTimerRemaining(SlamHoldTimer));
	bAttackHeld = false;
	GetWorld()->GetTimerManager().ClearTimer(SlamHoldTimer);
	Super::InputReleased(Handle, ActorInfo, ActivationInfo);
}

void UVBGA_AirCombo::OnSlamHoldElapsed()
{
	VB_LOG(Verbose, "AirSlamThreshold: held=%d slamming=%d", bAttackHeld, Phase == EPhase::Slamming);
	if (bAttackHeld && Phase == EPhase::String)
	{
		BeginSlam();
	}
}

void UVBGA_AirCombo::ApplyAdditionalEffects(UAbilitySystemComponent* TargetASC, const FHitResult& Hit)
{
	Super::ApplyAdditionalEffects(TargetASC, Hit);
	const UVBAirComboConfig* Config = ResolveAirCombo();
	if (!IsActive() || Phase != EPhase::String || !Config || !Config->bSlamOnFinalHit || !IsFinalComboStrike()
		|| !TargetASC || TargetASC->GetAvatarActor() != ChaseVictim.Get())
	{
		return;
	}

	ConfirmFinalStrike();
	if (!IsActive() || Phase != EPhase::FinalStrike)
	{
		return;
	}

	RequestVictimSlam(*Config);
}

void UVBGA_AirCombo::ConfirmFinalStrike()
{
	if (!IsActive() || Phase != EPhase::String)
	{
		return;
	}

	OnFinalStrikeConfirmed();
	if (!IsActive() || Phase != EPhase::FinalStrike)
	{
		return;
	}

	if (!GetActorInfo().IsLocallyControlled())
	{
		GetAbilitySystemComponentFromActorInfo()->ClientSetReplicatedEvent(EAbilityGenericReplicatedEvent::GameCustom1,
			CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
	}
}

void UVBGA_AirCombo::RequestVictimAirControl(FGameplayTag Event, const UVBAirComboConfig* Config, float Magnitude)
{
	if (!GetActorInfo().IsNetAuthority() || !ChaseVictim.IsValid())
	{
		return;
	}

	FGameplayEventData Request;
	Request.Instigator = GetAvatarActorFromActorInfo();
	Request.Target = ChaseVictim.Get();
	Request.EventMagnitude = Magnitude;
	Request.OptionalObject = Config;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(ChaseVictim.Get(), Event, Request);
}

void UVBGA_AirCombo::ConfigureHitContext(FVBGameplayEffectContext& HitFacts, const AActor* Target) const
{
	Super::ConfigureHitContext(HitFacts, Target);
	const UVBAirComboConfig* Config = ResolveAirCombo();
	if (Target != ChaseVictim.Get() || Phase != EPhase::String || !IsFinalComboStrike() || !Config || !Config->bSlamOnFinalHit
		|| !Config->IsSlamMotionValid())
	{
		return;
	}

	FVBDeathMotion Motion;
	Motion.InitialVelocity = FVector(0.0f, 0.0f, -Config->SlamFallSpeed);
	Motion.MaxAirSeconds = Config->SlamMaxSeconds;
	Motion.GroundImpactCue = Config->SlamImpactCue;
	HitFacts.SetDeathMotion(Motion);
}

void UVBGA_AirCombo::OnChaseVictimDeadTag(const FGameplayTag Tag, int32 NewCount)
{
	if (NewCount <= 0 || !IsActive() || (Phase != EPhase::String && Phase != EPhase::Chasing))
	{
		return;
	}

	const UVBAirComboConfig* Config = ResolveAirCombo();
	if (Phase == EPhase::String && IsFinalComboStrike() && Config && Config->bSlamOnFinalHit)
	{
		ConfirmFinalStrike();
	}
	else
	{
		StopAirCombo(TEXT("공중 대상 사망"));
	}
}

void UVBGA_AirCombo::OnFinalStrikeConfirmed()
{
	GetAbilitySystemComponentFromActorInfo()->ConsumeGenericReplicatedEvent(EAbilityGenericReplicatedEvent::GameCustom1,
		CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
	if (!IsActive() || Phase != EPhase::String)
	{
		return;
	}

	const UVBAirComboConfig* Config = ResolveAirCombo();
	if (!Config || !Config->IsSlamMotionValid())
	{
		VB_LOG(Error, "Final air strike requires finite positive descent and landing limits");
		StopAirCombo(TEXT("마무리 설정 없음"));
		return;
	}

	Phase = EPhase::FinalStrike;
	GetWorld()->GetTimerManager().ClearTimer(SlamHoldTimer);
	bAttackHeld = false;
	StopFollowing();
	if (ACharacter* Attacker = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		Attacker->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	}

	GetWorld()->GetTimerManager().SetTimer(AttackerAirTimer, this, &ThisClass::OnAttackerAirExpired, Config->SlamMaxSeconds, false);
	VB_LOG(Log, "AirFinalStrike: attacker=%s victim=%s", *GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(ChaseVictim.Get()));
}

void UVBGA_AirCombo::RequestVictimSlam(const UVBAirComboConfig& Config)
{
	if (!FindChaseRefusal(&Config, GetAvatarActorFromActorInfo(), ChaseVictim.Get()))
	{
		RequestVictimAirControl(VBGameplayTags::Event_Combat_AirSlam, &Config, Config.SlamFallSpeed);
	}
}

void UVBGA_AirCombo::BeginSlam()
{
	const UVBAirComboConfig* AirCombo = ResolveAirCombo();
	ACharacter* Attacker = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UAnimInstance* AnimInstance = GetActorInfo().GetAnimInstance();
	UAnimMontage* Montage = const_cast<UAnimMontage*>(GetActiveMontage());
	if (Phase != EPhase::String || !AirCombo || !Attacker || !AnimInstance || !Montage)
	{
		VB_LOG(Verbose, "AirSlamRejected: config=%d avatar=%d anim=%d montage=%d", AirCombo != nullptr,
			Attacker != nullptr, AnimInstance != nullptr, Montage != nullptr);
		return;
	}

	if (!AirCombo->IsSlamMotionValid())
	{
		VB_LOG(Error, "Air slam requires a finite positive fall speed and landing timeout");
		StopAirCombo(TEXT("invalid slam fall speed"));
		return;
	}

	Phase = EPhase::Slamming;
	GetWorld()->GetTimerManager().ClearTimer(SlamHoldTimer);
	bAttackHeld = false;
	GetWorld()->GetTimerManager().SetTimer(AttackerAirTimer, this, &ThisClass::OnAttackerAirExpired, AirCombo->SlamMaxSeconds, false);
	StopFollowing();
	// 시작 -> 낙하 반복 -> (착지에서) 착지. 반복은 착지 알림이 끊는다.
	AnimInstance->Montage_JumpToSection(AirCombo->SlamStartSection, Montage);
	AnimInstance->Montage_SetNextSection(AirCombo->SlamStartSection, AirCombo->SlamLoopSection, Montage);
	AnimInstance->Montage_SetNextSection(AirCombo->SlamLoopSection, AirCombo->SlamLoopSection, Montage);
	ReleaseAirMovement(Attacker, bWeSetFlying);
	bWeSetFlying = false;
	Attacker->GetCharacterMovement()->Velocity = FVector(0.0f, 0.0f, -AirCombo->SlamFallSpeed);
	RequestVictimSlam(*AirCombo);
	Attacker->LandedDelegate.AddDynamic(this, &ThisClass::OnSlamLanded);
	VB_LOG(Log, "[공중] %s 내려찍기 시작", *GetNameSafe(Attacker));
}

void UVBGA_AirCombo::OnSlamLanded(const FHitResult& Hit)
{
	const UVBAirComboConfig* AirCombo = ResolveAirCombo();
	ACharacter* Attacker = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UAnimInstance* AnimInstance = GetActorInfo().GetAnimInstance();
	UAnimMontage* Montage = const_cast<UAnimMontage*>(GetActiveMontage());
	if (Attacker)
	{
		Attacker->LandedDelegate.RemoveDynamic(this, &ThisClass::OnSlamLanded);
	}

	if (Phase != EPhase::Slamming || !AirCombo || !AirCombo->HasFiniteSlamRecovery() || !AnimInstance
		|| !AirCombo->StringAttack || Montage != AirCombo->StringAttack->AttackMontage)
	{
		StopAirCombo(TEXT("착지 복구 몽타주가 없거나 종료할 수 없다"));
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(AttackerAirTimer);
	Phase = EPhase::Recovering;
	// 착지 섹션의 판정 창이 바닥 타격을 한 번 낸다(부모 판정).
	AnimInstance->Montage_JumpToSection(AirCombo->SlamEndSection, Montage);
	VB_LOG(Log, "[공중] %s 내려찍기 착지", *GetNameSafe(Attacker));
}

void UVBGA_AirCombo::OnAttackerAirExpired()
{
	StopAirCombo(TEXT("공중 한도 시간"));
}

void UVBGA_AirCombo::StopAirCombo(const TCHAR* Reason)
{
	VB_LOG(Log, "[공중] %s 끝 - %s", *GetNameSafe(GetAvatarActorFromActorInfo()), Reason);
	// 서버만 능력 종료를 복제한다.
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, CurrentActorInfo && CurrentActorInfo->IsNetAuthority(), true);
}

void UVBGA_AirCombo::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	StagingLease.Release();
	if (ChaseDeadTagHandle.IsValid())
	{
		if (UAbilitySystemComponent* VictimASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(ChaseVictim.Get()))
		{
			VictimASC->RegisterGameplayTagEvent(VBGameplayTags::State_Dead, EGameplayTagEventType::NewOrRemoved).Remove(ChaseDeadTagHandle);
		}
	}

	ChaseDeadTagHandle.Reset();
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		const FPredictionKey Key = ActivationInfo.GetActivationPredictionKey();
		if (FinalStrikeEventHandle.IsValid())
		{
			ASC->AbilityReplicatedEventDelegate(EAbilityGenericReplicatedEvent::GameCustom1, Handle, Key).Remove(FinalStrikeEventHandle);
		}

		ASC->ConsumeGenericReplicatedEvent(EAbilityGenericReplicatedEvent::GameCustom1, Handle, Key);
	}

	FinalStrikeEventHandle.Reset();
	// 모든 종료 경로에서 소유한 이동 상태와 구독을 해제한다.
	StopFollowing();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AttackerAirTimer);
	}

	ACharacter* Avatar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	RequestVictimAirControl(VBGameplayTags::Event_Combat_AirRelease, nullptr);
	if (UVBCombatInputComponent* Input = UVBCombatInputComponent::FindFor(Avatar))
	{
		Input->BeginAirRecovery();
	}

	ReleaseAirMovement(Avatar, bWeSetFlying);
	bWeSetFlying = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SlamHoldTimer);
	}

	if (Avatar)
	{
		Avatar->LandedDelegate.RemoveDynamic(this, &ThisClass::OnSlamLanded);
	}

	Phase = EPhase::Inactive;
	bAttackHeld = false;
	ChaseVictim.Reset();
	DeferredTrigger = FGameplayEventData();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
