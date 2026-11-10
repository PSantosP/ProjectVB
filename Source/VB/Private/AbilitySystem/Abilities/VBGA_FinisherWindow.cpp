// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_FinisherWindow.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h" // ShouldReplicateActivationOwnedTags - 창 태그를 엔진과 같은 복제 설정으로 붙이고 뗀다
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayTag.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBFinisherComponent.h"
#include "ContextualAnimSceneActorComponent.h"
#include "Data/VBFinisherWindowConfig.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Animation/AnimInstance.h"         // PlayWaitPose - 이미 도는 자세는 다시 틀지 않는다
#include "Components/SkeletalMeshComponent.h"
#include "VB/VB.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBGA_FinisherWindow"

namespace
{
	// 누움, 기상 태그가 걷혔는지 다시 보는 간격(초). Downed, GettingUp 순서가 GA 마다 달라 태그 이벤트 대신 재확인을 쓴다.
	constexpr float LyingRecheckSeconds = 0.1f;
	// 처형 장면이 끝났는지 다시 보는 간격(초). 장면 시작 시각을 몰라 창 길이로는 막지 못한다.
	constexpr float SceneRecheckSeconds = 0.25f;
}

UVBGA_FinisherWindow::UVBGA_FinisherWindow()
{
	// 입력 GA 가 아니다. 두 사건을 다 듣고 어느 쪽을 받을지는 설정이 정한다(ShouldAbilityRespondToEvent).
	FAbilityTriggerData PoiseTrigger;
	PoiseTrigger.TriggerTag = VBGameplayTags::Event_Combat_PoiseBroken;
	AbilityTriggers.Add(PoiseTrigger);
	FAbilityTriggerData HealthTrigger;
	HealthTrigger.TriggerTag = VBGameplayTags::Event_Combat_FinisherWindow;
	AbilityTriggers.Add(HealthTrigger);
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Combat_FinisherWindow));

	// 켜짐(대기 + 열림)은 엔진이 붙이고 뗀다. 열림 태그는 OpenWindow 가 손으로 붙인다 - 누운 대기 중에 처형이 걸리지 않게.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_FinisherWindowArmed);

	// 창 중의 추가 사건이 창을 다시 열지 않는다 - 다시 열면 사실상 영구 무력화가 된다.
	bRetriggerInstancedAbility = false;
}

void UVBGA_FinisherWindow::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);
	UAbilitySystemComponent* OwnerASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	// 설정이 없으면 어느 사건으로도 안 열린다 - 기본값으로 메우지 않고 부여 순간에 크게 말한다.
	if (!FinisherWindowConfig || !OwnerASC)
	{
		VB_LOG(Error, "%s: 처형 창 부여 실패 - %s. 이 적은 처형 창이 열리지 않는다",
			*GetName(), !FinisherWindowConfig ? TEXT("FinisherWindowConfig 미할당") : TEXT("ASC 가 아직 없다(InitAbilityActorInfo 전 부여)"));
		return;
	}
	// 기절 게이지로 여는 적이라는 사실의 홈. 경직 GA 와 피해 허브는 이 태그만 본다. 부여는 서버, 클라가 각자 하므로 복제하지 않는다.
	if (FinisherWindowConfig->OpenOn == EVBFinisherWindowOpenOn::PoiseBreak && !bPostureTagGranted)
	{
		OwnerASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_PostureExecutable, 1, EGameplayTagReplicationState::None);
		bPostureTagGranted = true;
	}
}

void UVBGA_FinisherWindow::OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	if (bPostureTagGranted)
	{
		if (UAbilitySystemComponent* OwnerASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
		{
			OwnerASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_PostureExecutable, 1, EGameplayTagReplicationState::None);
		}

		bPostureTagGranted = false;
	}

	Super::OnRemoveAbility(ActorInfo, Spec);
}

#if WITH_EDITOR
EDataValidationResult UVBGA_FinisherWindow::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	// 네이티브 추상 클래스 자신은 저작 대상이 아니다 - 저작된 파생(BP)만 본다.
	if (!GetClass()->HasAnyClassFlags(CLASS_Abstract) && !FinisherWindowConfig)
	{
		Context.AddError(LOCTEXT("VBFinisherWindow_NoConfig",
			"FinisherWindowConfig 가 비었습니다. 이 창은 어느 사건으로도 열리지 않습니다."));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif

bool UVBGA_FinisherWindow::ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* /*ActorInfo*/,
	const FGameplayEventData* Payload) const
{
	// 설정이 없으면 열지 않는다 - 쿨다운을 태우기 전에 막는다.
	if (!Payload || !FinisherWindowConfig)
	{
		return false;
	}

	if (Payload->EventTag == VBGameplayTags::Event_Combat_PoiseBroken)
	{
		return FinisherWindowConfig->OpenOn == EVBFinisherWindowOpenOn::PoiseBreak;
	}
	// 체력 사건의 EventMagnitude 는 남은 체력 비율이다(VBDamageResponse 가 싣는다).
	return Payload->EventTag == VBGameplayTags::Event_Combat_FinisherWindow
		&& FinisherWindowConfig->OpenOn == EVBFinisherWindowOpenOn::HealthThreshold
		&& Payload->EventMagnitude <= FinisherWindowConfig->HealthThreshold;
}

void UVBGA_FinisherWindow::ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// InstancedPerActor 라 인스턴스가 액터 수명 내내 산다 - 활성화마다 상태를 비운다.
	bDeferredForScene = false;
	bWindowOpen = false;
	bImmuneTagged = false;
	bReactionWatchLive = false;

	// 데이터 홈 미할당은 설정 오류다. 기본값으로 열면 「문턱이 어디인지 아무도 모르는 창」이 생긴다.
	if (!FinisherWindowConfig)
	{
		VB_LOG(Warning, "%s: FinisherWindowConfig 미할당 -> 창을 열지 않는다. "
			"BP_GA_FinisherWindow 에 설정 자산을 할당하라", *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 사망 감시. 처형 성공 뒤 창이 남으면 시체가 두 번째 처형을 받는다. 살아남는 처형은 창을 닫으면 안 되므로 죽음만 본다.
	// 맨 앞에 둔다 - 이미 죽어 있으면 엔진이 그 자리에서 Added 를 쏜다(아래 IsActive 가드).
	UAbilityTask_WaitGameplayTagAdded* DeathWatch =
		UAbilityTask_WaitGameplayTagAdded::WaitGameplayTagAdd(
			this, VBGameplayTags::State_Dead, /*InOptionalExternalTarget=*/nullptr, /*OnlyTriggerOnce=*/true);
	DeathWatch->Added.AddDynamic(this, &UVBGA_FinisherWindow::OnOwnerDied);
	DeathWatch->ReadyForActivation();

	if (!IsActive())
	{
		return;
	}

	// 체력 문턱 창은 처형 아닌 체력 피해를 받지 않는다(서버 판정, 비복제). 게이지 창은 일반 타격을 받는다.
	if (FinisherWindowConfig->OpenOn == EVBFinisherWindowOpenOn::HealthThreshold)
	{
		if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
		{
			OwnerASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindowImmune, 1, EGameplayTagReplicationState::None);
			bImmuneTagged = true;
		}
	}

	// 쓰러진 캐릭터면 일어날 때까지 기다린다(헤더 주석). 서 있는 캐릭터는 반응, 공격 중이어도 지금 연다 - 여는 쪽이 그것들을 끊는다.
	if (IsOwnerLying())
	{
		VB_LOG(Log, "처형 창 대기(누움·기상 중): %s - 일어나면 연다", *GetNameSafe(GetAvatarActorFromActorInfo()));
		LyingWaitStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		OnLyingRecheck();
		return;
	}

	OpenWindow();
}

bool UVBGA_FinisherWindow::IsOwnerLying() const
{
	const UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo();
	if (!OwnerASC)
	{
		return false;
	}
	// 쓰러진 상태(Downed)나 일어나는 중(GettingUp)에는 선 자세 대기, 처형 장면을 걸 수 없다.
	FGameplayTagContainer LyingTags;
	LyingTags.AddTag(VBGameplayTags::State_Combat_Downed);
	LyingTags.AddTag(VBGameplayTags::State_Combat_GettingUp);
	return OwnerASC->HasAnyMatchingGameplayTags(LyingTags);
}

void UVBGA_FinisherWindow::OnLyingRecheck()
{
	if (!IsActive())
	{
		return;
	}

	if (!IsOwnerLying())
	{
		OpenWindow();
		return;
	}
	// 상한. 설정 시간을 초과하면 창을 포기하고 경고한다 - 조용히 기다리면 그 적만 안 죽는다.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now - LyingWaitStartTime > FinisherWindowConfig->MaxReactionWaitSeconds)
	{
		VB_LOG(Warning, "처형 창 포기: %s - %.1f초 동안 누움·기상 태그가 안 걷혔다(새는 태그를 의심하라)",
			*GetNameSafe(GetAvatarActorFromActorInfo()), FinisherWindowConfig->MaxReactionWaitSeconds);
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	UAbilityTask_WaitDelay* Recheck = UAbilityTask_WaitDelay::WaitDelay(this, LyingRecheckSeconds);
	Recheck->OnFinish.AddDynamic(this, &UVBGA_FinisherWindow::OnLyingRecheck);
	Recheck->ReadyForActivation();
}

void UVBGA_FinisherWindow::OpenWindow()
{
	if (bWindowOpen)
	{
		return;
	}

	bWindowOpen = true;

	// 창 태그. 붙는 순간 전투 규칙 표가 실행 중인 공격과 반응을 취소한다. 복제는 ActivationOwnedTags 설정을 따른다.
	if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
	{
		OwnerASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindow, 1,
			UAbilitySystemGlobals::Get().ShouldReplicateActivationOwnedTags()
				? EGameplayTagReplicationState::CountToOwner : EGameplayTagReplicationState::None);
	}

	InteractionStateHandle = UVBFinisherComponent::OnInteractionParticipantChanged.AddUObject(
		this, &UVBGA_FinisherWindow::OnInteractionParticipantChanged);
	ObservedLegacyScene = UVBFinisherComponent::FindSceneActorComponent(GetAvatarActorFromActorInfo());
	if (UContextualAnimSceneActorComponent* Scene = ObservedLegacyScene.Get())
	{
		Scene->OnJoinedSceneDelegate.AddUniqueDynamic(this, &UVBGA_FinisherWindow::OnLegacySceneChanged);
		Scene->OnLeftSceneDelegate.AddUniqueDynamic(this, &UVBGA_FinisherWindow::OnLegacySceneChanged);
	}
	RefreshWindowCue();

	// 속도와 가속을 지운다. 「지금 멈춤」이지 「못 움직임」이 아니다 - 경계는 설정 자산의 주석에 적어 두었다.
	if (FinisherWindowConfig->bStopMovementOnOpen)
	{
		if (const ACharacter* AvatarCharacter = GetAvatarCharacter())
		{
			if (UCharacterMovementComponent* MoveComp = AvatarCharacter->GetCharacterMovement())
			{
				MoveComp->StopMovementImmediately();
			}
		}
	}

	// 대기 자세(미할당이면 생략). 창의 길이는 WindowDuration 하나가 정하고 몽타주 끝과 묶지 않는다.
	PlayWaitPose();
	// 창 동안 반응이 자세를 끊으면 다시 튼다. 감시를 거는 순간의 동기 알림은 무시한다(bReactionWatchLive).
	UAbilityTask_WaitGameplayTagRemoved* ReactionWatch =
		UAbilityTask_WaitGameplayTagRemoved::WaitGameplayTagRemove(this, VBGameplayTags::State_Combat_HitReacting,
			nullptr, /*OnlyTriggerOnce=*/false);
	ReactionWatch->Removed.AddDynamic(this, &UVBGA_FinisherWindow::OnHitReactionEnded);
	UAbilityTask_WaitGameplayTagRemoved* GetUpWatch =
		UAbilityTask_WaitGameplayTagRemoved::WaitGameplayTagRemove(this, VBGameplayTags::State_Combat_GettingUp,
			nullptr, /*OnlyTriggerOnce=*/false);
	GetUpWatch->Removed.AddDynamic(this, &UVBGA_FinisherWindow::OnHitReactionEnded);
	bReactionWatchLive = false;
	ReactionWatch->ReadyForActivation();
	GetUpWatch->ReadyForActivation();
	bReactionWatchLive = true;

	// 처형할 수 있다는 사실을 게임 사건으로 알린다 - 튜토리얼, 퀘스트가 듣는다(전투 코드는 안내를 모른다).
	if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(GetAvatarActorFromActorInfo()))
	{
		FVBGameEvent Event;
		Event.EventTag = VBGameplayTags::Event_Combat_ExecuteAvailable;
		Events->Broadcast(Event);
	}

	UAbilityTask_WaitDelay* WindowTimer =
		UAbilityTask_WaitDelay::WaitDelay(this, FinisherWindowConfig->WindowDuration);
	WindowTimer->OnFinish.AddDynamic(this, &UVBGA_FinisherWindow::OnWindowExpired);
	WindowTimer->ReadyForActivation();

	VB_LOG(Log, "처형 창 개방: %s (%.1f초)", *GetNameSafe(GetAvatarActorFromActorInfo()),
		FinisherWindowConfig->WindowDuration);
}

void UVBGA_FinisherWindow::RefreshWindowCue()
{
	const bool bShow = IsActive() && bWindowOpen &&
		!UVBFinisherComponent::IsActorInScene(GetAvatarActorFromActorInfo());
	if (bShow == bWindowCueActive)
	{
		return;
	}

	bWindowCueActive = bShow;
	if (bShow)
	{
		K2_AddGameplayCue(VBGameplayTags::GameplayCue_Combat_FinisherWindow, FGameplayEffectContextHandle(), true);
	}
	else
	{
		K2_RemoveGameplayCue(VBGameplayTags::GameplayCue_Combat_FinisherWindow);
	}
}

void UVBGA_FinisherWindow::OnInteractionParticipantChanged(AActor* Participant)
{
	if (Participant && Participant == GetAvatarActorFromActorInfo())
	{
		RefreshWindowCue();
	}
}

void UVBGA_FinisherWindow::OnLegacySceneChanged(UContextualAnimSceneActorComponent* Scene)
{
	if (Scene == ObservedLegacyScene.Get())
	{
		RefreshWindowCue();
	}
}

void UVBGA_FinisherWindow::PlayWaitPose()
{
	if (!FinisherWindowConfig || !FinisherWindowConfig->WaitMontage ||
		UVBFinisherComponent::IsActorInScene(GetAvatarActorFromActorInfo()))
	{
		return; // 미할당이 정상이다 - 이 경로에서도 창은 성립한다.
	}
	// 이미 그 자세가 돌고 있으면 다시 틀지 않는다 - 다시 틀면 자세가 처음부터 튄다.
	const ACharacter* AvatarCharacter = GetAvatarCharacter();
	const UAnimInstance* Anim = (AvatarCharacter && AvatarCharacter->GetMesh()) ? AvatarCharacter->GetMesh()->GetAnimInstance() : nullptr;
	if (Anim && Anim->Montage_IsPlaying(FinisherWindowConfig->WaitMontage))
	{
		return;
	}

	VB_LOG(Log, "처형 창 대기 자세: %s", *GetNameSafe(GetAvatarActorFromActorInfo()));
	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, FinisherWindowConfig->WaitMontage, 1.0f, NAME_None,
			/*bStopWhenAbilityEnds=*/true, 1.0f, 0.0f, false);
	MontageTask->ReadyForActivation();
}

void UVBGA_FinisherWindow::OnHitReactionEnded()
{
	// 쓰러진 캐릭터(넘어짐, 기상 중) 위에 선 대기 자세를 덮지 않는다 - 기상이 끝나면(GettingUp 해제) 이 함수가 다시 불린다.
	if (bReactionWatchLive && bWindowOpen && IsActive() && !IsOwnerLying())
	{
		PlayWaitPose();
	}
}

void UVBGA_FinisherWindow::OnWindowExpired()
{
	// 장면이 도는 동안에는 창을 닫지 않는다 - 닫히면 적 BT 의 이동, 공격 차단이 풀려 다리가 걷기로 돌아간다.
	if (UVBFinisherComponent::IsActorInScene(GetAvatarActorFromActorInfo()))
	{
		if (!bDeferredForScene)
		{
			bDeferredForScene = true;
			VB_LOG(Log, "처형 창 만료 보류(장면 진행 중): %s", *GetNameSafe(GetAvatarActorFromActorInfo()));
		}

		// 장면이 끝나거나 소유자가 죽으면 멈춘다.
		UAbilityTask_WaitDelay* Recheck = UAbilityTask_WaitDelay::WaitDelay(this, SceneRecheckSeconds);
		Recheck->OnFinish.AddDynamic(this, &UVBGA_FinisherWindow::OnWindowExpired);
		Recheck->ReadyForActivation();
		return;
	}

	// 처형당하지 않고 닫힌 정상 경로다. bWasCancelled=false - 취소가 아니라 만료다.
	VB_LOG(Log, "처형 창 만료: %s", *GetNameSafe(GetAvatarActorFromActorInfo()));
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UVBGA_FinisherWindow::OnOwnerDied()
{
	// 처형이 성사됐거나 다른 이유로 죽었다. 어느 쪽이든 창은 닫힌다.
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UVBGA_FinisherWindow::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	UVBFinisherComponent::OnInteractionParticipantChanged.Remove(InteractionStateHandle);
	InteractionStateHandle.Reset();
	if (UContextualAnimSceneActorComponent* Scene = ObservedLegacyScene.Get())
	{
		Scene->OnJoinedSceneDelegate.RemoveDynamic(this, &UVBGA_FinisherWindow::OnLegacySceneChanged);
		Scene->OnLeftSceneDelegate.RemoveDynamic(this, &UVBGA_FinisherWindow::OnLegacySceneChanged);
	}
	ObservedLegacyScene.Reset();
	bWindowCueActive = false;

	// 손으로 붙인 창 태그를 같은 복제 상태로 뗀다. 붙인 적이 없으면 떼지 않는다(개수가 음수가 된다).
	if (bWindowOpen)
	{
		if (UAbilitySystemComponent* OwnerASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
		{
			OwnerASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindow, 1,
				UAbilitySystemGlobals::Get().ShouldReplicateActivationOwnedTags()
					? EGameplayTagReplicationState::CountToOwner : EGameplayTagReplicationState::None);
		}

		bWindowOpen = false;
	}

	if (bImmuneTagged)
	{
		if (UAbilitySystemComponent* OwnerASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
		{
			OwnerASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindowImmune, 1, EGameplayTagReplicationState::None);
		}

		bImmuneTagged = false;
	}

	bReactionWatchLive = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

#undef LOCTEXT_NAMESPACE
