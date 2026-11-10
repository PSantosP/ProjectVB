// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_Execute.h"
#include "Components/SkeletalMeshComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"

#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h" // 처형 타격 창 노티 듣기
#include "AbilitySystemComponent.h"  // CancelAbilities - 진행 중인 내 공격 끊기
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/AnimMontage.h"        // GetUpMontage 를 UObject* 로 전달한다 - 공용 PCH 없이도 빌드되게
#include "Animation/VBEnemyAnimInstance.h"
#include "Character/VBFinisherComponent.h"
#include "Character/VBFinisherSelection.h"
#include "Data/VBFinisherConfig.h"
#include "Enemy/VBEnemyDeathPresentationComponent.h" // KeepMomentum - 던져진 시체는 운동량을 지우지 않는다
#include "Enemy/VBEnemyRagdollComponent.h"
#include "GameFramework/Actor.h"
#include "VB/VB.h" // VB_LOG

namespace
{
	// 옛 경로의 캡슐 보정이 메시가 그려진 자리로 읽는 본.
	const FName VictimRootBoneName(TEXT("root"));
}

UVBGA_Execute::UVBGA_Execute()
{
	InputTag = VBGameplayTags::Input_Execute;
	GoreCueTag = VBGameplayTags::GameplayCue_Damage_Physical;
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Medical_Execute));

	// 처형이 도는 동안 무엇을 막는지는 전투 규칙 표(DA_CombatRules)의 Ability.Medical.Execute 줄에 있다.

	// 「지금 처형을 걸고 있다」 태그(피해자 쪽 FinisherWindow 의 짝). 반응 GA 차단, 무적, 비-GA 진입점 질의가 이것 하나를 본다.
	// SuperArmor 를 재사용하지 않는다 - 적 공격 GA 가 이미 붙였다 떼어 관리 주체가 둘이 된다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Finishing);
	// 진입은 처형 키(InputTag) 하나다. 공격 버튼은 처형하지 않는다 - 창이 열린 적도 일반 공격으로 때릴 수 있어야 한다.
}

void UVBGA_Execute::ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 원격 클라는 아무것도 하지 않는다. Super 로 떨어지면 복제된 EndAbility 가 서버의 처형을 끝내 피해가 안 들어간다.
	// listen 호스트에서는 이 경로가 안 열려 PIE 로는 못 잡는다.
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return;
	}

	AActor* const Avatar = GetAvatarActorFromActorInfo();
	UVBFinisherComponent* Finisher = UVBFinisherComponent::FindFor(Avatar);
	// 활성화 판정과 같은 대상 규칙(이 능력의 설정) - 표시도 FindExecuteTarget 을 거쳐 이것을 부른다.
	AActor* const Target = Finisher ? FindMedicalTarget(ActorInfo) : nullptr;
	const UVBFinisherConfig* Config = nullptr;
	const EFinisherPath Path = Finisher ? PickFinisherPath(*Finisher, Avatar, Target, Config) : EFinisherPath::Instant;
	switch (Path)
	{
	case EFinisherPath::Refused:
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	case EFinisherPath::Interaction:
		// 참가자 재생 실패를 즉사/옛 연출로 바꾸지 않는다. 실패는 피해 없는 취소다.
		if (!StartInteractionScene(*Finisher, Target))
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		}

		return;
	case EFinisherPath::Contextual:
		// 어빌리티를 끝내지 않는다. 장면이 끝날 때 HandleFinisherEnded 가 마무리한다.
		if (StartContextualScene(*Finisher, Config, Target))
		{
			return;
		}

		break;
	case EFinisherPath::Instant:
		break;
	}

	// 연출이 없거나 못 걸린 경우 - 부모의 즉시 처치 그대로다. 자산이 없는 무기에서 기능이 사라지지 않는다.
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

UVBGA_Execute::EFinisherPath UVBGA_Execute::PickFinisherPath(const UVBFinisherComponent& Finisher, AActor* Avatar, AActor* Target,
	const UVBFinisherConfig*& OutConfig) const
{
	if (!Target)
	{
		VB_LOG(Log, "%s: 처형 대상 없음 - 락온 대상도 전방 원뿔도 «처형 창에 든 적» 을 못 찾았다",
			*GetName());
		return EFinisherPath::Instant;
	}

	if (UVBFinisherComponent::IsActorInScene(Target))
	{
		return EFinisherPath::Refused;
	}
	// 새 경로 배선이 있으면 그것을 먼저 쓰고, 없는 무기는 기존 contextual 경로를 쓴다.
	const FVBFinisherSelection& Selection = Finisher.GetSelection();
	if (Selection.HasInteractionFinisherData(Avatar))
	{
		const bool bCanStart = Selection.CanStartInteractionFinisher({Avatar, Target, Finisher.IsPlaying(),
			UVBFinisherComponent::IsActorInScene(Target)});
		return bCanStart ? EFinisherPath::Interaction : EFinisherPath::Refused;
	}

	OutConfig = Selection.SelectFinisher(Avatar, Target);
	if (!OutConfig)
	{
		VB_LOG(Log, "%s: %s 에 걸 처형이 없다 - 지금 무기의 FinisherConfigs 에 이 접근 방향 것이 없다. "
			"즉시 갈래로 간다", *GetName(), *GetNameSafe(Target));
		return EFinisherPath::Instant;
	}

	return EFinisherPath::Contextual;
}

bool UVBGA_Execute::StartInteractionScene(UVBFinisherComponent& Finisher, AActor* Target)
{
	CancelOwnAttacks();
	Scene = FFinisherScene();
	Scene.Path = EFinisherPath::Interaction;
	Scene.Target = Target;
	SubscribeFinisher(Finisher);
	if (!Finisher.StartInteractionFinisher(Target))
	{
		UnsubscribeFinisher();
		return false;
	}

	Scene.Config = Finisher.GetActiveConfig();
	// 새 경로는 처형별 FinisherConfig 대신 피해자의 RagdollConfig 가
	// 살아남았을 때의 물리 전환과 기상 후보 몽타주를 소유한다.
	VB_LOG(Log, "%s: 상호작용 처형 시작 (target=%s)", *GetName(), *GetNameSafe(Target));
	BeginFinisherGore();
	return true;
}

bool UVBGA_Execute::StartContextualScene(UVBFinisherComponent& Finisher, const UVBFinisherConfig* Config, AActor* Target)
{
	// 장면 시작 전에 끊는다 - 뒤에 끊으면 취소 정리가 새 처형 몽타주까지 건드린다.
	CancelOwnAttacks();

	// 끝을 먼저 구독한다. 시작 직후 끝나는 경우(클립이 없거나 즉시 이탈)에도 놓치지 않는다.
	SubscribeFinisher(Finisher);
	if (!Finisher.StartFinisher(Config, Target))
	{
		// 구독을 되돌린다. 안 그러면 다음 처형의 끝에 이 어빌리티가 반응한다.
		UnsubscribeFinisher();
		Scene = FFinisherScene();
		return false;
	}

	Scene.Path = EFinisherPath::Contextual;
	Scene.Target = Target;
	Scene.Config = Config;
	VB_LOG(Log, "%s: 처형 연출 시작 (target=%s)", *GetName(), *GetNameSafe(Target));
	BeginFinisherGore();
	return true;
}

void UVBGA_Execute::SubscribeFinisher(UVBFinisherComponent& Finisher)
{
	Scene.Finisher = &Finisher;
	Finisher.OnFinisherEnded.AddUniqueDynamic(this, &UVBGA_Execute::HandleFinisherEnded);
	// 피해자 클립이 공격자보다 먼저 끝난다. 피해를 그 시각에 줘야 사이에 일어서지 않는다.
	Finisher.OnInteractionVictimLanded.AddUniqueDynamic(this, &UVBGA_Execute::HandleInteractionVictimLanded);
}

void UVBGA_Execute::UnsubscribeFinisher()
{
	if (UVBFinisherComponent* Finisher = Scene.Finisher.Get())
	{
		Finisher->OnFinisherEnded.RemoveDynamic(this, &UVBGA_Execute::HandleFinisherEnded);
		Finisher->OnInteractionVictimLanded.RemoveDynamic(this, &UVBGA_Execute::HandleInteractionVictimLanded);
	}
}

void UVBGA_Execute::CancelOwnAttacks()
{
	// 진행 중인 내 공격을 끊는다. BlockAbilitiesWithTag 는 신규 활성화만 막아 도는 스윙이 처형 몽타주를 덮는다.
	if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackTags;
		AttackTags.AddTag(VBGameplayTags::Ability_Attack);
		OwnerASC->CancelAbilities(&AttackTags, nullptr, this);
	}
}

void UVBGA_Execute::BeginFinisherGore()
{
	// 이벤트 구독만 건다(서버). 창은 공격자 몽타주의 판정 창 노티가 연다.
	if (!GoreCueTag.IsValid() || !StrikeFeedback.TryBeginListening())
	{
		return;
	}

	UAbilityTask_WaitGameplayEvent* BeginTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, VBGameplayTags::Event_Montage_AttackTrace, nullptr, /*OnlyTriggerOnce=*/false, /*OnlyMatchExact=*/true);
	BeginTask->EventReceived.AddDynamic(this, &ThisClass::OnFinisherStrikeBegin);
	BeginTask->ReadyForActivation();
	UAbilityTask_WaitGameplayEvent* EndTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, VBGameplayTags::Event_Montage_AttackTraceEnd, nullptr, /*OnlyTriggerOnce=*/false, /*OnlyMatchExact=*/true);
	EndTask->EventReceived.AddDynamic(this, &ThisClass::OnFinisherStrikeEnd);
	EndTask->ReadyForActivation();
}

void UVBGA_Execute::OnFinisherStrikeBegin(FGameplayEventData Payload)
{
	StrikeFeedback.OpenStrike(Payload, MakeStrikeContext(), [this](int32 Slot)
	{
		return FVBMeleeTraceHits::CreateUObject(this, &ThisClass::OnFinisherGoreHits, Slot);
	});
}

void UVBGA_Execute::OnFinisherStrikeEnd(FGameplayEventData Payload)
{
	StrikeFeedback.CloseStrikes(Payload, MakeStrikeContext());
}

void UVBGA_Execute::OnFinisherGoreHits(const TArray<FHitResult>& Hits, int32 Slot)
{
	StrikeFeedback.ReportHits(Hits, Slot, MakeStrikeContext());
}

FVBFinisherStrikeContext UVBGA_Execute::MakeStrikeContext() const
{
	FVBFinisherStrikeContext Context;
	Context.Ability = this;
	Context.Avatar = GetAvatarActorFromActorInfo();
	Context.Target = Scene.Target.Get();
	Context.OwnerASC = GetAbilitySystemComponentFromActorInfo();
	Context.Config = Scene.Config.Get();
	Context.FallbackCue = GoreCueTag;
	Context.NearMissReach = GoreNearMissReach;
	Context.bSceneActive = Scene.Path != EFinisherPath::Instant;
	return Context;
}

void UVBGA_Execute::HandleInteractionVictimLanded(FVector LaunchVelocity)
{
	if (Scene.Path != EFinisherPath::Interaction || Scene.bDamageApplied)
	{
		return;
	}

	AActor* const Target = Scene.Target.Get();
	if (!Target)
	{
		// 대상이 사라졌다. 표시는 세우지 않아 HandleFinisherEnded 가 한 번 더 시도할 수 있게 둔다.
		VB_LOG(Warning, "%s: 피해자 착지 신호가 왔는데 대상이 없다", *GetName());
		return;
	}

	// 피해 전에 표시를 세운다 - 피해가 곧바로 사망 -> 래그돌 -> 정착 예약으로 이어진다.
	// 피해자 클립은 날고 있는 채 끝나므로 남은 비행 운동량을 물리가 이어받게 한다.
	// 두 컴포넌트는 적 몸이 C++ 에서 만든다. 없으면 적이 아닌 피해자다.
	UVBEnemyDeathPresentationComponent* const VictimDeath = UVBEnemyDeathPresentationComponent::FindFor(Target);
	UVBEnemyRagdollComponent* const VictimRagdoll = UVBEnemyRagdollComponent::FindFor(Target);
	if (VictimDeath)
	{
		VictimDeath->KeepMomentum(LaunchVelocity);
	}

	Scene.bDamageApplied = true;
	ApplyMedicalEffects(GetCurrentActorInfo(), Target);
	if (VictimDeath && VictimRagdoll)
	{
		const UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
		if (!TargetASC || !UVBAbilitySystemStatics::IsDead(TargetASC))
		{
			// 불사 더미/피해 실패에는 물리 전환 예약이 다음 사망까지 남지 않는다.
			VictimDeath->ClearPendingMomentum();
			// 저작된 상호작용은 공중에서 피해자를 놓는다. 나머지는 물리가, 정착 뒤는 GA_GetUp 이 맡는다.
			VictimRagdoll->StartRecoverable(LaunchVelocity);
		}
	}

	VB_LOG(Log, "%s: 피해자 착지 - 피해 적용 (%s), 발사 속도 %.0f cm/s. 공격자 클립은 계속 돈다",
		*GetName(), *GetNameSafe(Target), LaunchVelocity.Size());
}

void UVBGA_Execute::HandleFinisherEnded()
{
	if (Scene.Path == EFinisherPath::Interaction)
	{
		FinishInteractionScene();
		return;
	}

	FinishContextualScene();
}

void UVBGA_Execute::FinishInteractionScene()
{
	const UVBFinisherComponent* Finisher = Scene.Finisher.Get();
	if (!Finisher || Finisher->WasLastFinisherCancelled() || !Scene.bDamageApplied)
	{
		// 피해 없이 끝난 처형은 알린다. 실제 취소는 조용히, 장면이 끝났는데 피해가 없으면 결함이다.
		if (Finisher && Finisher->WasLastFinisherCancelled())
		{
			VB_LOG(Log, "%s: 상호작용 처형이 중단되어 피해 없이 끝났다 (대상 %s)", *GetName(), *GetNameSafe(Scene.Target.Get()));
		}
		else
		{
			VB_LOG(Warning, "%s: 상호작용 처형이 피해 없이 끝났다 - %s (대상 %s)", *GetName(),
				!Finisher ? TEXT("처형 컴포넌트 없음") : TEXT("장면은 끝났는데 피해자 착지 신호가 없었다"), *GetNameSafe(Scene.Target.Get()));
		}

		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	Scene.Target.Reset();
	Scene.Config.Reset();
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(),
		/*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
}

void UVBGA_Execute::FinishContextualScene()
{
	// 피해는 장면이 끝난 뒤 준다. 부모와 같은 적용 경로이고 대상은 시작 때 잡은 것을 쓴다.
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	AActor* const Target = Scene.Target.Get();
	if (!Scene.bDamageApplied)
	{
		Scene.bDamageApplied = true;
		ApplyMedicalEffects(ActorInfo, Target);
	}

	RebaseVictimCapsule(Target);
	SendFinisherSurvived(Target);
	Scene.Target.Reset();
	Scene.Config.Reset();
	EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(),
		/*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
}

void UVBGA_Execute::RebaseVictimCapsule(AActor* Target) const
{
	// 캡슐을 메시가 실제로 그려진 자리로 데려온다(장면 직후여야 마지막 자세가 남아 있다).
	// 피해자 클립 루트가 루트모션 없이 움직여 메시와 캡슐이 떨어지기 때문이다. 어긋난 양은 재서 쓴다.
	USkeletalMeshComponent* VictimMesh = Target ? Target->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	if (!VictimMesh)
	{
		return;
	}

	const UAbilitySystemComponent* VictimASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
	const bool bVictimDead = VictimASC && UVBAbilitySystemStatics::IsDead(VictimASC);
	const bool bVictimSimulating = VictimMesh->IsAnySimulatingPhysics();
	if (bVictimDead || bVictimSimulating)
	{
		VB_LOG(Log, "FinisherEnd: skip capsule rebase actor=%s dead=%d physics=%d; ragdoll owns world pose",
			*Target->GetName(), bVictimDead ? 1 : 0, bVictimSimulating ? 1 : 0);
		return;
	}
	// 살아남은 피해자의 위치, 회전은 GetUp 이 첫 기상 포즈로 보정한다. 여기서 옮기면 튄다.
	const bool bGetUpOwnsHandoff = VictimASC && Scene.Config.IsValid() && Scene.Config->GetUpMontage
		&& Cast<UVBEnemyAnimInstance>(VictimMesh->GetAnimInstance());
	if (bGetUpOwnsHandoff)
	{
		return;
	}

	const FTransform MeshXf = VictimMesh->GetComponentTransform();
	const FTransform RootXf = VictimMesh->GetSocketTransform(VictimRootBoneName, RTS_World);

	// Z 는 뺀다. 캡슐은 땅에 붙어 있어야 하고, 누운 자세의 루트 높이를 그대로 주면 캡슐이 가라앉는다.
	const FVector Offset(RootXf.GetLocation().X - MeshXf.GetLocation().X,
		RootXf.GetLocation().Y - MeshXf.GetLocation().Y,
		0.0f);
	const float YawDelta = FRotator::NormalizeAxis(RootXf.Rotator().Yaw - MeshXf.Rotator().Yaw);

	Target->AddActorWorldOffset(Offset, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	Target->AddActorWorldRotation(FRotator(0.0f, YawDelta, 0.0f), /*bSweep=*/false, nullptr,
		ETeleportType::TeleportPhysics);

	VB_LOG(Log, "%s: 장면 끝 - 캡슐을 몸 자리로 (%.0fcm, %.1f도)",
		*GetName(), Offset.Size2D(), YawDelta);
}

void UVBGA_Execute::SendFinisherSurvived(AActor* Target) const
{
	const UVBFinisherConfig* Config = Scene.Config.Get();
	UAbilitySystemComponent* TargetASC = (Target && Config) ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target) : nullptr;
	if (!TargetASC || UVBAbilitySystemStatics::IsDead(TargetASC))
	{
		return;
	}

	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	FGameplayEventData Payload;
	Payload.EventTag = VBGameplayTags::Event_Combat_FinisherSurvived;
	Payload.Instigator = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	Payload.Target = Target;
	// 기상 클립은 처형마다 짝이다. 대상이 처형 설정을 몰라도 되게 실어 보낸다.
	Payload.OptionalObject = Config->GetUpMontage;
	// 쓰러진 방향과 클립이 가정하는 방향의 차. 이걸 안 실으면 기상 시작에 캐릭터가 홱 돈다.
	Payload.EventMagnitude = Config->GetUpYawOffset;

	TargetASC->HandleGameplayEvent(Payload.EventTag, &Payload);
	VB_LOG(Log, "%s: 대상이 처형을 버텼다 - 기상 이벤트 발송 (%s, 회전 보정 %.1f도)",
		*GetName(), *GetNameSafe(Config->GetUpMontage), Config->GetUpYawOffset);
}

void UVBGA_Execute::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 종료 경로 셋(정상, 취소, 조기 이탈)이 모두 여기로 와서 정리를 한 곳에 둔다.
	if (StrikeFeedback.IsListening())
	{
		StrikeFeedback.End(MakeStrikeContext());
	}

	if (Scene.Path != EFinisherPath::Instant)
	{
		UVBFinisherComponent* Finisher = Scene.Finisher.Get();
		UnsubscribeFinisher();
		Scene = FFinisherScene();
		// 취소로 끝났는데 장면이 돌고 있으면 세운다.
		if (bWasCancelled && Finisher && Finisher->IsPlaying())
		{
			Finisher->EarlyOut();
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
