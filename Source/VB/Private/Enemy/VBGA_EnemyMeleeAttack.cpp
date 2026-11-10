// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Enemy/VBGA_EnemyMeleeAttack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayTag.h"
#include "Animation/AnimMontage.h"
#include "MotionWarpingComponent.h"
#include "RootMotionModifier.h"
#include "Animation/VBMotionWarpingNames.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AI/VBEnemyCombatComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h" // IsDowned - 다운 반격 판별
#include "AbilitySystem/Abilities/VBMeleeAttackRules.h" // 클립 전진, 접근 워프 창 측정
#include "Data/VBAttackConfig.h"   // PeekAttackConfig()->AttackAnswer (예고 큐의 답 태그)
#include "Data/VBEnemyConfig.h"
#include "CharacterCommon/VBStrideScale.h"
#include "Animation/VBRootMotionModifier_ScaledSkewWarp.h" // 배율을 아는 워프인지 검사
#include "VB/VB.h"

namespace
{
	// 이 아바타의 아키타입 튜닝. 전투 컴포넌트를 거치면 미할당이어도 CDO 가 와서 null 이 아니다.
	const UVBEnemyConfig* ResolveEnemyConfig(const AActor* Avatar)
	{
		if (Avatar)
		{
			if (const UVBEnemyCombatComponent* CombatComponent = Avatar->FindComponentByClass<UVBEnemyCombatComponent>())
			{
				return CombatComponent->GetCombatConfig();
			}
		}

		return GetDefault<UVBEnemyConfig>();
	}

	// 계측: 예고 시작 시점의 대상 거리. 스윙 시점 거리와 비교해 예고 동안의 이동을 본다.
	void LogTelegraphStartDistance(const AActor* Avatar)
	{
		if (const UVBEnemyCombatComponent* CombatForLog = Avatar ? Avatar->FindComponentByClass<UVBEnemyCombatComponent>() : nullptr)
		{
			if (const AActor* LogTarget = CombatForLog->GetCombatTarget())
			{
				VB_LOG(Log, "[예고 시작] %s 거리 %.0f", *GetNameSafe(Avatar),
					FVector::Dist2D(Avatar->GetActorLocation(), LogTarget->GetActorLocation()));
			}
		}
	}

	// 계획된 변형을 쓸지 정하고 판정을 로그로 남긴다.
	bool KeepPlannedEnemyVariant(const FVBEnemyVariantFit& Fit, int32 Planned, const ACharacter& Avatar)
	{
		switch (VBMeleeAttackRules::JudgePlannedVariant(Fit, Planned))
		{
		case EVBPlannedVariant::InBand:
			VB_LOG(Log, "[변형] %s 계획된 %d번 사용 (필요 %.0f, 전진 %.0f)", *GetNameSafe(&Avatar), Planned, Fit.Needed[Planned], Fit.Lunges[Planned]);
			return true;
		case EVBPlannedVariant::OutOfBand:
			VB_LOG(Log, "[변형] %s 계획된 %d번 기각 - 필요 %.0f 가 허용 %.0f~%.0f 밖 (예고 중 거리가 바뀌었다) -> 다시 고른다",
				*GetNameSafe(&Avatar), Planned, Fit.Needed[Planned], Fit.Lunges[Planned] / Fit.Ratio, Fit.Lunges[Planned] * Fit.Ratio);
			return false;
		case EVBPlannedVariant::Unusable:
			break;
		}

		return false;
	}
}

UVBGA_EnemyMeleeAttack::UVBGA_EnemyMeleeAttack()
{
	// BT 태스크가 이 태그로 스펙을 찾는다. 비면 적이 영영 안 때린다. 바꾸면 UVBEnemyConfig::AttackAbilityInputTag 도 같이 바꾼다.
	InputTag = VBGameplayTags::Input_Attack_Light;

	// 이 GA 의 식별자. 경직의 Ability.Attack 막기가 이것으로 매칭한다. 플레이어 경공격과 이름을 나눈다.
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Attack_Enemy));

	// 이 태그의 상승/하강 에지가 토큰 임대의 시작과 해제를 만든다(UVBEnemyCombatComponent::OnAttackingTagChanged).
	// 빠지면 토큰이 해제되지 않고 만료 안전망도 이 멤버를 못 본다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Attacking);

	// 사망, 페이즈 전환, 조우 보류 중 활성화 금지는 전투 규칙 표(DA_CombatRules)가 맡는다 - BT 데코레이터는 한 틱 늦다.
}

void UVBGA_EnemyMeleeAttack::ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// Super 를 여기서 부르지 않는다 - 예고가 끝난 뒤 BeginSwing 이 부른다.
	AActor* Avatar = GetAvatarActorFromActorInfo();
	const UVBEnemyConfig* Config = ResolveEnemyConfig(Avatar);

	// 예고 분기보다 먼저 건다 - TelegraphDuration=0 이어도 안전망이 남아야 한다.
	WatchStaggerUnlessDownCounter();

	// 이 활성화의 공격 설정(스윙 전이라 ActiveConfig 대신 PeekAttackConfig). 예고 값은 공격 값이 우선, 없으면 아키타입.
	const UVBAttackConfig* Pending = PeekAttackConfig();
	LogTelegraphStartDistance(Avatar);

	const float TelegraphDuration = (Pending && Pending->TelegraphDurationOverride >= 0.0f)
		? Pending->TelegraphDurationOverride : Config->TelegraphDuration;
	UAnimMontage* TelegraphMontage = (Pending
		&& Pending->TelegraphMontage) ? Pending->TelegraphMontage.Get() : Config->TelegraphMontage.Get();

	// 슈퍼아머 공격은 예고부터 슈퍼아머가 선다(래치가 스윙 때의 두 번째 호출을 무시). HitReacting 감시보다 먼저여야 예외가 된다.
	if (Pending && Pending->bSuperArmorDuringAttack)
	{
		GrantSuperArmorOnce(Pending);
	}

	WatchHitReact();

	// WaitGameplayTagAdded 는 태그가 이미 있으면 즉시 알려 위 감시가 EndAbility 까지 동기로 끝냈을 수 있다 - 여기서 끊는다.
	if (!IsActive())
	{
		return;
	}

	if (TelegraphDuration <= 0.0f)
	{
		// 예고 없음 - 바로 스윙한다. 경직 감시는 이미 걸려 있다.
		BeginSwing();
		return;
	}

	RaiseTelegraph(Avatar, Pending, TelegraphDuration);
	if (TelegraphMontage)
	{
		PlayTelegraphMontage(TelegraphMontage);
	}

	WaitTelegraphThenSwing(TelegraphDuration);
}

void UVBGA_EnemyMeleeAttack::WaitTelegraphThenSwing(float TelegraphDuration)
{
	if (!GetWorld())
	{
		// 월드가 없으면 대기가 성립하지 않아 즉시 스윙한다.
		VB_LOG(Warning, "%s: 월드 없음 -> 예고 태스크를 걸 수 없다. 즉시 스윙으로 폴백", *GetName());
		BeginSwing();
		return;
	}

	// 예고 지연. 어빌리티 태스크라 어빌리티가 끝나면 엔진이 함께 끝낸다. 게임 시간 기준이라 히트스톱에 함께 늘어난다.
	UAbilityTask_WaitDelay* TelegraphDelay =
		UAbilityTask_WaitDelay::WaitDelay(this, TelegraphDuration);
	TelegraphDelay->OnFinish.AddDynamic(this, &UVBGA_EnemyMeleeAttack::BeginSwing);
	TelegraphDelay->ReadyForActivation();
}

void UVBGA_EnemyMeleeAttack::WatchStaggerUnlessDownCounter()
{
	// 경직(State.Combat.Staggered) 감시. 몽타주 중단에 기대지 않는다(예고, 경직 몽타주가 없을 수 있다).
	// 다운 반격(Downed)은 Staggered 를 든 채 켜지므로 제외한다.
	const UAbilitySystemComponent* GateASC = GetAbilitySystemComponentFromActorInfo();
	const bool bDownCounter = UVBAbilitySystemStatics::IsDowned(GateASC);
	if (!bDownCounter)
	{
		UAbilityTask_WaitGameplayTagAdded* StaggerWatch =
			UAbilityTask_WaitGameplayTagAdded::WaitGameplayTagAdd(
				this,
				VBGameplayTags::State_Combat_Staggered,
				/*InOptionalExternalTarget=*/nullptr,
				/*OnlyTriggerOnce=*/true);
		StaggerWatch->Added.AddDynamic(this, &UVBGA_EnemyMeleeAttack::OnStaggeredDuringAttack);
		StaggerWatch->ReadyForActivation();
	}
}

void UVBGA_EnemyMeleeAttack::WatchHitReact()
{
	// 맞으면 예고도 끊긴다.
	UAbilityTask_WaitGameplayTagAdded* HitWatch =
		UAbilityTask_WaitGameplayTagAdded::WaitGameplayTagAdd(
			this,
			VBGameplayTags::State_Combat_HitReacting,
			/*InOptionalExternalTarget=*/nullptr,
			/*OnlyTriggerOnce=*/true);
	HitWatch->Added.AddDynamic(this, &UVBGA_EnemyMeleeAttack::OnHitDuringAttack);
	HitWatch->ReadyForActivation();
}

void UVBGA_EnemyMeleeAttack::RaiseTelegraph(AActor* Avatar, const UVBAttackConfig* Pending, float TelegraphDuration)
{
	UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo();
	if (!OwnerASC)
	{
		return;
	}

	// 해석된 길이를 싣는다 - 슈퍼아머 반격의 긴 예고까지 빨강이 간다.
	Telegraph.Grant(*OwnerASC, Avatar, Pending, TelegraphDuration, Pending ? Pending->AttackAnswer : FGameplayTag());

	// 예고 시작 이벤트. 서버 로컬이다(RPC 없음) - 클라 연출은 위 GameplayCue 가 맡는다.
	FGameplayEventData TelegraphEvent;
	TelegraphEvent.EventTag = VBGameplayTags::Event_Combat_TelegraphStart;
	TelegraphEvent.Instigator = Avatar;
	TelegraphEvent.Target = Avatar;
	OwnerASC->HandleGameplayEvent(VBGameplayTags::Event_Combat_TelegraphStart, &TelegraphEvent);
}

void UVBGA_EnemyMeleeAttack::PlayTelegraphMontage(UAnimMontage* TelegraphMontage)
{
	// 순수 연출이라 델리게이트를 걸지 않고 재생속도도 1.0 고정 - 타이밍을 정하는 것은 WaitDelay 하나다.
	UAbilityTask_PlayMontageAndWait* TelegraphTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			NAME_None,
			TelegraphMontage,
			1.0f,
			NAME_None,
			true,
			1.0f,
			0.0f,
			false);
	TelegraphTask->ReadyForActivation();
}

void UVBGA_EnemyMeleeAttack::OnHitDuringAttack()
{
	// 예고 중일 때만 끝낸다. 스윙 중 피격은 히트리액트 몽타주가 이미 끝내고 있다.
	if (Telegraph.IsGranted())
	{
		VB_LOG(Log, "[Telegraph] %s 예고 끊김(피격)", *GetNameSafe(GetAvatarActorFromActorInfo()));
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
	           /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
}

void UVBGA_EnemyMeleeAttack::OnStaggeredDuringAttack()
{
	VB_LOG(Log, "%s: 경직 -> 공격 취소", *GetName());
	// 취소로 끝낸다. Attacking 태그가 빠지며 토큰이 해제되고, BT 태스크는 정상 종료해 트리가 재탐색한다.
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
	           /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
}

void UVBGA_EnemyMeleeAttack::BeginSwing()
{
	Telegraph.Release();
	CapturedApproachWindows.Reset();
	if (AActor* Avatar = GetAvatarActorFromActorInfo(); Avatar && Avatar->HasAuthority())
	{
		ApproachWarpComponent = Avatar->FindComponentByClass<UMotionWarpingComponent>();
		if (ApproachWarpComponent.IsValid())
		{
			ApproachWarpComponent->OnPreUpdate.AddUniqueDynamic(this, &UVBGA_EnemyMeleeAttack::OnApproachWarpUpdate);
		}
	}

	// 베이스의 본 스윙에 진입한다. 베이스는 TriggerEventData 를 쓰지 않는다.
	Super::ExecuteAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, nullptr);

}

bool UVBGA_EnemyMeleeAttack::IsApproachWindowPending(const URootMotionModifier* Modifier, const UAnimMontage* Montage)
{
	const URootMotionModifier_Warp* Warp = Cast<URootMotionModifier_Warp>(Modifier);
	return Montage && Warp && Warp->Animation.Get() == Montage
		&& Warp->WarpTargetName == VBMotionWarpingNames::AttackTarget
		&& Warp->GetState() == ERootMotionModifierState::Waiting;
}

float UVBGA_EnemyMeleeAttack::MeasureApproachWindowLunge(const URootMotionModifier* Modifier, float MeshScale)
{
	if (!Modifier || !Modifier->Animation.IsValid() || MeshScale <= 0.0f)
	{
		return 0.0f;
	}

	const FTransform Delta = UMotionWarpingUtilities::ExtractRootMotionFromAnimation(
		Modifier->Animation.Get(), FMath::Max(0.0f, Modifier->StartTime), Modifier->EndTime);
	return static_cast<float>(Delta.GetTranslation().Size2D()) * MeshScale;
}

void UVBGA_EnemyMeleeAttack::OnApproachWarpUpdate(UMotionWarpingComponent* Component)
{
	if (!IsActive() || !Component || Component != ApproachWarpComponent.Get()
		|| !CurrentActorInfo || !CurrentActorInfo->IsNetAuthority())
	{
		return;
	}

	for (URootMotionModifier* Modifier : Component->GetModifiers())
	{
		if (IsApproachWindowPending(Modifier, GetActiveMontage()) && !CapturedApproachWindows.Contains(Modifier))
		{
			CapturedApproachWindows.Add(Modifier);
			SetWarpTargetToCombatTarget(Modifier);
		}
	}
}

void UVBGA_EnemyMeleeAttack::SetWarpTargetToCombatTarget(const URootMotionModifier* Modifier)
{
	const ACharacter* Avatar = GetAvatarCharacter();
	const UVBEnemyCombatComponent* Combat = Avatar ? Avatar->FindComponentByClass<UVBEnemyCombatComponent>() : nullptr;
	const AActor* Target = Combat ? Combat->GetCombatTarget() : nullptr;
	if (Target && Target->GetRootComponent())
	{
		// 워프 대역: 클립 전진을 허용비 안에서만 늘이거나 줄인다. 밖이면 목적지를 경계에 세워 미끄러지지 않게 한다.
		// 하한은 캡슐 합 - 그 안으로 세우면 CMC 가 밀어내 떨린다.
		const USceneComponent* Root = Target->GetRootComponent();
		const float Distance = FVector::Dist2D(Avatar->GetActorLocation(), Target->GetActorLocation());
		const float Offset0 = ComputeApproachOffset(Root);
		const float Needed = FMath::Max(0.0f, Distance - Offset0);
		// 배율은 VBStrideScale::Resolve 에서만 얻는다 - 계획과 스윙이 같은 값을 쓰게.
		const float MeshScale = VBStrideScale::Resolve(Avatar);
		const float Lunge = MeasureApproachWindowLunge(Modifier, MeshScale);
#if !UE_BUILD_SHIPPING
		// 메시 배율이 1 이 아닌데 워프가 배율을 모르면 캐릭터가 미끄러진다. 자산 설정이라 몽타주당 한 번 경고만 한다.
		if (!FMath::IsNearlyEqual(MeshScale, 1.0f, 1.0e-3f))
		{
			const UAnimMontage* Montage = GetActiveMontage();
			const URootMotionModifier_Warp* Warp = VBMeleeAttackRules::FindApproachWarpModifier(Montage);
			static TSet<FName> WarnedMontages;
			if (Warp && !Warp->IsA<UVBRootMotionModifier_ScaledSkewWarp>() && !WarnedMontages.Contains(Montage->GetFName()))
			{
				WarnedMontages.Add(Montage->GetFName());
				VB_LOG(Warning, "[워프 배율] %s 배율 %.2f 인데 %s 의 접근 워프가 %s 다 - 배율을 두 번 곱해 미끄러진다. "
					"노티 모디파이어를 «Skew Warp (메시 배율 보정)» 으로 바꿔라",
					*GetNameSafe(Avatar), MeshScale, *GetNameSafe(Montage), *Warp->GetClass()->GetName());
			}
		}
#endif
		const float Ratio = FMath::Max(1.0f, ResolveEnemyConfig(Avatar)->WarpLungeToleranceRatio);
		float Travel = Needed;
		if (Lunge <= 1.0f)
		{
			Travel = 0.0f; // 제자리 클립 - 회전만 맞춘다
		}
		else
		{
			Travel = FMath::Clamp(Needed, Lunge / Ratio, Lunge * Ratio);
		}

		float Offset = Distance - Travel;
		Offset = FMath::Max(Offset, FMath::Min(FVBApproachWarpDriver::MinSeparation(Avatar, Root), Distance));
		VB_LOG(Log, "[워프 대역] %s 창 %.3f~%.3f 거리 %.0f 필요 %.0f 전진 %.0f 허용 %.0f~%.0f -> 이동 %.0f (오프셋 %.0f, 고정 목표)",
			*GetNameSafe(Avatar), Modifier->StartTime, Modifier->EndTime, Distance, Needed, Lunge,
			Lunge / Ratio, Lunge * Ratio, Distance - Offset, Offset);
		// 목표를 스윙 시작의 한 점에 고정한다 - 따라가면 물러나는 플레이어를 쫓아 대역보다 멀리 미끄러진다.
		ApplyApproachWarpWithOffset(Root, Offset, /*bFollowTarget=*/false);
	}
	else
	{
		// 타겟이 없으면 목표를 지워 생 루트모션이 돈다는 것을 명시한다.
		ClearWarpTarget();
	}
}

void UVBGA_EnemyMeleeAttack::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 예고 지연 태스크는 엔진이 함께 끝내므로 지울 타이머가 없다.

	Telegraph.Release();
	if (ApproachWarpComponent.IsValid())
	{
		ApproachWarpComponent->OnPreUpdate.RemoveDynamic(this, &UVBGA_EnemyMeleeAttack::OnApproachWarpUpdate);
	}

	ApproachWarpComponent.Reset();
	CapturedApproachWindows.Reset();

	// 베이스가 SuperArmor 루즈 태그를 정리한다.
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

int32 UVBGA_EnemyMeleeAttack::SelectVariantIndex(const UVBAttackConfig& Leaf, int32 VariantCount)
{
	if (VariantCount <= 1)
	{
		return Super::SelectVariantIndex(Leaf, VariantCount);
	}

	const ACharacter* Avatar = GetAvatarCharacter();
	UVBEnemyCombatComponent* Combat = Avatar ? Avatar->FindComponentByClass<UVBEnemyCombatComponent>() : nullptr;
	const AActor* Target = Combat ? Combat->GetCombatTarget() : nullptr;
	if (!Target || !Target->GetRootComponent())
	{
		return Super::SelectVariantIndex(Leaf, VariantCount);
	}

	FVBEnemyVariantFit Fit;
	Fit.Ratio = FMath::Max(1.0f, ResolveEnemyConfig(Avatar)->WarpLungeToleranceRatio);

	// 변형마다 멈춤 거리가 달라 필요 거리를 따로 잰다(ComputeApproachOffset). 전진은 MeasureMontageLungeCm(메시 배율 반영).
	const USceneComponent* Root = Target->GetRootComponent();
	const float Distance = FVector::Dist2D(Avatar->GetActorLocation(), Target->GetActorLocation());
	const float MeshScale = VBStrideScale::Resolve(Avatar);
	TArray<EVBAttackSide> Sides;
	FString Candidates;
	for (int32 Index = 0; Index < VariantCount; ++Index)
	{
		const FVBAttackMontageVariant Variant = Leaf.ResolveMontageVariant(Index);
		Sides.Add(Variant.Side);
		Fit.Lunges.Add(VBMeleeAttackRules::MeasureMontageLungeCm(Variant.Montage, MeshScale));
		Fit.Needed.Add(FMath::Max(0.0f, Distance - ComputeApproachOffset(Root, Leaf.GetVariantApproachOffset(Index, MeshScale))));
		Candidates += FString::Printf(TEXT("%s%s %.0f/%.0f"), Index ? TEXT(" / ") : TEXT(""), *GetNameSafe(Variant.Montage),
			Fit.Lunges[Index], Fit.Needed[Index]);
	}

	const float TargetLocalY = Avatar->GetActorTransform().InverseTransformPositionNoScale(Target->GetActorLocation()).Y;
	const EVBAttackSide TargetSide = VBMeleeAttackRules::ResolveTargetSide(TargetLocalY);
	bool bNoneOnSide = false;
	Fit.Allowed = VBMeleeAttackRules::FilterVariantsBySide(Sides, TargetSide, bNoneOnSide);
	VB_CLOG(bNoneOnSide, Warning, "[변형] %s 표적이 %s 쪽인데 그쪽 변형이 없다 - 전부 허용. %s 의 Side 저작을 보라",
		*GetNameSafe(Avatar), TargetSide == EVBAttackSide::Right ? TEXT("오른") : TEXT("왼"), *GetNameSafe(&Leaf));

	// 토큰 때 계획한 공격이 지금도 대역 안이면 그대로 쓴다. 예고 동안 거리가 바뀌어 밖이면 다시 고른다.
	if (Combat)
	{
		const int32 Planned = Combat->ConsumePlannedVariantIndex();
		if (KeepPlannedEnemyVariant(Fit, Planned, *Avatar))
		{
			return Planned;
		}
	}

	// 좌우 필터가 최소 하나는 남기므로 Best 가 INDEX_NONE 으로 끝나는 길은 없다.
	bool bInBand = false;
	const int32 Best = VBMeleeAttackRules::PickBestEnemyVariant(Fit, bInBand);
	// 압축 = 전진 / 필요. 1 에 가까울수록 발이 안 미끄러진다.
	VB_LOG(Log, "[변형] %s 거리 %.0f -> %d번 (전진 %.0f, 압축 %.2f, %s) 후보 [%s]",
		*GetNameSafe(Avatar), Distance, Best, Fit.Lunges[Best], Fit.Needed[Best] > 1.0f ? Fit.Lunges[Best] / Fit.Needed[Best] : 0.0f,
		bInBand ? TEXT("대역 안") : TEXT("대역 밖-덜 틀린 것"), *Candidates);
	return Best;
}
