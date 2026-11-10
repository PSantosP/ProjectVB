// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Enemy/VBGA_Knockdown.h"
#include "Data/VBAirComboConfig.h"

#include "AbilitySystemGlobals.h" // GetAbilitySystemComponentFromActor - 띄운 쪽에 알린다
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimInstance.h" // Montage_GetPosition - 넘어짐 클립이 어디까지 왔나
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h" // 공중 단계 - 착지 감시
#include "TimerManager.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBHitImpact.h" // VBGetPushYaw - 넘어지는 방향
#include "AI/VBEnemyCombatComponent.h"
#include "Data/VBHitReactConfig.h"
#include "Types/VBReportOnce.h"
#include "CharacterCommon/VBStrideScale.h"
#include "Enemy/VBEnemyBase.h" // MulticastPlayAdditiveFlinch - 플린치를 모든 머신에 실어 보낸다
#include "VB/VB.h"

UVBGA_Knockdown::UVBGA_Knockdown()
{
	// 두 번째 진입로: 넘어뜨리는 타격. 부모가 건 PoiseBroken 과 함께 이 GA 하나가 받는다 -
	//   같은 캐릭터를 두 GA 가 넘어뜨리면 누가 Downed 를 붙이고 떼는지가 갈라진다. 콘텐츠 유무는 ShouldAbilityRespondToEvent 가 답한다.
	FAbilityTriggerData KnockedDownTrigger;
	KnockedDownTrigger.TriggerTag = VBGameplayTags::Event_Combat_KnockedDown;
	AbilityTriggers.Add(KnockedDownTrigger);
	// 엎드린 동안 회피 누름은 회피가 아니라 빠져나오기다(TryEscape). 적은 입력이 없어 영향이 없다.
	ClaimedInputTags.AddTag(VBGameplayTags::Input_Dodge);

	// 경직과 이름을 나눈다 - 전투 규칙 표(DA_CombatRules)에서 경직은 공격을 막고 넘어짐은 막지 않는다(엎드린 채 휘두르는 반격 GA 가 공격이다).
	//   다운 중 두 번째 파탄, 넘어뜨리는 타격이 다운을 다시 시작하지 않는 것도 표의 State.Combat.Downed 줄이다.
	//   쓰러진 캐릭터에 온 타격은 허브가 무거움으로 내려 보내고, 그것은 아래 다운 피격(Hit 구독)이 받는다.
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Reaction_Knockdown));

	// bRetriggerInstancedAbility 는 부모 값(true) 그대로다. 다운 경로는 위 Downed 차단이 먼저 걸려 재진입이 없고,
	//   쿨다운 경로(서 있는 경직)는 부모와 같은 연타 FEEL 이 필요하다.
}

const FVBKnockdownSet* UVBGA_Knockdown::GetKnockdownSet() const
{
	return HitReactConfig ? HitReactConfig->FindKnockdownSet(ActiveStance) : nullptr;
}

void UVBGA_Knockdown::ReportKnockdownProblemsOnce() const
{
	if (!HitReactConfig || !VBFirstReportFor(HitReactConfig))
	{
		return;
	}

	for (const FText& Problem : HitReactConfig->FindProblems())
	{
		VB_LOG(Warning, "[다운] %s: %s", *GetNameSafe(HitReactConfig), *Problem.ToString());
	}
}

bool UVBGA_Knockdown::ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayEventData* Payload) const
{
	if (!Super::ShouldAbilityRespondToEvent(ActorInfo, Payload))
	{
		return false;
	}
	// 넘어뜨리는 타격은 넘어질 콘텐츠가 있을 때만 받는다. 여기서 거절하면 허브가 켜진 수 0 을 보고 무거운 피격으로 내린다 -
	//   받고 나서 서 있는 경직으로 떨어지면 허브가 모르는 다른 반응이 된다. 기절 게이지 소진은 콘텐츠가 없어도 받는다(부모의 경직).
	if (Payload && Payload->EventTag == VBGameplayTags::Event_Combat_KnockedDown)
	{
		const AActor* Victim = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
		const FVBKnockdownSet* Set = HitReactConfig ? HitReactConfig->FindKnockdownSet(VBResolveVictimStance(Victim)) : nullptr;
		return Set && Set->IsAuthored();
	}

	return true;
}

void UVBGA_Knockdown::RunReaction(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	ActiveStance = VBResolveVictimStance(GetAvatarActorFromActorInfo());
	const FVBKnockdownSet* Set = GetKnockdownSet();
	const AActor* Avatar = GetAvatarActorFromActorInfo();

	// 무엇이 넘어뜨리나. 넘어뜨리는 타격이면 쿨다운, bFallOnPoiseBreak 를 안 본다 - 그 둘은 기절 게이지 소진의 규칙이다.
	const bool bFromImpact = TriggerEventData && TriggerEventData->EventTag == VBGameplayTags::Event_Combat_KnockedDown;

	// 못 넘어지면 서 있는 경직으로 떨어진다 - 부모가 방향까지 봐서 종전 그대로 돈다.
	if (const TCHAR* Reason = ResolveNoFallReason(Set, bFromImpact))
	{
		VB_LOG(Log, "[다운] %s 넘어지지 않는다 - %s -> 서 있는 경직", *GetNameSafe(Avatar), Reason);
		Super::RunReaction(Handle, ActorInfo, ActivationInfo, TriggerEventData);
		return;
	}

	const bool bLaunchPath = PickDownClips(*Set, bFromImpact, TriggerEventData);

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 여기부터(루즈태그, GE, 타이머, 반격 시동)는 서버만 돈다. 플레이어에게 주면 소유 클라도 이 함수를 돌리므로 서버에서만 흐름을 진행한다.
	if (!ActorInfo->IsNetAuthority())
	{
		return;
	}
	// 복제 상태는 프로젝트의 다른 루즈태그(슈퍼아머, 사망, 페이즈 전환)와 같은 CountToOwner 다.
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_Downed, 1, EGameplayTagReplicationState::CountToOwner);
	bDownedTagGranted = true;

	// 기절 게이지 소진 경로는 돌리지 않는다(AI 포커스는 Staggered 동안 풀려 회전을 안 되돌린다).
	if (bFromImpact)
	{
		TurnToFaceImpact(*TriggerEventData);
	}

	// 받는 피해 배수의 값은 이 GE 자산이 든다(UVBCombatAttributeSet::DamageTakenScale). 비면 배수 없이 다운만.
	if (Set->VulnerabilityEffect)
	{
		const FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(Set->VulnerabilityEffect, GetAbilityLevel());
		VulnerabilityHandle = ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, Spec);
	}

	WatchDownHits();
	WarnIfDownIdleBlendsIn();
	LogGoingDown(*Set);
	BeginDownPath(*Set, bLaunchPath, TriggerEventData);
}

const TCHAR* UVBGA_Knockdown::ResolveNoFallReason(const FVBKnockdownSet* Set, bool bFromImpact) const
{
	// 못 넘어지는 이유 셋. 넘어뜨리는 타격은 첫째 이유로는 여기 못 온다(ShouldAbilityRespondToEvent 가 먼저 거절).
	//   둘째, 셋째는 기절 게이지 소진 전용이다.
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (!Set || !Set->IsAuthored())
	{
		ReportKnockdownProblemsOnce();
		return TEXT("KnockdownSet 에 땅 다운 묶음(넘어짐 · 대기 · 기상)이 없다");
	}

	if (!bFromImpact && !Set->bFallOnPoiseBreak)
	{
		return TEXT("기절 게이지 소진은 넘어뜨리지 않는 아키타입(bFallOnPoiseBreak=false)");
	}

	if (!bFromImpact && Now < CooldownUntil)
	{
		return TEXT("다운 쿨다운 중");
	}

	return nullptr;
}

bool UVBGA_Knockdown::PickDownClips(const FVBKnockdownSet& Set, bool bFromImpact, const FGameplayEventData* TriggerEventData)
{
	// 이번 다운의 길과 클립 묶음 하나 - 넘어짐부터 기상까지 이 묶음만 쓴다(팩을 섞으면 자세가 튄다). 직전 묶음은 다시 안 고른다.
	const bool bLaunchPath = bFromImpact && Set.IsLaunchAuthored()
		&& VBHitImpactFromTags(TriggerEventData->InstigatorTags) == EVBHitImpact::Launch;
	const TArray<FVBKnockdownClips> Choices = Set.ClipsFor(bLaunchPath);
	int32& PathPick = ClipPickIndex[bLaunchPath ? 1 : 0];
	PathPick = VBPickHitReactIndex(Choices.Num(), ClipPickSequence++, PathPick);
	ActiveClips = Choices[PathPick];
	return bLaunchPath;
}

void UVBGA_Knockdown::TurnToFaceImpact(const FGameplayEventData& Event)
{
	// 밀린 방향의 반대를 향해 돌려 세운 뒤 넘어뜨린다 - 넘어짐 클립이 한 방향뿐이고, 무거운 피격, 넉백과 같은 방향이 된다.
	//   밀린 방향이 없으면 공격자 위치를 향한다.
	AActor* FallingActor = GetAvatarActorFromActorInfo();
	float PushYaw = 0.0f;
	if (FallingActor && VBGetPushYaw(Event, PushYaw))
	{
		FallingActor->SetActorRotation(FRotator(0.0f, PushYaw + 180.0f, 0.0f));
	}
	// 이벤트의 Instigator 는 플레이어면 PlayerState(위치 원점)다 - Pawn 으로 풀어 읽는다(VBResolveInstigatorBody).
	else if (const AActor* Attacker = VBResolveInstigatorBody(Event.Instigator.Get()); FallingActor && Attacker)
	{
		FVector ToAttacker = Attacker->GetActorLocation() - FallingActor->GetActorLocation();
		ToAttacker.Z = 0.0f;
		if (!ToAttacker.IsNearlyZero())
		{
			FallingActor->SetActorRotation(FRotator(0.0f, ToAttacker.Rotation().Yaw, 0.0f));
		}
	}
}

void UVBGA_Knockdown::WatchDownHits()
{
	// 다운 중 피격 반응. 이벤트는 피해가 실제로 든 자리(VBHealthAttributeSet)가 쏜다 - 무적, 가드로 무른 타격은 여기 안 온다.
	UAbilityTask_WaitGameplayEvent* HitWait = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, VBGameplayTags::Event_Combat_Hit, /*OptionalExternalTarget=*/nullptr, /*OnlyTriggerOnce=*/false, /*OnlyMatchExact=*/true);
	HitWait->EventReceived.AddDynamic(this, &ThisClass::OnHitEventReceived);
	HitWait->ReadyForActivation();
}

void UVBGA_Knockdown::WarnIfDownIdleBlendsIn() const
{
	// 고른 묶음은 대기 클립을 반드시 가진다(FVBKnockdownClips::CoversPath).
	if (ActiveClips.DownIdle->BlendModeIn != EMontageBlendMode::Inertialization
		&& ActiveClips.DownIdle->BlendIn.GetBlendTime() > KINDA_SMALL_NUMBER)
	{
		// 막을 수는 없고(자산 값) 말할 수는 있다 - 표준 블렌드로 0 에서 들어오면 넘어짐->대기 이음매에서 몸이 뜬다.
		//   관성 모드여도 ABP 슬롯 뒤에 관성 노드가 없으면 끊긴다 - 그건 엔진이 "No Inertialization node found" 로 알린다.
		VB_LOG(Warning, "[다운] %s %s 가 표준 블렌드인 %.2f초다 - 관성 모드(또는 0)가 아니면 앞 몽타주가 빠지는 사이 서 있는 자세가 섞여 누운 몸이 뜬다",
			*GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(ActiveClips.DownIdle), ActiveClips.DownIdle->BlendIn.GetBlendTime());
	}
}

void UVBGA_Knockdown::LogGoingDown(const FVBKnockdownSet& Set) const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	VB_LOG(Log, "[다운] %s 넘어진다 - 대기 %.1f초, 쿨다운 %.0f초, 반격 %s(반경 %.0f = 자산 %.0f x 몸 배율 %.2f)",
		*GetNameSafe(Avatar), Set.DownHoldSeconds, Set.CooldownSeconds,
		Set.HasDownAttack() ? TEXT("있음") : TEXT("없음"), Set.DownAttackTriggerRadius * VBStrideScale::Resolve(Avatar),
		Set.DownAttackTriggerRadius, VBStrideScale::Resolve(Avatar));
}

void UVBGA_Knockdown::BeginDownPath(const FVBKnockdownSet& Set, bool bLaunchPath, const FGameplayEventData* TriggerEventData)
{
	FallTimeCutShort = 0.0f;
	bArrivedByLaunch = false;
	// 띄우는 타격이고 공중 클립이 있으면 넘어짐 대신 공중 흐름. 없으면 띄움도 넘어짐으로 간다(공중 칸이 생기기 전 동작).
	if (bLaunchPath)
	{
		AnnounceLaunchTo(VBResolveInstigatorBody(TriggerEventData->Instigator.Get()));
		BeginAirborne(Set);
		return;
	}

	BeginFalling();
}

void UVBGA_Knockdown::BeginFalling()
{
	Phase = EKnockdownPhase::Falling;
	UAbilityTask_PlayMontageAndWait* Fall = MakeMontageTask(ActiveClips.Fall);
	Fall->OnBlendOut.AddDynamic(this, &ThisClass::OnFallEnded);
	Fall->OnInterrupted.AddDynamic(this, &ThisClass::OnFallEnded);
	Fall->OnCancelled.AddDynamic(this, &ThisClass::OnFallEnded);
	Fall->ReadyForActivation();
}

void UVBGA_Knockdown::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	AirSuspension.Release();
	AirLauncher.Reset();
	ClearSlamImpact();
	SlamReactionTask.Reset();
	// Phase 가 None 이면 서 있는 경직 경로였다 - 다운의 정리, 쿨다운은 없다.
	if (Phase != EKnockdownPhase::None)
	{
		const FVBKnockdownSet* Set = GetKnockdownSet();
		UWorld* World = GetWorld();
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		const float Cooldown = Set ? Set->CooldownSeconds : 0.0f;
		CooldownUntil = Now + Cooldown;

		if (World)
		{
			World->GetTimerManager().ClearTimer(HoldTimer);
			World->GetTimerManager().ClearTimer(DownCounter.WatchTimer);
			World->GetTimerManager().ClearTimer(AirTimer);
		}

		if (UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
		{
			if (bDownedTagGranted)
			{
				ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Downed);
			}

			if (VulnerabilityHandle.IsValid())
			{
				ASC->RemoveActiveGameplayEffect(VulnerabilityHandle);
			}
		}

		DownCounter.ReleaseAbilityEnd();
		if (UAnimInstance* AnimInstance = DownCounter.TakeAnimSubscription())
		{
			AnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &ThisClass::OnAnyMontageBlendingOut);
		}

		VulnerabilityHandle.Invalidate();
		DownHitTask.Reset();
		VB_LOG(Log, "[다운] %s 끝(%s) - 다음 파탄은 %.0f초 동안 서 있는 경직",
			*GetNameSafe(GetAvatarActorFromActorInfo()), bWasCancelled ? TEXT("취소") : TEXT("기상"), Cooldown);

		Phase = EKnockdownPhase::None;
		bDownedTagGranted = false;
		bHoldExpired = false;
		bDownHitPlaying = false;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

UAbilityTask_PlayMontageAndWait* UVBGA_Knockdown::MakeMontageTask(UAnimMontage* Montage)
{
	// 부모 경직과 같은 인자: 재생 속도는 HitReactConfig->MontagePlayRate, 어빌리티가 끝나면 몽타주도 멈춘다(bStopWhenAbilityEnds),
	//   블렌드아웃 뒤 중단 알림은 받지 않는다(bAllowInterruptAfterBlendOut=false - 한 태스크에서 BlendOut 과 Interrupted 가 둘 중 하나만 오는 근거).
	return UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, Montage, HitReactConfig ? HitReactConfig->MontagePlayRate : 1.0f, NAME_None, true, 1.0f, 0.0f, false);
}

void UVBGA_Knockdown::OnFallEnded()
{
	if (Phase != EKnockdownPhase::Falling)
	{
		return;
	}

	BeginHold();
}

void UVBGA_Knockdown::BeginAirborne(const FVBKnockdownSet& Set)
{
	Phase = EKnockdownPhase::Airborne;
	bArrivedByLaunch = true;
	AirSuspension.Begin(Set.MaxAirSeconds);
	bAirHitPlaying = false;
	UAbilityTask_WaitGameplayEvent* SlamRequests = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, VBGameplayTags::Event_Combat_AirSlam, nullptr, false, true);
	SlamRequests->EventReceived.AddDynamic(this, &ThisClass::OnAirSlamRequested);
	SlamRequests->ReadyForActivation();
	UAbilityTask_WaitGameplayEvent* HoldRequests = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, VBGameplayTags::Event_Combat_AirHold, nullptr, false, true);
	HoldRequests->EventReceived.AddDynamic(this, &ThisClass::OnAirHoldRequested);
	HoldRequests->ReadyForActivation();
	UAbilityTask_WaitGameplayEvent* ReleaseRequests = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, VBGameplayTags::Event_Combat_AirRelease, nullptr, false, true);
	ReleaseRequests->EventReceived.AddDynamic(this, &ThisClass::OnAirReleaseRequested);
	ReleaseRequests->ReadyForActivation();
	VB_LOG(Log, "[다운] %s 띄워진다 - 최대 체공 %.1f초", *GetNameSafe(GetAvatarActorFromActorInfo()), Set.MaxAirSeconds);

	UAbilityTask_PlayMontageAndWait* Launch = MakeMontageTask(ActiveClips.Launch);
	Launch->OnBlendOut.AddDynamic(this, &ThisClass::OnAirLoopEnded);
	Launch->OnInterrupted.AddDynamic(this, &ThisClass::OnAirLoopEnded);
	Launch->OnCancelled.AddDynamic(this, &ThisClass::OnAirLoopEnded);
	Launch->ReadyForActivation();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(AirTimer, this, &ThisClass::TickAirWatch, Set.AirWatchSeconds, true);
	}
}

void UVBGA_Knockdown::AnnounceLaunchTo(const AActor* Attacker)
{
	AirLauncher.Reset();
	AActor* Victim = GetAvatarActorFromActorInfo();
	UAbilitySystemComponent* AttackerASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Attacker);
	if (!Victim)
	{
		VB_LOG(Warning, "[다운] 띄움을 알릴 몸(아바타)이 없다 - 공중 콤보 쫓기가 열리지 않는다");
		return;
	}

	if (!AttackerASC)
	{
		// 공격자 Pawn 을 못 찾았으면 알린다. 능력 없는 공격자(환경, 함정)는 쫓을 쪽이 없어 정상이다.
		VB_CLOG(!Attacker, Warning, "[다운] %s 띄움을 알릴 공격자 몸을 못 찾았다 - 공중 콤보 쫓기가 열리지 않는다", *GetNameSafe(Victim));
		VB_CLOG(Attacker != nullptr, Verbose, "[다운] %s 를 띄운 %s 는 능력이 없다 - 쫓기 없음", *GetNameSafe(Victim), *GetNameSafe(Attacker));
		return;
	}

	FGameplayEventData Payload;
	Payload.EventTag = VBGameplayTags::Event_Combat_LaunchedTarget;
	Payload.Instigator = Attacker;
	Payload.Target = Victim;
	AirLauncher = Attacker;
	AttackerASC->HandleGameplayEvent(Payload.EventTag, &Payload);
}

UAnimMontage* UVBGA_Knockdown::GetAirSlamMontage() const
{
	const FVBKnockdownSet* Set = GetKnockdownSet();
	return Set ? Set->AirSlamMontage.Get() : nullptr;
}

bool UVBGA_Knockdown::IsValidAirRequest(const FGameplayEventData& Payload) const
{
	const UAbilitySystemComponent* LauncherASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(AirLauncher.Get());
	return GetActorInfo().IsNetAuthority() && Phase == EKnockdownPhase::Airborne
		&& AirLauncher.IsValid() && Payload.Instigator.Get() == AirLauncher.Get()
		&& Payload.Target.Get() == GetAvatarActorFromActorInfo() && LauncherASC
		&& LauncherASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking_Air)
		&& !UVBAbilitySystemStatics::IsDead(GetAbilitySystemComponentFromActorInfo());
}

void UVBGA_Knockdown::OnAirHoldRequested(FGameplayEventData Payload)
{
	const UVBAirComboConfig* Config = Cast<UVBAirComboConfig>(Payload.OptionalObject.Get());
	if (IsValidAirRequest(Payload) && IsValid(Config))
	{
		AirSuspension.TryHold(Config->AttackerMaxAirSeconds);
	}
}

void UVBGA_Knockdown::OnAirReleaseRequested(FGameplayEventData Payload)
{
	if (IsValidAirRequest(Payload))
	{
		AirSuspension.Release();
	}
}

void UVBGA_Knockdown::OnAirSlamRequested(FGameplayEventData Payload)
{
	ACharacter* Victim = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* Move = Victim ? Victim->GetCharacterMovement() : nullptr;
	const UVBAirComboConfig* Config = Cast<UVBAirComboConfig>(Payload.OptionalObject.Get());
	if (!IsValidAirRequest(Payload) || !Move || !Move->IsFalling() || !IsValid(Config) || !Config->IsSlamMotionValid()
		|| !AirSuspension.CanSlam(Payload.EventMagnitude, Config->SlamMaxSeconds))
	{
		return;
	}

	const FVBKnockdownSet* Set = GetKnockdownSet();
	if (!Set || !Set->AirSlamMontage)
	{
		VB_LOG(Error, "Air slam requires a victim-owned reaction montage: %s", *GetNameSafe(Victim));
		return;
	}

	AirSuspension.Slam(*Move, Payload.EventMagnitude, Config->SlamMaxSeconds);
	Phase = EKnockdownPhase::Slamming;
	PendingSlamImpactCue = Config->SlamImpactCue;
	if (PendingSlamImpactCue.IsValid())
	{
		Victim->LandedDelegate.AddUniqueDynamic(this, &ThisClass::OnSlamImpact);
	}

	UAbilityTask_PlayMontageAndWait* Reaction = MakeMontageTask(Set->AirSlamMontage);
	SlamReactionTask = Reaction;
	Reaction->OnCompleted.AddDynamic(this, &ThisClass::OnSlamReactionEnded);
	Reaction->OnInterrupted.AddDynamic(this, &ThisClass::OnSlamReactionEnded);
	Reaction->OnCancelled.AddDynamic(this, &ThisClass::OnSlamReactionEnded);
	Reaction->ReadyForActivation();
	VB_LOG(Verbose, "AirSlamAccepted: victim=%s speed=%.1f", *GetNameSafe(Victim), Payload.EventMagnitude);
}

void UVBGA_Knockdown::OnSlamReactionEnded()
{
	SlamReactionTask.Reset();
}

void UVBGA_Knockdown::ClearSlamImpact()
{
	PendingSlamImpactCue = FGameplayTag();
	if (ACharacter* Victim = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		Victim->LandedDelegate.RemoveDynamic(this, &ThisClass::OnSlamImpact);
	}
}

void UVBGA_Knockdown::OnSlamImpact(const FHitResult& Hit)
{
	const FGameplayTag Cue = PendingSlamImpactCue;
	ClearSlamImpact();
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!GetActorInfo().IsNetAuthority() || Phase != EKnockdownPhase::Slamming || !ASC || !Cue.IsValid() || !Hit.bBlockingHit)
	{
		return;
	}

	FGameplayCueParameters Parameters;
	Parameters.EffectContext = ASC->MakeEffectContext();
	Parameters.EffectContext.AddHitResult(Hit);
	Parameters.Location = Hit.ImpactPoint;
	Parameters.Normal = Hit.ImpactNormal;
	Parameters.Instigator = const_cast<AActor*>(AirLauncher.Get());
	ASC->ExecuteGameplayCue(Cue, Parameters);
	VB_LOG(Log, "AirSlamImpact: victim=%s cue=%s location=%s", *GetNameSafe(GetAvatarActorFromActorInfo()), *Cue.ToString(),
		*Hit.ImpactPoint.ToString());
}

void UVBGA_Knockdown::HoldUpAfterAirHit(const FVBKnockdownSet& Set)
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (AirSuspension.HoldUpAfterHit(Set, Move))
	{
		VB_LOG(Log, "[다운] %s 공중 피격 - 체공 한도 %.2f초 (지금 %.2f초)", *GetNameSafe(Character), AirSuspension.GetBudgetSeconds(),
			AirSuspension.GetElapsedSeconds());
	}
}

void UVBGA_Knockdown::PlayAirLoop()
{
	const FVBKnockdownSet* Set = GetKnockdownSet();
	if (Phase != EKnockdownPhase::Airborne || bAirHitPlaying || !Set || !ActiveClips.AirLoop)
	{
		return;
	}

	UAbilityTask_PlayMontageAndWait* Loop = MakeMontageTask(ActiveClips.AirLoop);
	// BlendOut 만 건다 - 공중 피격, 착지 몽타주가 루프를 밀어내는 것이 정상 경로다(PlayDownIdle 과 같은 규칙).
	Loop->OnBlendOut.AddDynamic(this, &ThisClass::OnAirLoopEnded);
	Loop->ReadyForActivation();
}

void UVBGA_Knockdown::OnAirLoopEnded()
{
	PlayAirLoop();
}

void UVBGA_Knockdown::TickAirWatch()
{
	const FVBKnockdownSet* Set = GetKnockdownSet();
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!IsAirbornePhase() || !Set || !Move)
	{
		BeginLanding();
		return;
	}

	const bool bFalling = Move->IsFalling();
	if (Phase == EKnockdownPhase::Airborne)
	{
		AirSuspension.FreezeIfHeld(*Move, bFalling);
	}

	const bool bSlamReactionPlaying = Phase == EKnockdownPhase::Slamming && SlamReactionTask.IsValid();
	const EVBAirWatchResult Result = AirSuspension.AdvanceWatch(Set->AirWatchSeconds, bFalling, bSlamReactionPlaying,
		Set->AirTakeoffGraceSeconds);
	if (Result == EVBAirWatchResult::Stay)
	{
		return;
	}

	VB_LOG(Log, "[다운] %s 공중 끝 - %s (%.2f초)", *GetNameSafe(Character),
		Result == EVBAirWatchResult::Landed ? TEXT("착지")
		: Result == EVBAirWatchResult::NeverLeftGround ? TEXT("뜨지 않음(띄움 높이 0?)") : TEXT("최대 체공 시간"),
		AirSuspension.GetElapsedSeconds());
	BeginLanding();
}

void UVBGA_Knockdown::BeginLanding()
{
	AirSuspension.Release();
	ClearSlamImpact();
	// 단계 검사보다 먼저 끈다. 감시 틱이 이미 다른 단계에서 여기로 오면 타이머만 남아 EndAbility 까지 돌았다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AirTimer);
	}

	if (!IsAirbornePhase())
	{
		return;
	}

	const FVBKnockdownSet* Set = GetKnockdownSet();
	const bool bAfterSlam = Phase == EKnockdownPhase::Slamming;
	Phase = EKnockdownPhase::Landing;
	SlamReactionTask.Reset();
	if (bAfterSlam)
	{
		BeginHold();
		return;
	}

	if (!Set || !ActiveClips.Land)
	{
		BeginHold();
		return;
	}

	UAbilityTask_PlayMontageAndWait* Land = MakeMontageTask(ActiveClips.Land);
	Land->OnBlendOut.AddDynamic(this, &ThisClass::OnLandEnded);
	Land->OnInterrupted.AddDynamic(this, &ThisClass::OnLandEnded);
	Land->OnCancelled.AddDynamic(this, &ThisClass::OnLandEnded);
	Land->ReadyForActivation();
}

void UVBGA_Knockdown::OnLandEnded()
{
	if (Phase != EKnockdownPhase::Landing)
	{
		return;
	}

	BeginHold();
}

void UVBGA_Knockdown::OnAirHitEnded()
{
	if (Phase != EKnockdownPhase::Airborne)
	{
		return;
	}

	bAirHitPlaying = false;
	PlayAirLoop();
}

void UVBGA_Knockdown::BeginHold()
{
	const FVBKnockdownSet* Set = GetKnockdownSet();
	UWorld* World = GetWorld();
	Phase = EKnockdownPhase::Hold;
	bHoldExpired = false;
	HoldStartTime = World ? World->GetTimeSeconds() : 0.0;
	if (!Set || !World)
	{
		BeginGetUp();
		return;
	}
	// 0 초는 바로 일어난다는 뜻이다. SetTimer 는 0 이하를 안 거는 것으로 읽으므로 아주 짧은 양수로 바꿔 한 번은 울리게 한다.
	// 넘어짐을 다운 피격이 끊었으면 그 남은 클립 시간을 더한다 - 때린다고 전체 다운이 짧아지지 않게(FallHitAcceptSeconds 주석).
	World->GetTimerManager().SetTimer(HoldTimer, this, &ThisClass::OnHoldExpired,
		FMath::Max(Set->HoldSecondsFor(ActiveClips, bArrivedByLaunch) + FallTimeCutShort, 0.01f), false);
	if (DownCounter.Arm(*Set))
	{
		World->GetTimerManager().SetTimer(DownCounter.WatchTimer, this, &ThisClass::TickDownAttackWatch, Set->DownAttackWatchSeconds, true);
	}
	// 넘어짐을 끊은 다운 피격이 아직 재생 중이면 대기 클립을 틀지 않는다 - 그 피격이 끝날 때 ResumeAfterInterlude 가 튼다.
	if (!bDownHitPlaying)
	{
		PlayDownIdle();
	}
}

void UVBGA_Knockdown::PlayDownIdle()
{
	const FVBKnockdownSet* Set = GetKnockdownSet();
	if (!Set || !ActiveClips.DownIdle)
	{
		return;
	}
	// 대기가 이미 온전히 돌고 있으면 다시 틀지 않는다 - 반격 블렌드아웃과 반격 종료처럼 이어 달리기가 두 번 올 때 대기를 첫 프레임으로 되감지 않게.
	if (const UAnimInstance* AnimInstance = GetActorInfo().GetAnimInstance())
	{
		const FAnimMontageInstance* Running = AnimInstance->GetActiveInstanceForMontage(ActiveClips.DownIdle);
		if (Running && !Running->IsStopped())
		{
			return;
		}
	}

	UAbilityTask_PlayMontageAndWait* Idle = MakeMontageTask(ActiveClips.DownIdle);
	// Interrupted 는 안 건다 - 다운 피격, 반격 몽타주가 대기를 덮는 것이 정상 경로고, 그 몽타주가 끝날 때 대기를 다시 튼다.
	Idle->OnBlendOut.AddDynamic(this, &ThisClass::OnDownIdleEnded);
	Idle->ReadyForActivation();
}

void UVBGA_Knockdown::OnDownIdleEnded()
{
	// 대기 태스크는 BlendOut 만 받으므로 여기 오는 것은 제 끝에서 빠지는 경우뿐이다(남이 밀어내면 Interrupted 라 안 온다).
	//   반격 예고 중이면 반격 몽타주가 아직 안 틀렸으니 대기를 다시 걸어 메시 포즈를 덮는다 - ResumeAfterInterlude 는 반격 중이면 아무것도 안 튼다.
	if (Phase == EKnockdownPhase::Countering)
	{
		PlayDownIdle();
		return;
	}

	ResumeAfterInterlude();
}

void UVBGA_Knockdown::OnHoldExpired()
{
	bHoldExpired = true;
	ResumeAfterInterlude();
}

void UVBGA_Knockdown::ResumeAfterInterlude()
{
	if (Phase != EKnockdownPhase::Hold || bDownHitPlaying)
	{
		return;
	}

	if (!bHoldExpired)
	{
		PlayDownIdle();
		return;
	}
	// 빠지는 중인 몽타주 바로 뒤에 기상을 틀면 두 무게 합이 1 아래로 내려가 서 있는 자세가 섞여 쓰러진 메시가 뜬다.
	//   대기가 온전히 돌 때(IsStopped 아님)만 바로 기상하고, 아니면 대기를 먼저 걸고(즉시 무게 1) 다음 틱에 기상한다.
	const FVBKnockdownSet* Set = GetKnockdownSet();
	const UAnimInstance* AnimInstance = GetActorInfo().GetAnimInstance();
	const FAnimMontageInstance* IdleInstance = (Set && ActiveClips.DownIdle && AnimInstance)
		? AnimInstance->GetActiveInstanceForMontage(ActiveClips.DownIdle) : nullptr;
	if (!Set || !ActiveClips.DownIdle || (IdleInstance && !IdleInstance->IsStopped()))
	{
		BeginGetUp();
		return;
	}

	PlayDownIdle();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			// 그 한 틱 사이에 다운 피격, 반격이 끼어들었으면 기상은 그것이 끝날 때 다시 묻는다(ResumeAfterInterlude).
			if (IsActive() && Phase == EKnockdownPhase::Hold && !bDownHitPlaying)
			{
				BeginGetUp();
			}
		}));
	}
}

void UVBGA_Knockdown::OnHitEventReceived(FGameplayEventData Payload)
{
	// 반격 중만 반응 없음. 전신 반응을 틀 수 있으면 방향별 다운 피격을 새로 틀고(앞 반응이 재생 중이어도), 아니면 가산 플린치를 얹는다.
	//   버리는 타격은 모두 이유를 로그로 남긴다 - 왜 반응이 없었는지 로그로 답할 수 있게.
	const FVBKnockdownSet* Set = GetKnockdownSet();
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Set)
	{
		return;
	}

	if (Phase == EKnockdownPhase::Countering)
	{
		VB_LOG(Log, "[다운] %s 반격 중 피격 - 반응 없음", *GetNameSafe(Avatar));
		return;
	}

	const EVBHitDirection Direction = VBComputeHitDirection(Avatar, Payload.Instigator.Get());
	if (Phase == EKnockdownPhase::Airborne)
	{
		PlayAirHit(*Set, Direction);
		return;
	}

	if (Phase == EKnockdownPhase::None)
	{
		return;
	}

	float FallPosition = -1.0f;
	if (const TCHAR* FlinchSituation = ResolveFlinchSituation(*Set, FallPosition))
	{
		PlayFlinch(Direction, FlinchSituation);
		return;
	}

	PlayDownHit(*Set, Direction, FallPosition);
}

const TCHAR* UVBGA_Knockdown::ResolveFlinchSituation(const FVBKnockdownSet& Set, float& OutFallPosition) const
{
	// 전신 반응을 못 트는 구간. 넘어짐 앞부분, 기상은 클립이 스스로 자세를 바꾸는 중이라 갈아 끼우면 튀고(실측 50~120cm),
	//   대기 시간이 끝난 뒤에 새로 틀면 계속 때려 영원히 눕힐 수 있다 - 대기 시간이 영구 경직의 뚜껑이다.
	switch (Phase)
	{
	case EKnockdownPhase::Falling:
		{
			const UAnimInstance* AnimInstance = GetActorInfo().GetAnimInstance();
			OutFallPosition = AnimInstance ? AnimInstance->Montage_GetPosition(ActiveClips.Fall) : -1.0f;
			if (Set.FallHitAcceptSeconds < 0.0f || OutFallPosition < Set.FallHitAcceptSeconds)
			{
				return TEXT("넘어지는 중");
			}

			return nullptr;
		}
	case EKnockdownPhase::Hold:
		return bHoldExpired ? TEXT("대기 시간이 끝난 뒤") : nullptr;
	case EKnockdownPhase::GettingUp:
		return TEXT("일어나는 중");
	case EKnockdownPhase::Landing:
		return TEXT("착지하는 중");
	case EKnockdownPhase::Slamming:
		return TEXT("air slam descent");
	default:
		return nullptr;
	}
}

void UVBGA_Knockdown::PlayAirHit(const FVBKnockdownSet& Set, EVBHitDirection Direction)
{
	// 떠 있는 동안: 공중 피격 클립을 새로 틀고 끝나면 체공 반복으로. 더 띄우지 않는다(저글은 공중 전투 설계 때).
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	UAnimMontage* AirHit = Set.AirHitSet.ResolveByDirection(Direction);
	if (!AirHit)
	{
		VB_LOG(Log, "[다운] %s 공중 피격 %s - AirHitSet 미저작, 반응 없음", *GetNameSafe(Avatar), *UEnum::GetValueAsString(Direction));
		return;
	}

	bAirHitPlaying = true;
	HoldUpAfterAirHit(Set);
	UAbilityTask_PlayMontageAndWait* Hit = MakeMontageTask(AirHit);
	// BlendOut 만 건다 - 다음 공중 피격이나 착지가 밀어내면(Interrupted) 그쪽이 메시 포즈를 덮는다.
	Hit->OnBlendOut.AddDynamic(this, &ThisClass::OnAirHitEnded);
	Hit->ReadyForActivation();
	VB_LOG(Log, "[다운] %s 공중 피격 %s -> %s", *GetNameSafe(Avatar), *UEnum::GetValueAsString(Direction), *GetNameSafe(AirHit));
}

void UVBGA_Knockdown::PlayDownHit(const FVBKnockdownSet& Set, EVBHitDirection Direction, float FallPosition)
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	UAnimMontage* Montage = Set.DownHitSet.ResolveByDirection(Direction);
	if (!Montage)
	{
		VB_LOG(Log, "[다운] %s 다운 피격 %s - DownHitSet 미저작, 반응 없음", *GetNameSafe(Avatar), *UEnum::GetValueAsString(Direction));
		return;
	}

	if (Phase == EKnockdownPhase::Falling && ActiveClips.Fall)
	{
		// 넘어짐을 여기서 끊는다. 남은 클립 시간은 대기에 더한다(BeginHold). 넘어짐 태스크는 아래 피격 몽타주가 넘어짐을 덮을 때
		//   OnInterrupted 로 OnFallEnded -> BeginHold 에 온다 - 그때 bDownHitPlaying 이 서 있어 대기 클립을 틀지 않는다.
		const float RateScale = FMath::Max(ActiveClips.Fall->RateScale * (HitReactConfig ? HitReactConfig->MontagePlayRate : 1.0f),
			KINDA_SMALL_NUMBER);
		FallTimeCutShort = FMath::Max(0.0f, (ActiveClips.Fall->GetPlayLength() - FallPosition) / RateScale);
	}
	// 앞 반응이 재생 중이면 갈아 끼운다. 앞 태스크의 종료 알림을 먼저 떼어 낸다 - 새 몽타주가 앞 몽타주를 밀어내며 앞 태스크가
	//   Interrupted 를 부르면 대기가 틀려 방금 튼 반응을 덮는다. 앞 몽타주 자체는 새 몽타주가 교차 블렌드로 밀어낸다(같은 그룹, 둘 다 무게 1 이라 합 1).
	const bool bReplacing = bDownHitPlaying;
	if (UAbilityTask_PlayMontageAndWait* Previous = DownHitTask.Get())
	{
		Previous->OnBlendOut.RemoveAll(this);
		Previous->OnInterrupted.RemoveAll(this);
		Previous->OnCancelled.RemoveAll(this);
		Previous->EndTask();
	}

	VB_LOG(Log, "[다운] %s 다운 피격 %s -> %s%s%s", *GetNameSafe(Avatar), *UEnum::GetValueAsString(Direction), *GetNameSafe(Montage),
		Phase == EKnockdownPhase::Falling ? *FString::Printf(TEXT(" (넘어짐 %.2f초에서 끊음, 대기 +%.2f초)"), FallPosition,
			FallTimeCutShort) : TEXT(""),
		bReplacing ? TEXT(" (앞 반응을 갈아 끼움)") : TEXT(""));
	bDownHitPlaying = true;
	UAbilityTask_PlayMontageAndWait* Hit = MakeMontageTask(Montage);
	Hit->OnBlendOut.AddDynamic(this, &ThisClass::OnDownHitEnded);
	Hit->OnInterrupted.AddDynamic(this, &ThisClass::OnDownHitInterrupted);
	Hit->OnCancelled.AddDynamic(this, &ThisClass::OnDownHitInterrupted);
	DownHitTask = Hit;
	Hit->ReadyForActivation();
}

void UVBGA_Knockdown::OnDownHitEnded()
{
	// 제 끝에서 빠지기 시작했다 - 이 뒤를 덮는 것은 우리 몫이다. 반격 예고 중이면(반격 몽타주가 아직 안 틀림) 대기로 덮는다.
	//   ResumeAfterInterlude 는 반격 중이면 아무것도 안 트므로 여기서 따로 건다. 안 덮으면 예고 동안 서 있는 자세가 샌다.
	bDownHitPlaying = false;
	if (Phase == EKnockdownPhase::Countering)
	{
		PlayDownIdle();
		return;
	}

	ResumeAfterInterlude();
}

void UVBGA_Knockdown::OnDownHitInterrupted()
{
	// 다른 몽타주(반격 휘두르기 등)가 밀어냈다 - 메시 포즈는 그 몽타주가 덮고 있다. 반격이면 그것이 끝날 때 대기로 돌아온다.
	bDownHitPlaying = false;
	ResumeAfterInterlude();
}

void UVBGA_Knockdown::PlayFlinch(EVBHitDirection Direction, const TCHAR* Situation)
{
	AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(GetAvatarActorFromActorInfo());
	UAnimMontage* Montage = HitReactConfig ? HitReactConfig->FlinchSet.ResolveByDirection(Direction) : nullptr;
	if (!Montage)
	{
		VB_LOG(Log, "[다운] %s %s 피격 %s - 반응 없음 (FlinchSet 미저작)", *GetNameSafe(Enemy), Situation, *UEnum::GetValueAsString(Direction));
		return;
	}

	if (!Enemy)
	{
		// 이 GA 는 적에게만 붙는다(ExecuteAbility 의 서버 전용 전제와 같은 줄). 적이 아니면 실어 보낼 멀티캐스트가 없다.
		VB_LOG(Warning, "[다운] %s 는 적이 아니다 - 플린치를 실어 보낼 곳이 없다", *GetNameSafe(GetAvatarActorFromActorInfo()));
		return;
	}
	// 가산이 아니면 전신 포즈를 덮어 넘어짐, 기상을 가린다. 막을 수 있으니 막는다(자산 실수가 보스가 순간 서 있는 모습으로 보이지 않게).
	if (!Montage->IsValidAdditive())
	{
		VB_LOG(Warning, "[다운] %s FlinchSet 의 %s 가 가산 몽타주가 아니다 - 틀면 %s 동작을 덮는다. 재생 안 함",
			*GetNameSafe(Enemy), *GetNameSafe(Montage), Situation);
		return;
	}
	// ASC 를 거치지 않는다: ASC 로 틀면 능력의 현재 몽타주가 플린치로 바뀌어 기상 태스크의 블렌드아웃 판정이 어긋난다.
	//   그래서 ASC 몽타주 복제 대신 적의 멀티캐스트로 실어 보낸다(리슨 호스트 자신도 받는다). 앞 플린치 정지, 그룹 정지 끔은 거기서 한다.
	Enemy->MulticastPlayAdditiveFlinch(Montage);
	VB_LOG(Log, "[다운] %s %s 피격 %s -> 가산 플린치 %s", *GetNameSafe(Enemy), Situation, *UEnum::GetValueAsString(Direction),
		*GetNameSafe(Montage));
}

void UVBGA_Knockdown::TickDownAttackWatch()
{
	// 다운 피격 재생 중에도 본다. 피격마다 반응을 새로 트니 연타 중에는 반응 사이 빈틈이 없고, 빈틈을 기다리면 반격이 영영 안 나온다.
	//   반격 몽타주가 피격 몽타주를 밀어낸다(OnDownHitInterrupted). 대기 시간이 끝난 뒤에는 안 켠다 - 켜면 그만큼 눕는 시간이 늘어난다.
	if (Phase != EKnockdownPhase::Hold || bHoldExpired)
	{
		return;
	}

	const FVBKnockdownSet* Set = GetKnockdownSet();
	const UWorld* World = GetWorld();
	if (!Set || !World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	if (!DownCounter.IsAttemptDue(Now))
	{
		return;
	}

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	const UVBEnemyCombatComponent* Combat = Avatar ? Avatar->FindComponentByClass<UVBEnemyCombatComponent>() : nullptr;
	const AActor* Target = Combat ? Combat->GetCombatTarget() : nullptr;
	if (!Target)
	{
		return;
	}
	// 반격도 공격이다 - 어택 토큰 없이 켜면 코디네이터가 경보를 내고 남의 다음 공격을 미룬다. 토큰이 없으면 반격도 없다.
	if (!Combat->HasAttackToken())
	{
		return;
	}
	const float Distance = FVector::Dist2D(Avatar->GetActorLocation(), Target->GetActorLocation());
	const float TriggerRadius = DownCounter.GetTriggerRadius(VBStrideScale::Resolve(Avatar));
	if (Distance > TriggerRadius)
	{
		return;
	}

	if (TryStartDownAttack(*Set, Now))
	{
		VB_LOG(Log, "[다운] %s 반격 - 표적 거리 %.0f (반경 %.0f)", *GetNameSafe(Avatar), Distance, TriggerRadius);
	}
}

bool UVBGA_Knockdown::TryStartDownAttack(const FVBKnockdownSet& Set, double Now)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return false;
	}

	DownCounter.MarkAttempt(Now);

	// BT 태스크(UVBBTTask_ActivateAbility)와 같은 함수로 스펙을 찾는다 - 태그로 GA 를 고르는 홈은 하나다.
	const FGameplayAbilitySpec* Spec = UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(ASC, DownCounter.GetInputTag());
	if (!Spec)
	{
		VB_LOG(Warning, "[다운] %s 반격 태그 %s 에 맞는 GA 가 없다 - 보스 BP 의 AbilitySet 에 반격 GA 를 넣어라",
			*GetNameSafe(GetAvatarActorFromActorInfo()), *DownCounter.GetInputTag().ToString());
		return false;
	}

	DownCounter.WatchAbilityEnd(*ASC, Spec->Handle, FGameplayAbilityEndedDelegate::FDelegate::CreateUObject(this, &ThisClass::OnAnyAbilityEnded));
	// 반격 몽타주의 블렌드아웃 시작도 받는다 - 능력 종료는 그 0.25초 뒤라 그때 대기를 걸면 이음매가 빈다(OnAnyMontageBlendingOut).
	if (UAnimInstance* AnimInstance = GetActorInfo().GetAnimInstance())
	{
		AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &ThisClass::OnAnyMontageBlendingOut);
		DownCounter.NoteAnimSubscription(*AnimInstance);
	}

	// 활성화 앞에 바꾼다. TryActivateAbility 안에서 GA 가 동기로 끝나면 그 종료가 OnAnyAbilityEnded 로 들어와 대기를 되살려야 한다.
	Phase = EKnockdownPhase::Countering;
	if (!ASC->TryActivateAbility(Spec->Handle))
	{
		// 활성화 안에서 이 능력이 끝났으면 단계가 이미 바뀌어 있다.
		if (Phase == EKnockdownPhase::Countering)
		{
			Phase = EKnockdownPhase::Hold;
		}

		VB_LOG(Log, "[다운] %s 반격 활성화 거부(게이트·토큰)", *GetNameSafe(GetAvatarActorFromActorInfo()));
		return false;
	}

	return true;
}

void UVBGA_Knockdown::OnAnyMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	// 반격이 제 끝에서 빠지기 시작한 순간이 대기가 들어갈 자리다 - 능력 종료까지 기다리면 그 사이 서 있는 자세가 드러난다.
	//   끊긴 반격은 끊은 몽타주가 메시 포즈를 덮으므로 OnAnyAbilityEnded 로 충분하다.
	const FVBKnockdownSet* Set = GetKnockdownSet();
	if (Phase != EKnockdownPhase::Countering || bInterrupted || !Montage || !Set)
	{
		return;
	}
	// 플린치도 우리 것이다 - 반격 직전에 틀린 플린치가 제 끝에서 빠지는 것을 반격 끝으로 읽으면 휘두르는 중에 대기가 덮는다.
	if (HitReactConfig->IsDownReaction(*Set, ActiveClips, Montage))
	{
		return;
	}
	// 반격 능력은 아직 살아 있다(몽타주 완료 때 끝난다). 뒤늦은 종료 알림은 OnAnyAbilityEnded 첫 줄에서 걸러진다.
	FinishCounter();
}

void UVBGA_Knockdown::OnAnyAbilityEnded(const FAbilityEndedData& EndedData)
{
	if (Phase == EKnockdownPhase::Countering && DownCounter.IsCounterAbility(EndedData.AbilitySpecHandle))
	{
		FinishCounter();
	}
}

void UVBGA_Knockdown::FinishCounter()
{
	Phase = EKnockdownPhase::Hold;
	ResumeAfterInterlude();
}

void UVBGA_Knockdown::BeginGetUp(bool bEscaping)
{
	const FVBKnockdownSet* Set = GetKnockdownSet();
	Phase = EKnockdownPhase::GettingUp;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HoldTimer);
		World->GetTimerManager().ClearTimer(DownCounter.WatchTimer);
	}

	UAnimMontage* GetUpMontage = !Set ? nullptr : (bEscaping ? Set->EscapeGetUpMontage.Get() : ActiveClips.GetUpFor(bArrivedByLaunch));
	if (!GetUpMontage)
	{
		// IsAuthored 가 진입을 막으므로 여기 오는 길은 없다. 오면 자세는 틀리되 상태는 닫힌다.
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
		return;
	}

	UAbilityTask_PlayMontageAndWait* GetUp = MakeMontageTask(GetUpMontage);
	GetUp->OnBlendOut.AddDynamic(this, &ThisClass::OnGetUpEnded);
	GetUp->OnInterrupted.AddDynamic(this, &ThisClass::OnGetUpEnded);
	GetUp->OnCancelled.AddDynamic(this, &ThisClass::OnGetUpEnded);
	GetUp->ReadyForActivation();
}

void UVBGA_Knockdown::InputPressed(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputPressed(Handle, ActorInfo, ActivationInfo);
	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		TryEscape();
	}
}

bool UVBGA_Knockdown::TryEscape()
{
	const FVBKnockdownSet* Set = GetKnockdownSet();
	const UWorld* World = GetWorld();
	const double Held = World ? World->GetTimeSeconds() - HoldStartTime : 0.0;
	if (!IsActive() || !Set || !Set->CanEscape(Phase == EKnockdownPhase::Hold, Held))
	{
		return false;
	}

	VB_LOG(Log, "[다운] %s 회피로 빠져나온다 (엎드린 %.2f초)", *GetNameSafe(GetAvatarActorFromActorInfo()), Held);
	BeginGetUp(/*bEscaping=*/true);
	return true;
}

void UVBGA_Knockdown::OnGetUpEnded()
{
	if (Phase != EKnockdownPhase::GettingUp)
	{
		return;
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
