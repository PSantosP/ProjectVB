// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_MeleeAttackBase.h"
#include "Animation/VBMontageTransition.h"

#include "Character/VBCombatInputComponent.h"
#include "AbilitySystemComponent.h"
#include "MotionWarpingComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "Character/VBCharacter.h"
#include "Character/VBSlideMovementComponent.h"
#include "Character/VBHeroComponent.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Character/VBMeleeTraceComponent.h"
#include "Character/VBTargetable.h"
#include "Character/VBTargetLockComponent.h"
#include "Data/VBTargetLockConfig.h" // 방향 전환 거리 상한 읽기
#include "Character/VBWeaponStateComponent.h"
#include "Data/VBWeaponDefinition.h"
#include "Data/VBComboSelectionPolicy.h"
#include "AbilitySystem/VBAttackActivationData.h"
#include "CharacterCommon/VBStrideScale.h"
#include "Data/VBSwingImpactProfile.h" // Cast<> 대상 - 완전 타입 필요
#include "Data/VBMoveCancelGate.h"
#include "AbilitySystem/Abilities/VBWeaponStyleBehavior.h"
#include "AbilitySystem/Abilities/VBMeleeAttackRules.h"
#include "AbilitySystem/Abilities/VBMeleeHitApplication.h"
#include "AbilitySystem/Abilities/VBMeleeHitPresentation.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "Animation/AnimMontage.h" // ActiveMontage 의 노티, 섹션 읽기
#include "Components/CapsuleComponent.h"
#include "Animation/VBMotionWarpingNames.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "VB/VB.h"
#include "AbilitySystem/VBGameplayEffectContext.h"
#include "Debug/VBCombatReadoutSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
// 개발용 - 새 콤보마다 변형 N 으로 시작한다(0 = 기본 콤보, -1 = 끔). 이것 없이는 수동 콤보가 플레이에서 안 나온다
static TAutoConsoleVariable<int32> CVarForceCombo(
	TEXT("vb.Combat.ForceCombo"), -1,
	TEXT("Dev test: start every new melee combo with variant N (0 = base combo). -1 = off (normal distance-based pick). Read where the attack ability activates (server / listen host)."),
	ECVF_Cheat);
#endif

void UVBGA_MeleeAttackBase::InputPressed(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputPressed(Handle, ActorInfo, ActivationInfo);
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!IsActive() || !ASC)
	{
		return;
	}

	FGameplayEventData Event;
	Event.EventTag = VBGameplayTags::Event_Combo_InputReceived;
	ASC->HandleGameplayEvent(Event.EventTag, &Event);
}

namespace
{
	void LogMontageMeshState(const TCHAR* Phase, ACharacter* Character)
	{
		if (!Character || !Character->GetMesh())
		{
			return;
		}

		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const USkeletalMeshComponent* Mesh = Character->GetMesh();
		const UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();

		VB_LOG(Warning,
			"[%s] ActorZ=%.2f CapsuleZ=%.2f MeshWorldZ=%.2f MeshRelZ=%.2f HalfHeight=%.2f MovementMode=%d",
			Phase,
			Character->GetActorLocation().Z,
			Capsule ? Capsule->GetComponentLocation().Z : -9999.0f,
			Mesh->GetComponentLocation().Z,
			Mesh->GetRelativeLocation().Z,
			Capsule ? Capsule->GetScaledCapsuleHalfHeight() : -9999.0f,
			MoveComp ? (int32)MoveComp->MovementMode : -1);
	}
}

UVBGA_MeleeAttackBase::UVBGA_MeleeAttackBase()
{
	// ServerInitiated - 소유 클라에도 인스턴스가 있어 몽타주를 로컬 재생한다(ServerOnly 면 소유 클라가 몽타주를 못 본다).
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
	// 공격의 입구는 서버 누름 수락 하나다 - 클라의 기본 GAS 활성화 요청은 준비를 건너뛰고 임의 데이터를 실을 수 있어 엔진 입구에서 거절시킨다.
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnlyExecution;
	bCancelOnInputRelease = false;
}

void UVBGA_MeleeAttackBase::ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	++ActivationSerial; // 맞은 뒤 처리가 «같은 스윙» 인지 가르는 번호(ApplySwingHits)

	// ServerInitiated 전환으로 이 함수는 서버/클라 양쪽에서 실행된다.
	// 몽타주 재생 Task는 양쪽 공통(로컬 재생), Trace/GE/Event 송신은 서버 전용 가드 필요.
	const bool bIsServer = ActorInfo && ActorInfo->IsNetAuthority();

	// 이 누름의 방향(서버).
	UVBCombatInputComponent* PressInput = bIsServer ? UVBCombatInputComponent::FindFor(GetAvatarActorFromActorInfo()) : nullptr;
	const FVector2D PressIntent = PressInput ? PressInput->ConsumeAttackIntent() : FVector2D::ZeroVector;
	Combo.ClearChainIntent();
	ResetActivationState();

	// 무기 상태가 없는 아바타(적)는 전투 자극이 없다. 발도는 없다 - 이 능력은 손에 든 무기의 세트가 주었다(칼집 누름은 누름 길이 먼저 꺼낸다).
	if (UVBWeaponStateComponent* WSC = GetWeaponStateFromActorInfo())
	{
		WSC->EnterCombat();
	}

	// 무브셋, 공격 자산 해석은 아바타 종류와 무관하다(적도 여기서 잎을 잡는다).
	//   서버가 활성화 전에 고른 것(PrepareActivation)이 있으면 그것이 원본이다.
	const FVBAttackActivationData* Prepared = FVBAttackActivationData::Find(TriggerEventData);
	if (!ResolveActiveAttack(Prepared))
	{
		// 끝냄을 서버로 올리는 것은 권위뿐 - 클라의 자산 해석 실패가 서버에서 정상으로 시작한 공격을 끊지 않게.
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/bIsServer, /*bWasCancelled=*/true);
		return;
	}

	if (!ActiveMontage)
	{
		EndWithoutMontage(Handle, ActorInfo, ActivationInfo, bIsServer);
		return;
	}

	// 슈퍼아머: 설정이 요구하면 스윙 동안 태그를 건다 - 피격자의 히트리액트 능력이 이 태그로 막힌다(서버, 소유 클라 양쪽).
	//   제거는 EndAbility 한 곳.
	if (ActiveConfig && ActiveConfig->bSuperArmorDuringAttack)
	{
		GrantSuperArmorOnce(ActiveConfig);
	}

	// 회피 오프셋: 회피가 끊은 콤보를 유예 안에 이어 받는다(시작 섹션으로). 워프 등록보다 먼저, 재개 판정은 서버 것을 쓴다.
	const FName StartSection = ResolveDodgeResumeSection(Prepared);
	PrepareFirstSwing(PressIntent, Prepared != nullptr, bIsServer);
	PlayAttackMontage(StartSection);

	// ReadyForActivation 이 몽타주를 못 틀면 그 자리에서 EndAbility 가 돈다 - 그 뒤 이벤트 대기 Task 를 만들면 고아가 되어
	//   다음 활성화에서 한 휘두름이 피해를 두 번 넣는다. 처형 중(ExternalMontageOwner)이 실제로 그 경우다.
	if (!IsActive())
	{
		return;
	}

	ListenForAttackEvents(bIsServer);
	VB_LOG(Log, "%s: 몽타주 재생 시작 (%s)", *GetName(), *ActiveMontage->GetName());
}

void UVBGA_MeleeAttackBase::ResetActivationState()
{
	Combo.ResetForActivation();
	// 변형 재생배율은 활성화마다 초기화한다(공격 사이 기억 ActivationMemory 와 반대).
	// 이 값은 "이번에 고른 변형의 배율"이라 다음 활성화로 새어 나가면 안 된다.
	ActiveVariantRate = 1.0f;
	ActiveVariantDamage = 1.0f;
	ActiveVariantApproachOffset = -1.0f;
	ActiveVariantComboOffsets.Reset();

	// 임팩트 쉐이크 프로필도 활성화마다 비운다 - 몽타주 없는 폴백은 노티 없이 판정하므로 직전 스윙의 프로필이 남는다.
	ActiveSwingProfile = nullptr;
	ActiveSwingCombatProfile = nullptr;

	// 잎 해석: 무기 정의에 이 공격의 잎이 있으면 그것, 없으면 GA 에 직접 단 AttackConfig.
	ActiveConfig = AttackConfig; // 레거시 폴백 기본값(GA에 직접 할당된 config)
	ActiveMontage = AnimMontage; // 레거시 폴백 기본값(GA에 직접 할당된 montage)
}

bool UVBGA_MeleeAttackBase::ResolveActiveAttack(const FVBAttackActivationData* Prepared)
{
	UVBAttackConfig* Leaf = nullptr;
	int32 ChosenVariantIndex = 0;
	bStartTargetInReach = true;
	if (Prepared)
	{
		if (!Prepared->IsExecutable())
		{
			VB_LOG(Error, "%s: 준비된 공격을 실행할 수 없다(설정 %s, 변형 %d) - 다른 몽타주로 대신하지 않고 거절한다",
				*GetName(), *GetNameSafe(Prepared->Config), Prepared->VariantIndex);
			return false;
		}

		Leaf = Prepared->Config;
		ChosenVariantIndex = Prepared->VariantIndex;
		bStartTargetInReach = Prepared->bTargetInReach;
		// 실행 시점인 여기서 기록해 거절된 입력이 변형 순환을 진행시키지 않는다. 회피 재개는 같은 변형을 다시 기록한다
		ActivationMemory.NoteComboStart(Leaf, ChosenVariantIndex, Leaf->ResolveMontageVariant(ChosenVariantIndex).Role);
	}
	else
	{
		// 공격 자산이 먼저, 거기 몽타주가 없을 때만 GA 의 AnimMontage(PeekAttackConfig 와 같은 규칙 - 예고와 스윙이 같은 자산을 읽는다).
		Leaf = ResolveWeaponLeaf();
		if (!Leaf)
		{
			Leaf = AttackConfig;
		}

		ChosenVariantIndex = Leaf ? SelectVariantIndex(*Leaf, Leaf->GetMontageVariantCount()) : 0;
	}

	ApplyAttackLeaf(Leaf, ChosenVariantIndex);
	return true;
}

void UVBGA_MeleeAttackBase::ApplyAttackLeaf(UVBAttackConfig* Leaf, int32 ChosenVariantIndex)
{
	const int32 VariantCount = Leaf ? Leaf->GetMontageVariantCount() : 0;
	if (Leaf)
	{
		ActiveConfig = Leaf;
		// 변형이 없으면 결과가 AttackMontage 그대로라 현행과 동일 동작이다.
		const FVBAttackMontageVariant Variant = Leaf->ResolveMontageVariant(ChosenVariantIndex);
		if (Variant.Montage)
		{
			ActiveMontage = Variant.Montage;
			ActiveVariantRate = Variant.PlayRateScale;
			ActiveVariantDamage = Variant.DamageScale;
			ActiveVariantApproachOffset = Leaf->GetVariantApproachOffset(ChosenVariantIndex,
				VBStrideScale::Resolve(GetAvatarActorFromActorInfo()));
			ActiveVariantComboOffsets = Variant.ComboApproachOffsets;
		}
	}

	// 무브셋 해석 1줄 계측 - 무기 타입/leaf/최종 몽타주/변형 (오선택 진단용)
	VB_LOG(Log, "%s: Moveset resolve — WeaponType=%d Leaf=%s Montage=%s variant=%d/%d rate=%.2f",
		*GetName(), (int32)GetGrantingWeaponType(),
		Leaf ? *Leaf->GetName() : TEXT("null(legacy-fallback)"),
		ActiveMontage ? *ActiveMontage->GetName() : TEXT("null"),
		VariantCount > 0 ? ((ChosenVariantIndex % VariantCount) + VariantCount) % VariantCount : 0,
		VariantCount, ActiveVariantRate);
}

void UVBGA_MeleeAttackBase::EndWithoutMontage(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bIsServer)
{
	if (!AllowsImmediateTraceFallback())
	{
		VB_LOG(Warning, "%s: no montage and this attack allows no immediate trace - ends cancelled, no damage", *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	VB_LOG(Warning, "%s: AttackMontage 미설정 -> 즉시 Trace 폴백", *GetName());
	if (bIsServer)
	{
		PerformAttackTrace(); // 권위 전용: 데미지/GE 이중 적용 방지
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

FName UVBGA_MeleeAttackBase::ResolveDodgeResumeSection(const FVBAttackActivationData* Prepared)
{
	FName StartSection = NAME_None;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const int32 ResumeIndex = Prepared ? Prepared->ResumeComboIndex
		: ActivationMemory.PendingDodgeResume(ActiveConfig, GetGrantingWeaponType(), ActiveMontage, Now);
	if (ResumeIndex > 0 && VBMeleeAttackRules::HasComboSection(ActiveMontage, ActiveConfig, ResumeIndex))
	{
		Combo.ResumeAtStrike(ResumeIndex);
		StartSection = VBMeleeAttackRules::ComboSectionName(ActiveConfig, Combo.GetComboIndex());
		VB_LOG(Log, "[Combo] 회피 오프셋 - %.2f초 전 끊김 -> %d타(%s)부터 재개",
			Now - ActivationMemory.DodgeCutTime(), Combo.GetComboIndex() + 1, *StartSection.ToString());
	}

	ActivationMemory.ConsumeDodgeResume(); // 한 번 쓰거나 유예를 초과하면 버린다
	return StartSection;
}

void UVBGA_MeleeAttackBase::PrepareFirstSwing(const FVector2D& PressIntent, bool bPrepared, bool bIsServer)
{
	Combo.PrepareFirstSwing();

	if (bIsServer)
	{
		ApplyFirstHitDirectionalSwitch(PressIntent, bPrepared);
	}

	// 워프 윈도우 진입 시 타겟이 이미 등록이 되어있어야 한다. (양쪽 공통 - 로컬 재생 정렬)
	++SwingSerial;
	SetWarpTargetLockedEnemy();
	BindWarpRearm();
}

void UVBGA_MeleeAttackBase::ApplyFirstHitDirectionalSwitch(const FVector2D& PressIntent, bool bPrepared)
{
	// 변형 정책이 있는 무기(bPrepared)는 변형을 A 의 거리로 이미 골랐다 - 그 변형이 닿을 만한 콤보 단계 거리만 허용한다.
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (const UVBTargetLockComponent* Lock = Avatar ? Avatar->FindComponentByClass<UVBTargetLockComponent>() : nullptr)
	{
		const UVBTargetLockConfig* LockCfg = Lock->GetTargetLockConfig();
		ApplyDirectionalSwitch(PressIntent, bPrepared ? LockCfg->DirectionalChainMaxDistance : LockCfg->DirectionalMaxDistance);
	}
}

void UVBGA_MeleeAttackBase::PlayAttackMontage(FName StartSection)
{
	// 실효 속도 = GA 기본 x 무기 잎 x 변형(입도가 셋이라 따로 둔다).
	const float ConfigRateScale = ActiveConfig ? ActiveConfig->MontagePlayRateScale : 1.0f;
	UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
				this,
				NAME_None,
				ActiveMontage,
				MontagePlayRate * ConfigRateScale * ActiveVariantRate,
				StartSection,
				true,
				1.0f,
				0.0f,
				true);

	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnMontageCompleted);
	MontageTask->OnBlendOut.AddDynamic(this, &ThisClass::OnMontageBlendOut);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnMontageCancelled);
	MontageTask->ReadyForActivation();
}

UAbilityTask_WaitGameplayEvent* UVBGA_MeleeAttackBase::WaitRepeatedEvent(const FGameplayTag& EventTag)
{
	// 자기 자신을 기다린다. 콤보의 각 섹션마다 같은 이벤트를 반복 수신해야 한다.
	return UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, EventTag, nullptr, false, true);
}

void UVBGA_MeleeAttackBase::ListenForAttackEvents(bool bIsServer)
{
	// 노티 이벤트 대기 - 양쪽 공통(클라도 닿는 프레임을 알아야 같은 시점에 전환한다). 판정, 스타일 훅은 서버만.
	UAbilityTask_WaitGameplayEvent* TraceTask = WaitRepeatedEvent(VBGameplayTags::Event_Montage_AttackTrace);
	TraceTask->EventReceived.AddDynamic(this, &ThisClass::OnAttackTraceEvent);
	TraceTask->ReadyForActivation();

	// 판정 창 닫힘 - 서버만 듣는다. 창은 서버에서만 열린다(UVBMeleeTraceComponent::BeginWindow 의 권위 관문).
	if (bIsServer)
	{
		UAbilityTask_WaitGameplayEvent* TraceEndTask = WaitRepeatedEvent(VBGameplayTags::Event_Montage_AttackTraceEnd);
		TraceEndTask->EventReceived.AddDynamic(this, &ThisClass::OnAttackTraceEndEvent);
		TraceEndTask->ReadyForActivation();
	}

	if (!ActiveConfig || ActiveConfig->MaxComboCount <= 1)
	{
		return;
	}

	UAbilityTask_WaitGameplayEvent* WindowOpenTask = WaitRepeatedEvent(VBGameplayTags::Event_Combo_WindowOpen);
	WindowOpenTask->EventReceived.AddDynamic(this, &ThisClass::OnComboWindowOpen);
	WindowOpenTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* WindowCloseTask = WaitRepeatedEvent(VBGameplayTags::Event_Combo_WindowClose);
	WindowCloseTask->EventReceived.AddDynamic(this, &ThisClass::OnComboWindowClose);
	WindowCloseTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* InputTask = WaitRepeatedEvent(VBGameplayTags::Event_Combo_InputReceived);
	InputTask->EventReceived.AddDynamic(this, &ThisClass::OnComboInputReceived);
	InputTask->ReadyForActivation();
}


// AnimNotify에서 이벤트 수신 -> Trace 실행
void UVBGA_MeleeAttackBase::OnAttackTraceEvent(FGameplayEventData PayLoad)
{
	// 닿는 프레임 도달 - 양쪽 공통. 조기 전환의 두 번째 조건이다.
	Combo.NoteHitFrame(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);

	// 트레이스, 스타일 훅은 서버만. 클라는 표시만 하고 전환 시도로 간다.
	if (!CurrentActorInfo || !CurrentActorInfo->IsNetAuthority())
	{
		TryChainAfterHit();
		return;
	}

	VB_LOG(Log, "%s: AttackTrace 이벤트 수신 -> Trace 실행", *GetName());
	// 계측: 닿는 프레임에 목표까지 남은 거리, 캐릭터 방향 오차, 카메라-캐릭터 각, 워프 회전 소유.
	//   워프 소유는 AVBCharacter::IsWarpOwningRotation 과 같은 조건을 여기서 읽는다 - 그쪽이 바뀌면 같이 바꾼다.
	if (const USceneComponent* AimTarget = ResolveAimTargetComponent())
	{
		if (const AActor* Avatar = GetAvatarActorFromActorInfo())
		{
			const FVector ToTarget = (AimTarget->GetComponentLocation() - Avatar->GetActorLocation()).GetSafeNormal2D();
			const double FacingError = ToTarget.IsNearlyZero() ? 0.0
				: FRotator::NormalizeAxis(ToTarget.Rotation().Yaw - Avatar->GetActorRotation().Yaw);
			const APawn* AvatarPawn = Cast<APawn>(Avatar);
			const AController* Controller = AvatarPawn ? AvatarPawn->GetController() : nullptr;
			// 조종자가 없으면 '없음' 으로 남긴다 - 0도는 '카메라와 캐릭터가 같은 방향' 과 구별되지 않는다.
			const FString CameraToBody = Controller
				? FString::Printf(TEXT("%+.0f도"),
					FRotator::NormalizeAxis(Controller->GetControlRotation().Yaw - Avatar->GetActorRotation().Yaw))
				: FString(TEXT("없음"));
			const UMotionWarpingComponent* Warping = Avatar->FindComponentByClass<UMotionWarpingComponent>();
			const bool bWarpOwnsRotation = Warping && Warping->FindWarpTarget(VBMotionWarpingNames::AttackTarget) != nullptr;
			VB_LOG(Log, "[Warp] %d타 닿는 프레임 - 목표 %s 까지 %.0fcm, 몸 방향 오차 %+.0f도, 카메라-몸 %s, 워프 회전 소유 %s", Combo.GetComboIndex() + 1,
				*GetNameSafe(AimTarget->GetOwner()), FVector::Dist2D(Avatar->GetActorLocation(), AimTarget->GetComponentLocation()),
				FacingError, *CameraToBody, bWarpOwnsRotation ? TEXT("O") : TEXT("X"));
		}
	}

	// 이번 스윙의 임팩트 쉐이크 프로필 - 노티마다 덮어쓴다(한 몽타주 안의 타마다 다르다). null 이면 잎을 쓴다.
	ActiveSwingProfile = Cast<UVBSwingImpactProfile>(PayLoad.OptionalObject.Get());
	// 전투 수치 배율도 노티마다 덮어쓰고 null 도 받는다 - 거르면 저작 안 한 스윙이 직전 무게를 물려받는다.
	ActiveSwingCombatProfile = Cast<UVBSwingCombatProfile>(PayLoad.OptionalObject2.Get());

	// 스타일 고유 타격(예: 마법 투사체)이 있으면 위임. 처리되면(true) 기본 판정 스킵. 판정 창이면 창 시작에서 한 번 묻는다.
	if (ActiveConfig && ActiveConfig->StyleBehavior && ActiveConfig->StyleBehavior->DeliverHit(this))
	{
		TryChainAfterHit();
		return;
	}
	// 판정 창(UVBANS_AttackTraceWindow)은 Melee.Window 태그를 싣는다. 없으면 이전 한 프레임 노티다 - 창으로 안 바꾼 몽타주는 이전 방식 그대로 돈다.
	if (PayLoad.InstigatorTags.HasTagExact(VBGameplayTags::Melee_Window))
	{
		BeginTraceWindow(PayLoad);
	}
	else
	{
		PerformAttackTrace();
	}

	TryChainAfterHit();
}

void UVBGA_MeleeAttackBase::OnAttackTraceEndEvent(FGameplayEventData PayLoad)
{
	const FGameplayTagContainer Shapes = UVBMeleeTraceComponent::ShapesFromEventTags(PayLoad.InstigatorTags);
	// 모양 없는 창은 열리지 않았다 - 여기서 전부 닫으면 겹친 다른 창을 끊는다. 남는 창은 섹션 전환, EndAbility 가 닫는다.
	if (!Shapes.IsEmpty())
	{
		EndTraceWindowByShapes(Shapes, PayLoad.EventMagnitude > 0.0f);
	}
}

void UVBGA_MeleeAttackBase::TryChainAfterHit()
{
	if (!Combo.CanChainAfterHit(ActiveConfig))
	{
		return;
	}

	// 여운이 남았으면 타이머로 돌아온다.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const double Remaining = Combo.ChainDelayRemaining(*ActiveConfig, Now);
	if (Remaining > 0.0)
	{
		Combo.ScheduleChainRetry(GetWorld(), FTimerDelegate::CreateUObject(this, &ThisClass::TryChainAfterHit), Remaining);
		return;
	}

	TryChainToNextSection();
}

// 아바타의 이동 컴포넌트. 플레이어, NPC 캐릭터는 같은 이동 컴포넌트 클래스를 쓴다.
UVBSlideMovementComponent* UVBGA_MeleeAttackBase::GetAvatarMovement() const
{
	const ACharacter* Avatar = GetAvatarCharacter();
	return Avatar ? Cast<UVBSlideMovementComponent>(Avatar->GetCharacterMovement()) : nullptr;
}

// 몽타주 정상 완료 -> GA 종료
void UVBGA_MeleeAttackBase::OnMontageCompleted()
{
	if (UVBSlideMovementComponent* Movement = GetAvatarMovement())
	{
		Movement->StopResidualAttackMotion(0.0f, ActiveMontage);
	}

	LogMontageMeshState(TEXT("Completed"), GetAvatarCharacter());
	VB_LOG(Log, "%s: 몽타주 완료 -> EndAbility", *GetName());
	ClearWarpTarget();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

// 몽타주 BlendOut 시작
void UVBGA_MeleeAttackBase::OnMontageBlendOut()
{
	// 저작된 복귀는 자연 블렌드아웃까지 유지한다. 무입력 잔여 이동만 이 몽타주의 수명에 묶어 정리한다.
	if (UVBSlideMovementComponent* Movement = GetAvatarMovement())
	{
		const float Frame = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
		Movement->StopResidualAttackMotion(2.0f * Frame, ActiveMontage);
	}

	if (AVBCharacter* Character = Cast<AVBCharacter>(GetAvatarCharacter()))
	{
		Character->BeginRecoveryCancelTrace();
	}
	// BlendOut 중에는 GA 가 아직 살아 있다.
	LogMontageMeshState(TEXT("BlendOut"), GetAvatarCharacter());
	VB_LOG(Log, "%s: 몽타주 BlendOut", *GetName());
	// BlendOut 은 EndAbility 를 부르지 않는다 - 뒤이어 Completed 가 온다.
}

// 다른 몽타주에 의해 중단
void UVBGA_MeleeAttackBase::OnMontageInterrupted()
{
	LogMontageMeshState(TEXT("Interrupted"), GetAvatarCharacter());
	VB_LOG(Log, "%s: 몽타주 중단됨 -> EndAbility(취소)", *GetName());
	ClearWarpTarget();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}


// GA 취소로 인한 중단
void UVBGA_MeleeAttackBase::OnMontageCancelled()
{
	LogMontageMeshState(TEXT("Cancelled"), GetAvatarCharacter());
	VB_LOG(Log, "%s: 몽타주 취소됨 -> EndAbility(취소)", *GetName());
	ClearWarpTarget();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

// 공격 종료 - 슈퍼아머 태그 제거(건 경우만). 완료, 중단, 취소가 모두 여기로 온다.
void UVBGA_MeleeAttackBase::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (GrantedSuperArmorTag.IsValid())
	{
		if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
		{
			OwnerASC->RemoveLooseGameplayTag(GrantedSuperArmorTag);
		}

		GrantedSuperArmorTag = FGameplayTag();
	}

	Combo.StopTimers(GetWorld()); // 창 닫힘, 잇기는 다음 검사에서 스스로 멈추지만, 끝난 GA 에 주기가 남지 않게 여기서도 끊는다
	Combo.ClearChainIntent(); // 끝난 공격의 방향이 다음 활성화로 새지 않게

	// 열린 판정 창을 닫는다 - 닫지 않으면 다음 활성화에 옛 창의 콜백이 들어온다(액터당 한 인스턴스).
	EndAllTraceWindows();

	// 회피 오프셋 기억: 회피가 끊었을 때만. 피격, 가드, 완료, 회복 캔슬은 기억하지 않는다.
	if (bWasCancelled && ActiveConfig && ActiveConfig->DodgeOffsetGraceSeconds > 0.0f)
	{
		const UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo();
		if (OwnerASC && OwnerASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Dodging))
		{
			const int32 ResumeComboIndex = Combo.ResumeStrikeAfterCut();
			ActivationMemory.NoteDodgeCut(ResumeComboIndex, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0, GetGrantingWeaponType());
			VB_LOG(Log, "[Combo] 회피가 %d타를 끊음(닿음 %s) - %.1f초 안에 %d타부터 재개",
				Combo.GetComboIndex() + 1, Combo.IsHitFrameReached() ? TEXT("O") : TEXT("X"), ActiveConfig->DodgeOffsetGraceSeconds,
				ResumeComboIndex + 1);
		}
	}

	UnbindWarpRearm();
	ClearWarpTarget();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// 스윙 진입에서 확정한 대상만 읽는다. 도중 락 전환은 다음 스윙에 반영한다.
const USceneComponent* UVBGA_MeleeAttackBase::ResolveAimTargetComponent() const
{
	AActor* Target = SwingTarget.Get();
	if (!IsValid(Target))
	{
		return nullptr;
	}

	return IVBTargetable::IsActorTargetable(Target) ? Target->GetRootComponent() : nullptr;
}

AActor* UVBGA_MeleeAttackBase::FindSwingTargetCandidate() const
{
	// 락온이 없는 아바타(적)는 고를 후보가 없다.
	const UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(GetAvatarActorFromActorInfo());
	if (!Lock)
	{
		return nullptr;
	}

	AActor* Target = Lock->GetLockedTarget();
	if (!IsValid(Target))
	{
		Target = Lock->FindSoftLungeTarget();
	}

	return IVBTargetable::IsActorTargetable(Target) ? Target : nullptr;
}

void UVBGA_MeleeAttackBase::CaptureSwingTarget()
{
	// 대상이 없으면 Bind 가 앞 대상을 푸는 것으로 끝난다.
	FScriptDelegate OnDestroyed;
	OnDestroyed.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UVBGA_MeleeAttackBase, OnSwingTargetDestroyed));
	SwingTarget.Bind(FindSwingTargetCandidate(), OnDestroyed,
		FOnGameplayEffectTagCountChanged::FDelegate::CreateUObject(this, &ThisClass::OnSwingTargetDeath));
}

void UVBGA_MeleeAttackBase::OnSwingTargetDeath(const FGameplayTag /*Tag*/, int32 NewCount)
{
	if (NewCount > 0)
	{
		DropWarpTarget(EVBWarpDropReason::TargetDied);
	}
}

void UVBGA_MeleeAttackBase::OnSwingTargetDestroyed(AActor* /*DestroyedActor*/)
{
	DropWarpTarget(EVBWarpDropReason::TargetDestroyed);
}

void UVBGA_MeleeAttackBase::DropWarpTarget(EVBWarpDropReason Reason)
{
	const TCHAR* Text = Reason == EVBWarpDropReason::TargetDied ? TEXT("대상 사망")
		: Reason == EVBWarpDropReason::TargetDestroyed ? TEXT("대상 파괴")
		: TEXT("대상이 타겟 자격을 잃음");
	VB_LOG(Log, "[Warp] %d타 워프 타겟 해제 - %s", Combo.GetComboIndex() + 1, Text);
	ClearWarpTarget();
}

void UVBGA_MeleeAttackBase::ApplyDirectionalSwitch(const FVector2D& WorldIntent, float StepMaxDistance)
{
	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(GetAvatarActorFromActorInfo());
	if (!bAllowDirectionalLockSwitch || !Info || !Info->IsNetAuthority() || !Lock || WorldIntent.IsNearlyZero()
		|| !IsValid(Lock->GetLockedTarget()))
	{
		return;
	}
	// 판 번호는 제안 전에 읽는다 - 제안과 확정 사이에 락이 바뀌었으면(대상 사망 재지정 등) 확정이 거절한다.
	const uint32 Revision = Lock->GetLockRevision();
	AActor* Previous = Lock->GetLockedTarget();
	AActor* Proposed = Lock->ProposeDirectionalTarget(WorldIntent, StepMaxDistance);
	const bool bSwitched = Proposed && Lock->CommitDirectionalSwitch(Proposed, Revision);
	// 누른 타마다 한 줄 - '방향을 줬는데 안 바뀌었다' 를 로그로 가른다.
	VB_LOG(Log, "[DirSwitch] %d타 입력 (%.2f, %.2f) 크기 %.2f 거리상한 %.0f: %s -> %s",
		Combo.GetComboIndex() + 1, WorldIntent.X, WorldIntent.Y, WorldIntent.Size(), StepMaxDistance,
		*GetNameSafe(Previous), bSwitched ? *GetNameSafe(Proposed) : TEXT("유지"));
}

void UVBGA_MeleeAttackBase::SetWarpTargetLockedEnemy(bool bCapture)
{
	// 타 시작에만 대상을 잡는다. 같은 타의 다음 워프 창은 잡은 대상을 그대로 쓴다(죽었으면 워프 없음 - 다른 적으로 꺾지 않는다).
	if (bCapture)
	{
		CaptureSwingTarget();
	}
	// 락온이 없는 아바타(적)는 소프트 런지/워프 대상에서 빠진다.
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!UVBTargetLockComponent::FindFor(Avatar))
	{
		return;
	}

	UMotionWarpingComponent* MotionWarpingComponent = Avatar->FindComponentByClass<UMotionWarpingComponent>();
	if (!MotionWarpingComponent)
	{
		return;
	}

	// 타겟 해석은 판정과 공유한다 - 둘이 갈리면 워프와 판정 방향이 어긋난다.
	const USceneComponent* ResolvedComponent = ResolveAimTargetComponent();

	// write 단일화(락, 소프트 공유)
	if (ResolvedComponent && Combo.GetComboIndex() == 0 && !bStartTargetInReach)
	{
		ApplyNaturalReachWarp(ResolvedComponent);
	}
	else if (ResolvedComponent)
	{
		ApplyApproachWarpToTarget(ResolvedComponent);
	}
	else
	{
		ApproachWarp.Clear(MotionWarpingComponent);
		VB_LOG(Log, "[Warp] %d타 목표 없음 - 접근 워프 없음 (락온 X, 소프트 콘 밖)", Combo.GetComboIndex() + 1);
	}
}

float UVBGA_MeleeAttackBase::ComputeApproachOffset(const USceneComponent* TargetComponent) const
{
	// 변형이 해석된 뒤면 그 변형의 멈춤(이미 캐릭터 배율을 곱했다), 아니면 자산 값에 캐릭터 배율을 곱한 것. 두 경우 다 아래 사슬은 같다.
	float BaseOffset = ActiveVariantApproachOffset >= 0.0f
		? ActiveVariantApproachOffset
		: (ActiveConfig ? ActiveConfig->WarpApproachOffset * VBStrideScale::Resolve(GetAvatarActorFromActorInfo()) : 0.0f);
	if (ActiveConfig)
	{
		BaseOffset = ActiveConfig->ResolveComboApproachOffset(ActiveVariantComboOffsets, Combo.GetComboIndex(), BaseOffset,
			VBStrideScale::Resolve(GetAvatarActorFromActorInfo()));
	}

	return ComputeApproachOffset(TargetComponent, BaseOffset);
}

float UVBGA_MeleeAttackBase::ComputeApproachOffset(const USceneComponent* TargetComponent, float BaseOffset) const
{
	const ACharacter* VBChar = GetAvatarCharacter();
	// 문맥이 없으면 사슬을 부르지 않는다 - 0 으로 채워 부르면 캡슐 하한이 0 이 아닌 값을 낼 수 있다(설계 1절).
	if (!VBChar || !TargetComponent || !ActiveConfig)
	{
		return 0.0f;
	}

	FVBApproachOffsetInputs In;
	// BaseOffset 은 이미 캐릭터 배율이 곱해진 값이다 - 상한인 사거리도 같은 단위로 맞춘다. 먼 시작, 여유는 배율을 곱하지 않는 자산 값이다.
	In.BaseOffset = BaseOffset;
	In.ScaledRange = ActiveConfig->GetScaledAttackRange(VBStrideScale::Resolve(VBChar));
	In.FarStartApproachOffset = ActiveConfig->FarStartApproachOffset;
	In.MinWarpStandoff = ActiveConfig->MinWarpStandoff;
	In.DistToTarget2D = FVector::Dist2D(VBChar->GetActorLocation(), TargetComponent->GetComponentLocation());
	// 반경은 런타임에서 읽는다 - 적 종류마다 다르고 스케일도 걸릴 수 있어 상수로 굳히면 어긋난다.
	In.MinSeparation = FVBApproachWarpDriver::MinSeparation(VBChar, TargetComponent);
	return VBMeleeAttackRules::ResolveApproachOffset(In);
}

int32 UVBGA_MeleeAttackBase::SelectVariantIndex(const UVBAttackConfig& Leaf, int32 VariantCount)
{
	return ActivationMemory.TakeVariant(VariantCount);
}

void UVBGA_MeleeAttackBase::ApplyApproachWarpToTarget(const USceneComponent* TargetComponent)
{
	ApplyApproachWarpWithOffset(TargetComponent, ComputeApproachOffset(TargetComponent));
}

void UVBGA_MeleeAttackBase::ApplyApproachWarpWithOffset(const USceneComponent* TargetComponent, float ApproachOffset, bool bFollowTarget)
{
	// 워프 목적지를 쓰는 한 곳(락, 소프트 런지, 적 공격 GA). 아바타는 ACharacter 로 받는다.
	ACharacter* VBChar = GetAvatarCharacter();
	if (!VBChar || !TargetComponent)
	{
		return;
	}

	UMotionWarpingComponent* MotionWarpingComponent = VBChar->FindComponentByClass<UMotionWarpingComponent>();
	if (!MotionWarpingComponent)
	{
		return;
	}

	// (B) 접근 간격 - 워프 목적지를 적 중심에서 공격자 쪽으로 WarpApproachOffset 만큼 당겨 무기 리치에서 멈춘다.
	// 오프셋은 [0, AttackRange] 로 자른다 - 스윕 끝점 구체가 AttackRange 지점의 적 중심까지 덮으므로 그 이상은 헛친다.
	// 목표, 거리, 오프셋, 락온 여부를 한 줄로 남겨 닿는 프레임의 거리와 짝을 짓는다.
	{
		// 락온 컴포넌트는 플레이어에게만 있다. 적이면 '적' 으로 적어 같은 한 줄이 양쪽 경로를 덮게 한다.
		const UVBTargetLockComponent* TLC = UVBTargetLockComponent::FindFor(VBChar);
		VB_LOG(Log, "[Warp] %d타 목표=%s 거리=%.0fcm 오프셋=%.0fcm 락온=%s",
			Combo.GetComboIndex() + 1, *GetNameSafe(TargetComponent->GetOwner()),
			FVector::Dist2D(VBChar->GetActorLocation(), TargetComponent->GetComponentLocation()), ApproachOffset,
			!TLC ? TEXT("적") : (TLC->IsLockedOn() ? TEXT("O") : TEXT("X(소프트)")));
	}

	ApproachWarp.SetGoal(*MotionWarpingComponent, *VBChar, *TargetComponent, ApproachOffset, bFollowTarget);
}

float UVBGA_MeleeAttackBase::GetWarpSyncMinLead() const
{
	// 0 이하면 붙잡기가 꺼진다 - 모르는 값은 기본값으로 떨어뜨린다(규칙 27).
	return (ActiveConfig && ActiveConfig->WarpSyncMinLead > 0.0f) ? ActiveConfig->WarpSyncMinLead
		: GetDefault<UVBAttackConfig>()->WarpSyncMinLead;
}

void UVBGA_MeleeAttackBase::HoldSpentApproachSync()
{
	const ACharacter* Avatar = GetAvatarCharacter();
	if (!Avatar)
	{
		return;
	}

	const float MinLead = GetWarpSyncMinLead();
	const FVBWarpHoldResult Result = ApproachWarp.Hold(*Avatar, MinLead);
	switch (Result.Event)
	{
	case EVBWarpHoldEvent::HoldStarted:
		VB_LOG(Log, "[Warp] %d타 멈추는 거리 도착(거리 %.1fcm, 멈춤 %.1fcm, 앞섬 < %.1fcm) - 제자리에서 대상을 보게 붙잡음",
			Combo.GetComboIndex() + 1, Result.Distance, ApproachWarp.GetStopDistance(), MinLead);
		break;
	case EVBWarpHoldEvent::Restored:
		VB_LOG(Log, "[Warp] %d타 대상이 다시 멀어짐(거리 %.1fcm) - 원래 목표로 되돌림", Combo.GetComboIndex() + 1, Result.Distance);
		break;
	case EVBWarpHoldEvent::Dropped:
		DropWarpTarget(EVBWarpDropReason::TargetUntargetable);
		break;
	case EVBWarpHoldEvent::Forgot: // 다른 쪽이 지웠다 - 되살리지 않는다(드라이버가 이미 잊었다)
	case EVBWarpHoldEvent::None:
		break;
	}
}

void UVBGA_MeleeAttackBase::ApplyNaturalReachWarp(const USceneComponent* TargetComponent)
{
	const ACharacter* Avatar = GetAvatarCharacter();
	if (!Avatar || !TargetComponent)
	{
		return;
	}
	// 멈추는 거리 = 지금 거리 - 자연 전진 -> 목표 = 내 자리 + 표적 방향 * 자연 전진. 고정 목표라 표적이 움직여도 더 끌려가지 않는다.
	const float Natural = VBMeleeAttackRules::MeasureMontageLungeCm(ActiveMontage, VBStrideScale::Resolve(Avatar));
	const float Distance = FVector::Dist2D(Avatar->GetActorLocation(), TargetComponent->GetComponentLocation());
	VB_LOG(Log, "[Warp] 1타 거리 자격 밖(%.0fcm) - 표적까지 늘이지 않고 자연 전진 %.0fcm 만", Distance, Natural);
	ApplyApproachWarpWithOffset(TargetComponent, FMath::Max(0.0f, Distance - Natural), /*bFollowTarget=*/false);
}

void UVBGA_MeleeAttackBase::ClearWarpTarget()
{
	SwingTarget.Unbind();
	// 세우는 쪽과 같은 탐색 - 적도 지운다.
	ACharacter* Avatar = GetAvatarCharacter();
	ApproachWarp.Clear(Avatar ? Avatar->FindComponentByClass<UMotionWarpingComponent>() : nullptr);
}

void UVBGA_MeleeAttackBase::BindWarpRearm()
{
	UnbindWarpRearm();
	// MotionWarping 을 가진 모든 아바타에 묶는다(멈춘 목표 붙잡기는 늘 필요). 창 시작 목록은 락온 컴포넌트가 있는 플레이어만.
	ACharacter* Avatar = GetAvatarCharacter();
	UMotionWarpingComponent* MotionWarpingComponent = Avatar ? Avatar->FindComponentByClass<UMotionWarpingComponent>() : nullptr;
	if (ApproachWarp.Bind(MotionWarpingComponent, ActiveMontage, UVBTargetLockComponent::FindFor(Avatar) != nullptr))
	{
		MotionWarpingComponent->OnPreUpdate.AddUniqueDynamic(this, &ThisClass::OnWarpPreUpdate);
	}
}

void UVBGA_MeleeAttackBase::UnbindWarpRearm()
{
	if (UMotionWarpingComponent* MotionWarpingComponent = ApproachWarp.Unbind())
	{
		MotionWarpingComponent->OnPreUpdate.RemoveDynamic(this, &ThisClass::OnWarpPreUpdate);
	}
}

void UVBGA_MeleeAttackBase::OnWarpPreUpdate(UMotionWarpingComponent* /*MotionWarpingComp*/)
{
	// 순서: 창 시작에서 목표를 다시 세운 뒤, 그 목표까지 포함해 붙잡기를 판정한다.
	RearmWarpAtWindowStart();
	HoldSpentApproachSync();
}

void UVBGA_MeleeAttackBase::RearmWarpAtWindowStart()
{
	const ACharacter* Avatar = GetAvatarCharacter();
	UAnimInstance* AnimInstance = Avatar && Avatar->GetMesh() ? Avatar->GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || !ActiveMontage || !AnimInstance->Montage_IsPlaying(ActiveMontage))
	{
		return;
	}

	float Start = 0.0f;
	if (ApproachWarp.PassedWindowStart(AnimInstance->Montage_GetPosition(ActiveMontage), Start))
	{
		VB_LOG(Log, "[Warp] %d타 워프 창 %.2f초 시작 - 목표 다시 세움(대상은 이 타에 잡은 그대로)", Combo.GetComboIndex() + 1, Start);
		SetWarpTargetLockedEnemy( /*bCapture=*/false);
	}
}

void UVBGA_MeleeAttackBase::OnComboWindowOpen(FGameplayEventData Payload)
{
	Combo.OpenWindow(Payload.OptionalObject.Get(), ActiveConfig, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);

	// 조기 전환: 창 열림만으로 전환하지 않는다 - 닿음이 지났고 여운이 끝났을 때. 아직이면 닿는 이벤트, 타이머가 다시 부른다.
	TryChainAfterHit();

	// 창 안 이동 끊기: 창이 열린 순간부터 새로 민 막대만 센다 - 맞을 때부터 밀던 막대는 놓았다가 다시 밀어야 한다.
	Combo.StartMoveCutWatch(ActiveConfig, GetWorld(), FTimerDelegate::CreateUObject(this, &ThisClass::TickWindowMoveCut));
}

void UVBGA_MeleeAttackBase::TickWindowMoveCut()
{
	const AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarActorFromActorInfo());
	const UCharacterMovementComponent* Movement = VBChar ? VBChar->GetCharacterMovement() : nullptr;
	if (!Combo.IsWindowOpen() || !ActiveConfig || !Movement)
	{
		Combo.StopMoveCutWatch(GetWorld());
		return;
	}
	// 가속(이동 입력) 또는 원시 막대 입력 - 공격 중 이동 입력이 무브먼트에 안 닿는 구간도 센다(상호작용과 같은 판정).
	const bool bHasMove = FVBMoveCancelGate::HasMoveInput(Movement->GetCurrentAcceleration(), UVBHeroComponent::GetRawMoveInputFor(VBChar));
	bool bAcceptsHeld = false;
	if (!Combo.StepMoveCut(*ActiveConfig, bHasMove, bAcceptsHeld))
	{
		return;
	}

	Combo.StopMoveCutWatch(GetWorld());
	CancelRecovery(bAcceptsHeld ? TEXT("창 안 이동 입력(쥔 막대 포함)") : TEXT("창 안 새 이동 입력"));
}

void UVBGA_MeleeAttackBase::OnComboWindowClose(FGameplayEventData Payload)
{
	if (!Combo.CloseWindow(Payload.OptionalObject.Get(), Payload.EventMagnitude))
	{
		return;
	}
	// 닫힐 때 전환: bChainAtWindowOpen 이 꺼진 무기, 또는 창 안 입력이 아직 소비되지 않은 경우.
	// 창이 열릴 때 잇는 쪽보다 최대 0.3초 늦게 반응한다.
	if (Combo.IsAttackSaved() && TryChainToNextSection())
	{
		Combo.ClearChainedMark(); // 이 닫힘이 곧 옛 창이라 추가 닫힘은 오지 않는다
		return;
	}
	// 설정에 따라 회복 구간을 취소한다.
	if (ActiveConfig && ActiveConfig->bCancelRecoveryAtWindowClose)
	{
		CancelRecovery();
	}
}

bool UVBGA_MeleeAttackBase::TryChainToNextSection()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	UAnimInstance* Anim = CurrentActorInfo ? CurrentActorInfo->GetAnimInstance() : nullptr;
	if (!ASC || !Anim || !ActiveMontage) { return false; }
	if (ActiveConfig && !VBMontageTransition::IsValidSectionBlend(ActiveConfig->ComboSectionBlendSeconds))
	{
		VB_LOG(Error, "Invalid ComboSectionBlendSeconds on %s", *GetNameSafe(ActiveConfig));
		return false;
	}
	if (!Combo.BeginChain(ActiveConfig, ActiveMontage, GetWorld()))
	{
		return false;
	}
	// 옛 타의 판정 창은 여기서 끝난다. 점프 뒤 엔진이 옛 창의 끝 알림을 내지만 그때는 닫을 것이 없다.
	//   조기 전환이 창의 뒤쪽을 자른다는 뜻이다 - ChainDelayAfterHitSeconds 는 '창 시작 -> 칼이 실제로 닿는 시각' 보다 길어야 한다.
	EndAllTraceWindows();

	const FName NextSection = VBMeleeAttackRules::ComboSectionName(ActiveConfig, Combo.GetComboIndex() + 1);

	Combo.AdvanceStrike();
	// 누른 다음 타의 방향 전환(서버) - 옛 판정 창은 위에서 닫혔고, 새 타의 대상을 잡기 전이다. 자동 연결은 방향이 없어 그대로다.
	if (const UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(GetAvatarActorFromActorInfo()))
	{
		ApplyDirectionalSwitch(Combo.GetChainIntent(), Lock->GetTargetLockConfig()->DirectionalChainMaxDistance);
	}

	Combo.ClearChainIntent();
	// 전환 뒤에 닿는 앞 타의 판정은 새 타로 센다. 고치지 않는다 - 판정 창은 섹션과 함께 끝나 그 틈은 한 프레임 안이고, 틀려도 반격이 한 타 일찍 올 뿐이다.
	++SwingSerial;
	ApproachWarp.ResetWindowCheck(); // 첫 창은 바로 아래에서 세운다
	SetWarpTargetLockedEnemy();

	// 로컬 즉시 전환 (ASC 경로 - AnimInstance 직접 호출은 ASC tracking 괴리 -> OnInterrupted -> EndAbility 위험)
	VBMontageTransition::RequestSectionBlend(Anim, ActiveMontage, ActiveConfig->ComboSectionBlendSeconds);
	ASC->CurrentMontageJumpToSection(NextSection);

	// Sim proxy 강제 동기화는 서버에서만 Multicast (RepAnimMontageInfo Position 기반 복제가 3+ 연속 jump에 불안정)
	if (CurrentActorInfo && CurrentActorInfo->IsNetAuthority())
	{
		if (AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarCharacter()))
		{
			VBChar->MulticastJumpToMontageSection(ActiveMontage, NextSection, ActiveConfig->ComboSectionBlendSeconds);
		}
	}

	VB_LOG(Log, "[Combo] 전환 - %d타 -> %d타(%s)", Combo.GetComboIndex(), Combo.GetComboIndex() + 1, *NextSection.ToString());
	Combo.FinishChain();
	return true;
}

void UVBGA_MeleeAttackBase::CancelRecovery(const TCHAR* Reason)
{
	// 활성 설정 없이 여기 오면 호출 순서 결함이다. 값은 설정 클래스의 기본값을 쓴다.
	VB_CLOG(!ActiveConfig, Warning, "%s: 활성 공격 설정 없이 회복을 끊는다 - RecoveryBlendOutSeconds 기본값을 쓴다", *GetName());
	const float Blend = (ActiveConfig ? ActiveConfig.Get() : GetDefault<UVBAttackConfig>())->RecoveryBlendOutSeconds;

	// 캔슬 시점의 이동 상태도 찍는다 - 일찍 끊은 몽타주의 루트모션 속도가 캡슐에 남을 수 있다.
	AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarActorFromActorInfo());
	const UCharacterMovementComponent* CancelCMC = VBChar ? VBChar->GetCharacterMovement() : nullptr;
	VB_LOG(Log, "[Combo] 회복 캔슬 - %d타 %s, %.2f초 블렌드아웃 (Speed2D %.0f, 가속 %.0f)",
		Combo.GetComboIndex() + 1, Reason, Blend,
		CancelCMC ? CancelCMC->Velocity.Size2D() : -1.0f,
		CancelCMC ? CancelCMC->GetCurrentAcceleration().Size2D() : -1.0f);
	if (VBChar)
	{
		VBChar->BeginRecoveryCancelTrace();

		// 이동 입력이 없으면 남은 이동은 플레이어 의도가 아니다 - 버린다.
		//   입력이 있으면 그 속도는 의도한 관성이므로 건드리지 않는다(회피, 이동 캔슬로 이어지는 경로).
		if (CancelCMC && CancelCMC->GetCurrentAcceleration().IsNearlyZero())
		{
			if (UVBSlideMovementComponent* Movement = GetAvatarMovement())
			{
				Movement->StopResidualAttackMotion(Blend, ActiveMontage);
			}
		}
	}

	ClearWarpTarget();
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->CurrentMontageStop(Blend);
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

bool UVBGA_MeleeAttackBase::PrepareActivation(const FVBActivationRequest& Request, FGameplayEventData& OutPayload) const
{
	const EVBWeaponType WeaponType = GetGrantingWeaponType();
	UVBAttackConfig* Leaf = ResolveWeaponLeaf();
	if (!Leaf)
	{
		Leaf = AttackConfig;
	}

	if (!Leaf || !Leaf->ComboSelection)
	{
		return false; // 정책 없는 공격(적, 카타나, 대검 지금)은 종전 규칙 - 데이터 없이 켠다
	}

	FVBAttackActivationData* Data = new FVBAttackActivationData(); // 핸들이 소유권을 가져간다(엔진 관례)
	Data->PressSerial = Request.PressSerial;
	Data->Config = Leaf;
	OutPayload.TargetData.Add(Data);
	// 회피가 끊은 콤보를 이어받으면 새로 고르지 않는다 - 시작 섹션만 바꾸는 재개라 끊긴 그 변형이어야 섹션이 맞다.
	//   여기는 읽기만 한다 - 버리는 것은 실행 시작(ExecuteAbility)이 한 번 한다.
	const int32 LastVariant = ActivationMemory.GetComboStarts().VariantFor(Leaf);
	const int32 ResumeIndex = LastVariant == INDEX_NONE ? INDEX_NONE
		: ActivationMemory.PendingDodgeResume(Leaf, WeaponType, Leaf->ResolveMontageVariant(LastVariant).Montage,
			GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	if (ResumeIndex != INDEX_NONE)
	{
		Data->VariantIndex = LastVariant;
		Data->ResumeComboIndex = ResumeIndex;
		if (UVBCombatReadoutSubsystem* Readout = UVBCombatReadoutSubsystem::GetActive(this))
		{
			Readout->NoteDodgeResume(GetAvatarActorFromActorInfo(), GetNameSafe(Leaf->ResolveMontageVariant(LastVariant).Montage));
		}

		return true;
	}
	// 고르는 것은 변형(콤보)뿐이다. 대상은 실행 때 CaptureSwingTarget 이 다시 잡는다 - 그 사이 대상이 바뀌어도 변형은 유지한다(design §7.5).
	FVBComboSelectContext Context;
	const AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarActorFromActorInfo());
	Context.bSprinting = UVBLocomotionIntentComponent::IsSprintingFor(VBChar);
	if (const AActor* Target = FindSwingTargetCandidate(); Target && VBChar)
	{
		Context.bHasTarget = true;
		Context.DistanceCm = FVector::Dist2D(Target->GetActorLocation(), VBChar->GetActorLocation());
	}

	const TArray<FVBComboCandidate> Candidates = Leaf->GetComboCandidates();
	FVBComboChoice Choice = Leaf->ComboSelection->Select(Context, Candidates, ActivationMemory.GetComboStarts(), Leaf);
#if !UE_BUILD_SHIPPING
	UVBComboSelectionPolicy::ApplyForcedVariant(CVarForceCombo.GetValueOnGameThread(), Candidates.Num(), Context.bHasTarget, Choice);
#endif
	Data->VariantIndex = Choice.Variant;
	Data->bTargetInReach = Choice.bTargetInReach;
	if (UVBCombatReadoutSubsystem* Readout = UVBCombatReadoutSubsystem::GetActive(this))
	{
		Readout->NoteComboChosen(VBChar, GetNameSafe(Leaf->ResolveMontageVariant(Choice.Variant).Montage), Choice, Context);
	}

	return true;
}

const UVBWeaponDefinition* UVBGA_MeleeAttackBase::GetGrantingWeapon() const
{
	// 무기 세트가 SourceObject = 정의로 준다(UVBWeaponStateComponent::GiveWeaponSet). 적 세트, 시험이 직접 준 능력은 정의가 아니다.
	return Cast<UVBWeaponDefinition>(GetCurrentSourceObject());
}

EVBWeaponType UVBGA_MeleeAttackBase::GetGrantingWeaponType() const
{
	const UVBWeaponDefinition* Weapon = GetGrantingWeapon();
	return Weapon ? Weapon->GetWeaponType() : EVBWeaponType::None;
}

void UVBGA_MeleeAttackBase::GrantSuperArmorOnce(const UVBAttackConfig* Config)
{
	if (GrantedSuperArmorTag.IsValid())
	{
		return;
	}

	if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
	{
		// 공격마다 armor 태그 하나 - breakable 자식은 이미 모든 소비자에게 SuperArmor 로 세어져, 둘 다 주면
		//   poise 허브가 보호된 출처로 보고 예외가 통과하지 못한다.
		const FGameplayTag ArmorTag = Config && Config->bArmorBreakable
			? VBGameplayTags::State_Combat_SuperArmor_Breakable.GetTag()
			: VBGameplayTags::State_Combat_SuperArmor.GetTag();
		// 복제 상태를 명시한다(다른 로스태그와 같은 CountToOwner).
		OwnerASC->AddLooseGameplayTag(ArmorTag, 1, EGameplayTagReplicationState::CountToOwner);
		GrantedSuperArmorTag = ArmorTag;
	}
}

UVBAttackConfig* UVBGA_MeleeAttackBase::ResolveWeaponLeaf() const
{
	// 적은 무기가 주지 않아 정의가 없다 - AttackConfig 로 돈다.
	const UVBWeaponDefinition* Weapon = GetGrantingWeapon();
	if (!Weapon)
	{
		return nullptr;
	}

	UVBAttackConfig* Leaf = SelectWeaponLeaf(*Weapon);
	if (!Leaf)
	{
		// 무기의 공격인데 그 무기의 잎이 없다 - GA 의 설정으로 조용히 내려가지 않게 소리낸다(정의 검증이 저장 때 막는 자리).
		VB_LOG(Error, "%s: 무기 정의 %s 에 이 공격의 잎이 없다 - GA 의 AttackConfig 로 돈다", *GetName(), *Weapon->GetName());
	}

	return Leaf;
}

const UVBAttackConfig* UVBGA_MeleeAttackBase::PeekAttackConfig() const
{
	const UVBAttackConfig* Leaf = ResolveWeaponLeaf();
	return Leaf ? Leaf : AttackConfig.Get();
}

void UVBGA_MeleeAttackBase::OnComboInputReceived(FGameplayEventData Payload)
{
	// 이 누름의 방향(서버). 다음 섹션에서 한 번 쓴다. 가장 새 누름이 이긴다.
	const FGameplayAbilityActorInfo* PressInfo = GetCurrentActorInfo();
	UVBCombatInputComponent* PressInput = PressInfo && PressInfo->IsNetAuthority()
		? UVBCombatInputComponent::FindFor(GetAvatarActorFromActorInfo()) : nullptr;
	const FVector2D ComboPressIntent = PressInput ? PressInput->ConsumeAttackIntent() : FVector2D::ZeroVector;

	if (Combo.NoteInput(ComboPressIntent, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0))
	{
		TryChainAfterHit();
	}
}


bool UVBGA_MeleeAttackBase::IsFinalComboStrike() const
{
	return Combo.IsFinalStrike(ActiveConfig);
}

void UVBGA_MeleeAttackBase::ConfigureHitContext(FVBGameplayEffectContext& HitFacts, const AActor*) const
{
	HitFacts.SetChargeSource(GetAssetTags().HasTag(VBGameplayTags::Ability_Attack_CinematicSkill)
		? EVBCombatChargeSource::CinematicSkill : EVBCombatChargeSource::OrdinaryAttack);
}

void UVBGA_MeleeAttackBase::ApplyAdditionalEffects(UAbilitySystemComponent* TargetASC, const FHitResult& Hit)
{
	// 데이터 기반 추가 효과: ActiveConfig->OnHitExtraEffects의 GE를 대상에 적용(출혈/스태거 등).
	// 서버 권위 경로(PerformAttackTrace)에서만 호출되므로 권위 가드 별도 불필요.
	if (!TargetASC || !ActiveConfig)
	{
		return;
	}

	for (const TSubclassOf<UGameplayEffect>& EffectClass : ActiveConfig->OnHitExtraEffects)
	{
		if (!EffectClass)
		{
			continue;
		}

		// 한도 판정은 적용 전에 한다. 엔진 OverflowEffects 는 첫 타격부터 터져(5.8) 쓰지 않는다. 한도는 GE 의 StackLimitCount.
		const UGameplayEffect* EffectCDO = EffectClass->GetDefaultObject<UGameplayEffect>();
		const int32 StackLimit = EffectCDO ? EffectCDO->StackLimitCount : 0;

		if (StackLimit > 0)
		{
			// 세는 쪽도 우리 ASC 가 건 스택만 센다 - 전체를 세면 남의 스택 때문에 첫 타가 터지고 그 뒤 계속 터진다.
			UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
			const int32 CurrentStacks = TargetASC->GetGameplayEffectCount(EffectClass, SourceASC);
			if (CurrentStacks >= StackLimit)
			{
				// 한도 도달: 터뜨리고 스택을 비운다. 이번 적용분은 소비된 것으로 보고 다시 쌓지 않는다.
				ApplyStackFullEffects(TargetASC, EffectClass);
				TargetASC->RemoveActiveGameplayEffectBySourceEffect(EffectClass, SourceASC);
				continue;
			}
		}

		FGameplayEffectSpecHandle ExtraSpec = MakeOutgoingGameplayEffectSpec(EffectClass, GetAbilityLevel());
		if (ExtraSpec.IsValid())
		{
			TargetASC->ApplyGameplayEffectSpecToSelf(*ExtraSpec.Data.Get());
		}
	}
}

// 스택 한도 도달 시의 폭발. 어느 스택형 효과의 짝인지는 데이터가 정한다 -
//  미저작이면 아무 일도 없이 넘어간다(출혈을 안 쓰는 무기가 정상 경로다).
void UVBGA_MeleeAttackBase::ApplyStackFullEffects(UAbilitySystemComponent* TargetASC,
	TSubclassOf<UGameplayEffect> StackingEffect)
{
	if (!TargetASC || !ActiveConfig || !StackingEffect)
	{
		return;
	}

	for (const FVBStackFullEffect& Entry : ActiveConfig->StackFullEffects)
	{
		if (Entry.StackingEffect != StackingEffect)
		{
			continue;
		}

		for (const TSubclassOf<UGameplayEffect>& BurstClass : Entry.BurstEffects)
		{
			if (!BurstClass)
			{
				continue;
			}

			FGameplayEffectSpecHandle BurstSpec = MakeOutgoingGameplayEffectSpec(BurstClass, GetAbilityLevel());
			if (BurstSpec.IsValid())
			{
				TargetASC->ApplyGameplayEffectSpecToSelf(*BurstSpec.Data.Get());
			}
		}

		VB_LOG(Log, "스택 한도 폭발: %s 한도 도달 -> %d종 적용",
			*StackingEffect->GetName(), Entry.BurstEffects.Num());
		return;
	}
}

// 임팩트 쉐이크 프로필 해석: 노티(스윙 입도) -> 공격 leaf(무기 입도).
const UVBSwingImpactProfile* UVBGA_MeleeAttackBase::ResolveSwingImpactProfile() const
{
	// 1순위: 직전 트레이스 노티가 실어 보낸 프로필. 저작 입도가 스윙이라 콤보 타마다 방향이 달라질 수 있다.
	if (ActiveSwingProfile)
	{
		return ActiveSwingProfile;
	}

	// 2순위: 무기 leaf 의 기본 프로필. 입도가 공격당 1개라 콤보 전 타가 같은 방향으로 굳는다 - 그래서 폴백이다.
	if (ActiveConfig)
	{
		return ActiveConfig->DefaultSwingImpactProfile;
	}

	// 셋째 기본값을 두지 않는다 - 미저작이면 아무것도 하지 않는다.
	return nullptr;
}

FVBSwingCombatScales UVBGA_MeleeAttackBase::ResolveSwingCombatScales() const
{
	// 노티가 프로필을 실어 보냈으면 그 배율을 쓴다.
	if (ActiveSwingCombatProfile)
	{
		return ActiveSwingCombatProfile->Scales;
	}

	// 아니면 곱하지 않는다(기본값 1.0). 기본 프로필을 조회하지 않는다 - 같은 배율을 정하는 곳이 둘이 된다.
	return FVBSwingCombatScales();
}

bool UVBGA_MeleeAttackBase::BuildSwingHitContext(FVBSwingHitContext& OutContext) const
{
	const ACharacter* AvatarCharacter = GetAvatarCharacter();
	if (!AvatarCharacter || !ActiveConfig || !ActiveConfig->DamageEffectClass)
	{
		return false;
	}

	const UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo();
	if (!OwnerASC)
	{
		return false;
	}

	const FVector Start = AvatarCharacter->GetActorLocation();

	// 조준 보정: 겨눈 대상이 AimAssistMaxAngle 안이면 판정을 그쪽으로 돌린다(밖이면 정면 그대로).
	FVector Forward = AvatarCharacter->GetActorForwardVector();
	Forward.Z = 0.0f;
	Forward.Normalize();
	if (ActiveConfig->AimAssistMaxAngle > 0.0f)
	{
		if (const USceneComponent* AimTarget = ResolveAimTargetComponent())
		{
			FVector ToTarget = AimTarget->GetComponentLocation() - Start;
			ToTarget.Z = 0.0f;
			if (!ToTarget.IsNearlyZero())
			{
				ToTarget.Normalize();
				const float AngleDeg = FMath::RadiansToDegrees(
					FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, ToTarget), -1.0, 1.0)));
				if (AngleDeg <= ActiveConfig->AimAssistMaxAngle)
				{
					Forward = ToTarget;
				}
			}
		}
	}
	// 판정 창에서는 칼 궤적이 판정이라 이 정면은 넉백 방향과 옛 구 스윕에만 쓰인다. 창이 열릴 때 한 번 정한다(설계 7절).
	OutContext.Forward = Forward;

	// 넉백 방향 - 스윙당 한 번, 판정이 실제로 간 방향(조준 보정된 Forward). 스윙 축은 쓰지 않는다(옆으로 날리면 다음 접근이 흔들린다).
	// 스윙 프로필이 지정한 수평 방향을 쓴다. 띄우기는 LaunchHeight가 정한다.
	FVector KnockbackDir = Forward;
	KnockbackDir.Z = 0.0f;
	OutContext.KnockbackYaw = KnockbackDir.IsNearlyZero() ? 0.0f : KnockbackDir.Rotation().Yaw;

	// 이번 스윙의 전투 수치 배율. 대상 루프 진입 전에 한 번만 해석한다 - 넉백 방향을 루프 밖에서
	//  한 번만 구하는 것과 같은 이유다. 스윙 한 번의 무게는 맞은 대상 수와 무관하게 하나여야 한다.
	OutContext.SwingScales = ResolveSwingCombatScales();

	// 이번 스윙의 반응 등급(스윙 프로필, 없으면 무기 잎) - 스윙당 한 번. 받는 쪽 저항은 피격자 허브가 본다.
	OutContext.SwingImpact = (ActiveSwingCombatProfile && ActiveSwingCombatProfile->bOverrideHitImpact)
		? ActiveSwingCombatProfile->HitImpact
		: ActiveConfig->HitImpact;
	OutContext.SwingLaunchHeight = UVBSwingCombatProfile::ResolveLaunchHeight(ActiveSwingCombatProfile.Get(), ActiveConfig->LaunchHeight);

	// 최종 데미지. 변형 배율을 곱한다 - 타격 수가 다른 변형끼리 총 데미지를 맞추기 위한 것이다(DamageScale 주석 참조).
	// 스윙 배율이 마지막 층이다 - 캐릭터/무기/변형/스윙 네 층은 입도가 각각 달라서 나뉘어 있다.
	OutContext.AttackPower = OwnerASC->GetNumericAttribute(UVBCombatAttributeSet::GetAttackPowerAttribute());
	// 다섯째 층 = 무기 강화. 손에 든 무기의 강화 배수 속성(무기 상태가 강화 효과로 쓴다, 적, 맨손은 기본 1).
	OutContext.WeaponType = GetGrantingWeaponType();
	OutContext.WeaponEnhanceScale = OwnerASC->GetNumericAttribute(UVBCombatAttributeSet::GetWeaponDamageScaleAttribute());
	OutContext.FinalDamage = OutContext.AttackPower * ActiveConfig->DamageMultiplier * ActiveVariantDamage
		* OutContext.SwingScales.DamageScale * OutContext.WeaponEnhanceScale;

	// 노티 프로필은 노티마다 덮어써지는 GA 멤버라, 창이 열린 동안 다른 창이 덮어쓸 수 있다. 이 스윙 것을 붙들어 둔다.
	OutContext.ImpactProfile = ResolveSwingImpactProfile();
	OutContext.CombatProfile = ActiveSwingCombatProfile.Get();
	return true;
}

void UVBGA_MeleeAttackBase::PerformAttackTrace()
{
	// 서버 권위 함수: 판정 + 피해 GE. 클라에서 불려도 입구에서 막는다.
	if (!CurrentActorInfo || !CurrentActorInfo->IsNetAuthority())
	{
		return;
	}
	// 즉시 trace 호출자(옛 notify, trace component/shape 없음, 못 연 창)가 모두 여기를 지나므로 검사 하나로 덮는다
	if (!AllowsImmediateTraceFallback())
	{
		VB_LOG(Warning, "%s: immediate trace refused - this attack damages only through its authored window", *GetName());
		return;
	}

	ACharacter* AvatarCharacter = GetAvatarCharacter();
	FVBSwingHitContext Context;
	if (!AvatarCharacter || !BuildSwingHitContext(Context))
	{
		return;
	}

	// Trace 시작/끝 위치
	const FVector Start = AvatarCharacter->GetActorLocation();
	const FVector& Forward = Context.Forward;
	// 사거리, 반경은 애니 고유 단위라 공격자 캐릭터 배율을 곱한다(UVBAttackConfig::GetScaledAttackRange 주석) - 큰 보스는 같은 클립으로 더 멀리 닿는다.
	const float BodyScale = VBStrideScale::Resolve(AvatarCharacter);
	const float TraceRadius = ActiveConfig->GetScaledAttackRadius(BodyScale);
	const FVector End = Start + (Forward * ActiveConfig->GetScaledAttackRange(BodyScale));

	// SphereTrace 설정
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(AvatarCharacter);

	TArray<FHitResult> HitResults;
	const bool bHit = AvatarCharacter->GetWorld()->SweepMultiByChannel(
		HitResults,
		Start,
		End,
		FQuat::Identity,
		ActiveConfig->HitTraceChannel,
		// 반경도 ActiveConfig 기준이다. AttackConfig 를 직접 읽으면 Moveset 만 할당된 무기에서 null 이 된다.
		FCollisionShape::MakeSphere(TraceRadius),
		QueryParams);


#if ENABLE_DRAW_DEBUG
	// 스윕 전체를 그린다(캡슐 = 스윕 볼륨, 양 끝 구 = 시작, 끝, 명중 지점).
	{
		const FVector Mid = (Start + End) * 0.5f;
		const float Half = FMath::Max((End - Start).Size() * 0.5f, KINDA_SMALL_NUMBER);
		const FQuat Rot = FRotationMatrix::MakeFromZ(Forward).ToQuat();
		const FColor Col = bHit ? FColor::Red : FColor::Green;
		DrawDebugCapsule(AvatarCharacter->GetWorld(), Mid, Half, TraceRadius, Rot, Col, false, 1.0f);
		DrawDebugSphere(AvatarCharacter->GetWorld(), Start, TraceRadius, 12, Col, false, 1.0f);
		DrawDebugSphere(AvatarCharacter->GetWorld(), End, TraceRadius, 12, Col, false, 1.0f);
		for (const FHitResult& H : HitResults)
		{
			if (H.GetActor())
			{
				DrawDebugSphere(AvatarCharacter->GetWorld(), H.ImpactPoint, 12.0f, 8, FColor::Yellow, false, 1.0f);
			}
		}
	}
#endif


	if (!bHit)
	{
		return;
	}


	VB_LOG(Log, "공격 Trace: Hit %d actors", HitResults.Num());

	if (HitResults.Num() == 0)
	{
		return;
	}

	ApplySwingHits(HitResults, Context);
}

void UVBGA_MeleeAttackBase::ApplySwingHits(const TArray<FHitResult>& Hits, FVBSwingHitContext& Context)
{
	// 두 판정 경로 모두 서버에서만 여기 온다. 새 호출자가 생겨도 클라가 피해를 내지 않게 입구에서 한 번 더 막는다.
	if (!CurrentActorInfo || !CurrentActorInfo->IsNetAuthority())
	{
		return;
	}

	ACharacter* AvatarCharacter = GetAvatarCharacter();
	UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo();
	if (!AvatarCharacter || !OwnerASC || !ActiveConfig || !ActiveConfig->DamageEffectClass)
	{
		return;
	}

	// 구 스윕은 한 액터에서 히트가 여럿 나오므로 이번 호출에서 피해를 준 액터를 기억한다.
	TSet<AActor*> ProcessedActors;

	// 표적에 효과를 걸면 이 스윙이 끊길 수 있다(패링 경직 -> EndAbility). 같은 호출 안에서 새 활성화가 켜질 수 있어 번호까지 본다.
	const uint32 SwingActivation = ActivationSerial;
	const auto IsSameSwing = [this, SwingActivation]() { return IsActive() && ActivationSerial == SwingActivation; };
	bool bSwingCut = false;

	for (const FHitResult& Hit : Hits)
	{
		if (!IsSameSwing())
		{
			bSwingCut = true;
			break;
		}

		UAbilitySystemComponent* TargetASC = VBMeleeHitApplication::FindDamageableTarget(Hit, *AvatarCharacter, ProcessedActors);
		if (!TargetASC)
		{
			continue;
		}

		// 등록은 모든 관문을 지난 뒤에 한다. 한 액터의 비관통 히트가 먼저 와도 뒤의 관통 히트를 버리지 않게.
		AActor* HitActor = Hit.GetActor();
		ProcessedActors.Add(HitActor);

		FGameplayEffectSpecHandle SpecHandle = MakeHitSpec(Hit, Context);
		if (!SpecHandle.IsValid())
		{
			continue;
		}

		const bool bDealtDamage = VBMeleeHitApplication::ApplyHitSpec(*TargetASC, SpecHandle);
		if (!IsSameSwing())
		{
			bSwingCut = true;
			break;
		}

		// 피해가 0 인 타격(패링, 회피 무적)에는 추가 효과를 걸지 않는다. 가드 감쇠 타격은 가드 브레이크 단계에서 정한다.
		if (bDealtDamage)
		{
			ApplyAdditionalEffects(TargetASC, Hit);
			if (!IsSameSwing())
			{
				bSwingCut = true;
				break;
			}
		}

		VBMeleeHitPresentation::ReportHitToTargetLock(UVBTargetLockComponent::FindFor(GetAvatarActorFromActorInfo()), Context, HitActor,
			bDealtDamage);
	}

	if (ProcessedActors.IsEmpty())
	{
		return;
	}

	VBMeleeHitApplication::LogSwingDamage(*this, Context, *ActiveConfig, ActiveVariantDamage, ProcessedActors.Num());

	// 히트스톱, 스윙 쉐이크는 스윙의 첫 접촉에서 한 번. 창 동안 여러 번 걸면 스윙이 끊겨 보인다.
	if (!Context.bContactFinished && !bSwingCut)
	{
		Context.bContactFinished = true;
		// 히트스톱은 대상 수와 무관하게 1회만 - 루프 밖 유지. 맞은 액터 전부를 실어 전투원만 멈추게 한다.
		PlayHitStop(ActiveConfig->HitStopDuration * Context.SwingScales.HitStopDurationScale,
			ActiveConfig->HitStopTimeDilation, ProcessedActors.Array(), ActiveConfig);
		VBMeleeHitPresentation::PlaySwingImpact(*this, Context, *ActiveConfig, *AvatarCharacter, *OwnerASC);
	}
}

FGameplayEffectSpecHandle UVBGA_MeleeAttackBase::MakeHitSpec(const FHitResult& Hit, const FVBSwingHitContext& Context) const
{
	// 대상마다 새 Spec 을 만든다. 공유 Context 에 HitResult 를 두 번 넣으면 assert 로 죽는다.
	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(ActiveConfig->DamageEffectClass, GetAbilityLevel());
	if (!SpecHandle.IsValid())
	{
		return SpecHandle;
	}

	FVBGameplayEffectContext* const HitFacts = VBMeleeHitApplication::AddHitToSpec(SpecHandle, Hit, Context, *this);
	if (!HitFacts)
	{
		return FGameplayEffectSpecHandle();
	}

	ConfigureHitContext(*HitFacts, Hit.GetActor());
	VBMeleeHitApplication::FillHitSpec(*SpecHandle.Data.Get(), *HitFacts, Hit, Context, *ActiveConfig);
	return SpecHandle;
}

UVBMeleeTraceComponent* UVBGA_MeleeAttackBase::GetMeleeTraceComponent() const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	return UVBMeleeTraceComponent::FindFor(Avatar);
}

void UVBGA_MeleeAttackBase::BeginTraceWindow(const FGameplayEventData& Payload)
{
	if (!CurrentActorInfo || !CurrentActorInfo->IsNetAuthority())
	{
		return;
	}

	const FGameplayTagContainer Shapes = UVBMeleeTraceComponent::ShapesFromEventTags(Payload.InstigatorTags);

	// 못 여는 경우는 옛 구 스윕으로 떨어진다. 몽타주는 창으로 바꿨는데 그 몽타주를 쓰는 캐릭터에 컴포넌트, 모양이 아직 없을 때다
	//   (변환은 캐릭터별로 나눠 한다). 조용히 피해가 사라지면 '칼이 안 맞는다' 로만 보여 원인을 못 찾는다. 경고는 매 스윙 낸다.
	UVBMeleeTraceComponent* Trace = GetMeleeTraceComponent();
	if (!Trace || Shapes.IsEmpty())
	{
		VB_LOG(Warning, "%s: 판정 창인데 %s - 옛 구 판정으로 대신한다", *GetName(),
			!Trace ? TEXT("아바타에 UVBMeleeTraceComponent 가 없다") : TEXT("노티에 모양 태그가 없다"));
		PerformAttackTrace();
		return;
	}

	FVBSwingHitContext Context;
	if (!BuildSwingHitContext(Context))
	{
		return; // 옛 판정도 이 경우 조용히 끝났다 - 피해를 낼 수 없는 공격(피해 GE 없음 등)
	}
	// 맞은 날이 큐를 정한다(주먹, 발 = 둔기). 맨손에서 무기 날이 손 모양으로 떨어지면 그 손의 큐다 - 풀린 모양에서 읽는다.
	//   부품이 여럿이면 각자 bone 별 큐를 지키고, 처음 풀린 부품의 큐가 창의 기본값이다.
	for (const FGameplayTag& ShapeTag : Shapes)
	{
		FVBResolvedMeleeShape ResolvedShape;
		if (Trace->ResolveShape(ShapeTag, ResolvedShape))
		{
			if (Context.Parts.IsEmpty())
			{
				Context.ImpactCueTag = ResolvedShape.ImpactCueTag;
				Context.StrikeShape = ShapeTag;
			}

			Context.Parts.Add({ResolvedShape.Bone, ShapeTag, ResolvedShape.ImpactCueTag});
		}
	}

	const int32 Key = NextTraceWindowKey++;
	const float RadiusScale = ResolveSwingCombatScales().TraceRadiusScale;
	const int32 WindowId = Trace->BeginWindow(Shapes, Payload.EventMagnitude,
		FVBMeleeTraceHits::CreateUObject(this, &ThisClass::OnTraceWindowHits, Key), RadiusScale);
	if (WindowId == INDEX_NONE)
	{
		// 컴포넌트가 이유를 이미 경고했다(모양 없음, 뼈 없음).
		VB_LOG(Warning, "%s: 판정 창 %s 을 못 열었다 - 옛 구 판정으로 대신한다", *GetName(), *Shapes.ToStringSimple());
		PerformAttackTrace();
		return;
	}

	FOpenTraceWindow& Open = OpenTraceWindows.AddDefaulted_GetRef();
	Open.Key = Key;
	Open.WindowId = WindowId;
	Open.Shapes = Shapes;
	Open.Context = MoveTemp(Context);
	VB_LOG(Log, "%s: 판정 창 열림 - %d타 %s (창 %d, 다단 간격 %.2f초, 판정 폭 x%.2f)", *GetName(), Combo.GetComboIndex() + 1,
		*Shapes.ToStringSimple(), WindowId,
		Payload.EventMagnitude, RadiusScale);
	if (UVBCombatReadoutSubsystem* Readout = UVBCombatReadoutSubsystem::GetActive(this))
	{
		const AActor* Avatar = GetAvatarActorFromActorInfo();
		const USceneComponent* AimTarget = ResolveAimTargetComponent();
		const float Distance = (Avatar && AimTarget) ? FVector::Dist2D(Avatar->GetActorLocation(),
			AimTarget->GetComponentLocation()) : -1.0f;
		const float Stop = AimTarget ? ComputeApproachOffset(AimTarget) : -1.0f;
		// "Melee.Shape.FootL" -> "FootL", 여럿이면 "FootL+FootR"
		FString ShapeNames;
		for (const FGameplayTag& ShapeTag : Shapes)
		{
			ShapeNames += (ShapeNames.IsEmpty() ? TEXT("") : TEXT("+")) + ShapeTag.GetTagName().ToString().RightChop(12);
		}

		Readout->NoteWindowOpen(Avatar, Key, Combo.GetComboIndex() + 1, ShapeNames, Distance, Stop,
			AimTarget ? AimTarget->GetOwner() : nullptr);
	}
}

void UVBGA_MeleeAttackBase::OnTraceWindowHits(const TArray<FHitResult>& Hits, int32 Key)
{
	FOpenTraceWindow* Open = OpenTraceWindows.FindByPredicate([Key](const FOpenTraceWindow& W) { return W.Key == Key; });
	if (!Open)
	{
		return; // 이미 닫은 창
	}

	TArray<FHitResult> Allowed = Hits;
	FilterWindowHits(Key, Allowed);
	// 적중을 모두 걸러낸 필터만 여기서 멈춘다. 빈 창은 그대로 readout 과 ApplySwingHits 까지 간다
	if (Allowed.IsEmpty() && !Hits.IsEmpty())
	{
		return;
	}

	for (const FHitResult& Hit : Allowed)
	{
		VB_LOG(Log, "%s: 판정 창 %d 접촉 - %s 의 %s", *GetName(), Open->WindowId, *GetNameSafe(Hit.GetActor()),
			Hit.BoneName.IsNone() ? TEXT("(뼈 없음)") : *Hit.BoneName.ToString());
	}

	// 적용 전에 적는다 - 적용이 피격 허브를 돌리고, 그 넉백 보고는 이 창에 피해자가 이미 적혀 있어야 한다
	if (UVBCombatReadoutSubsystem* Readout = UVBCombatReadoutSubsystem::GetActive(this))
	{
		Readout->NoteWindowHits(GetAvatarActorFromActorInfo(), Key, Allowed);
	}

	// 사본으로 적용하고 되돌려 쓴다. 적용 도중 이 GA 가 끝날 수 있다(예: 패링이 공격자를 끊음 -> EndAbility -> 창 목록이 비워짐).
	//   목록의 원소를 참조로 전달하면 그때 사라진 메모리를 쓴다.
	FVBSwingHitContext Context = Open->Context;
	ApplySwingHits(Allowed, Context);
	if (FOpenTraceWindow* Still = OpenTraceWindows.FindByPredicate([Key](const FOpenTraceWindow& W) { return W.Key == Key; }))
	{
		Still->Context = Context;
	}
}

void UVBGA_MeleeAttackBase::EndTraceWindowByShapes(const FGameplayTagContainer& Shapes, bool bSweepFinalPose)
{
	// 끝 이벤트는 시작 이벤트와 같은 태그 셋을 실어, 같은 셋이면 같은 notify 의 창이다
	const int32 Index = OpenTraceWindows.IndexOfByPredicate([&Shapes](const FOpenTraceWindow& W) { return W.Shapes == Shapes; });
	if (Index == INDEX_NONE)
	{
		return; // 섹션 전환이 이미 닫았거나, 못 열어서 옛 판정으로 대신한 창
	}

	const int32 WindowId = OpenTraceWindows[Index].WindowId;
	const int32 Key = OpenTraceWindows[Index].Key;
	if (UVBMeleeTraceComponent* Trace = GetMeleeTraceComponent())
	{
		// 마지막 접촉의 피해 문맥은 콜백 동안 살아 있어야 한다. 패링으로 GA 가 끝나면 이 목록도 없어지므로 번호로 다시 찾는다.
		Trace->EndWindow(WindowId, bSweepFinalPose);
	}

	OpenTraceWindows.RemoveAll([Key](const FOpenTraceWindow& W) { return W.Key == Key; });
	if (UVBCombatReadoutSubsystem* Readout = UVBCombatReadoutSubsystem::GetActive(this))
	{
		Readout->NoteWindowClosed(GetAvatarActorFromActorInfo(), Key, /*bCut=*/false);
	}
}

void UVBGA_MeleeAttackBase::EndAllTraceWindows()
{
	if (OpenTraceWindows.Num() == 0)
	{
		return;
	}
	// 목록을 먼저 비운다 - EndWindow 가 무엇을 부르든 이 GA 의 콜백이 옛 창을 다시 찾지 못하게.
	TArray<FOpenTraceWindow> Closing = MoveTemp(OpenTraceWindows);
	OpenTraceWindows.Reset();
	if (UVBMeleeTraceComponent* Trace = GetMeleeTraceComponent())
	{
		for (const FOpenTraceWindow& W : Closing)
		{
			Trace->EndWindow(W.WindowId);
		}
	}

	if (UVBCombatReadoutSubsystem* Readout = UVBCombatReadoutSubsystem::GetActive(this))
	{
		for (const FOpenTraceWindow& W : Closing)
		{
			// 자기 notify 끝이 아니라 섹션 전환, 패링, 회피, 능력 종료로 닫혔다
			Readout->NoteWindowClosed(GetAvatarActorFromActorInfo(), W.Key, /*bCut=*/true);
		}
	}
}
