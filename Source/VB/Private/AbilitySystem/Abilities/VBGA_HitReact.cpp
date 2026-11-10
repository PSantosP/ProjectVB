// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_HitReact.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBHitImpact.h"
#include "Data/VBHitReactConfig.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h" // ProcessRootMotionPostConvertToWorld - 밀림 방향 보정
#include "VB/VB.h"

UVBGA_HitReact::UVBGA_HitReact()
{
	// 입력 GA 가 아니라 InputTag 없음. ActivationPolicy 를 OnSpawn 으로 두면 부여 즉시 켜진다.

	// Event.Combat.Hit 트리거. GiveAbility 만 하면 ASC 가 이벤트 맵에 등록한다.
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = VBGameplayTags::Event_Combat_Hit; // 선언만 있고 사용처가 없던 예약 태그를 여기서 살린다
	AbilityTriggers.Add(Trigger);

	// 이름표. 처형 창이 열릴 때 부모 Ability.Reaction 으로 끊긴다. 플린치를 막는 상태는 전투 규칙 표에 있다.
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Reaction_HitReact));

	// Staggered 는 소유하지 않는다(생산자는 UVBGA_Stagger). 대신 '맞는 중' 태그를 든다 - 적 BT 가 이동 분기를 막는 데 쓴다.
	// ActivationOwnedTags 라 어느 종료 경로로 끝나도 엔진이 뗀다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_HitReacting);

	// 연타마다 반응을 다시 틀려면 true 여야 한다. false 면 활성 중 재활성화가 조용히 거부된다.
	bRetriggerInstancedAbility = true;
}

void UVBGA_HitReact::ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// HitReactConfig 미할당은 설정 오류다 - 무시하면 히트리액트가 흔적 없이 사라진다(BP 클래스 디폴트 누락).
	if (!HitReactConfig)
	{
		VB_LOG(Warning, "%s: HitReactConfig 미할당 -> 히트리액트 스킵. BP_GA_HitReact 에 DA_HitReact_Default 를 할당하라", *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 피격자 = 이 GA 의 아바타. 방향은 공격자(Instigator) 위치를 피격자 로컬로 옮긴 4분면이다.
	// 넉백 요는 조준 보정이 섞인 미는 방향이라 쓰지 않는다. 공격자를 모르면 Front.
	AActor* Avatar = GetAvatarActorFromActorInfo();
	const EVBWeaponType VictimStance = VBResolveVictimStance(Avatar);
	// 실효 등급은 허브가 저항까지 반영해 실었다. 넘어뜨림, 띄움 태그가 와도 무거움 이상으로 읽는다.
	const EVBHitImpact Impact = TriggerEventData ? VBHitImpactFromTags(TriggerEventData->InstigatorTags) : EVBHitImpact::Light;

	// 피격 캐릭터는 어느 등급에서도 돌리지 않고 4분면으로 클립을 고른다(돌리면 뒤에서 맞은 적이 플레이어를 마주 본다).
	// 좌, 우 클립이 없으면 밀린 방향에 맞는 앞, 뒤 클립으로 떨어진다.
	// 이동 담당은 등급마다 하나다: 무거운 클립은 허브의 물리 넉백, 가벼운 루트모션 클립은 아래 보정이 방향을 돌린다.
	float PushYaw = 0.0f;
	const bool bHasPushYaw = TriggerEventData && VBGetPushYaw(*TriggerEventData, PushYaw);
	EVBHitDirection Direction = VBComputeHitDirection(Avatar, TriggerEventData ? TriggerEventData->Instigator.Get() : nullptr);
	if (Impact >= EVBHitImpact::Heavy && bHasPushYaw && Avatar &&
		((Direction == EVBHitDirection::Left && !HitReactConfig->HeavySet.MontageLeft) ||
			(Direction == EVBHitDirection::Right && !HitReactConfig->HeavySet.MontageRight)))
	{
		const bool bPushedForward = FVector::DotProduct(FRotator(0.0f, PushYaw, 0.0f).Vector(), Avatar->GetActorForwardVector()) > 0.0;
		Direction = bPushedForward ? EVBHitDirection::Back : EVBHitDirection::Front;
	}

	UAnimMontage* HitReactMontage = nullptr;
	PlayingImpact = EVBHitImpact::Light;
	++ClipPick.Sequence;
	if (Impact >= EVBHitImpact::Heavy)
	{
		HitReactMontage = HitReactConfig->ResolveHeavyMontage(VictimStance, Direction, &ClipPick);
		if (HitReactMontage)
		{
			PlayingImpact = EVBHitImpact::Heavy;
		}

		if (!HitReactMontage && !bWarnedMissingHeavy)
		{
			// 가벼운 반응으로 떨어진다(무반응보다 낫다). 강타인데 플린치라 경고한다.
			bWarnedMissingHeavy = true;
			VB_LOG(Warning, "%s: %s 의 HeavySet 이 비어 무거운 피격을 가벼운 플린치로 튼다(이 액터에서 첫 1회만 알린다)",
				*GetNameSafe(Avatar), *GetNameSafe(HitReactConfig));
		}
	}

	if (!HitReactMontage)
	{
		HitReactMontage = HitReactConfig->ResolveMontage(VictimStance, Direction, &ClipPick);
	}

	if (!HitReactMontage)
	{
		VB_LOG(Warning, "%s: 히트리액트 몽타주 해석 실패 -> 즉시 종료", *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 서버와 소유 클라가 각자 로컬 재생한다(시뮬 프록시는 RepAnimMontageInfo). 재생 자체는 서버 전용 부작용이 없다.
	// bAllowInterruptAfterBlendOut 은 false 유지 - true 면 자연 블렌드아웃이 잘린다.
	// 밀림 방향 보정은 서버에서만 건다(적 이동은 서버가 관리한다).
	float PushDistance = 0.0f;
	if (TriggerEventData)
	{
		VBGetPushDistance(*TriggerEventData, PushDistance);
	}

	const bool bRedirected = bHasPushYaw && ActorInfo && ActorInfo->IsNetAuthority() &&
		BeginRootMotionRedirect(HitReactMontage, PushYaw, PushDistance);

	VB_LOG(Log, "%s: 히트리액트 %s 방향=%d 등급=%s%s", *GetNameSafe(Avatar), *GetNameSafe(HitReactMontage),
		static_cast<int32>(Direction), VBHitImpactName(Impact),
		bRedirected ? *FString::Printf(TEXT(" (밀림 방향 보정 %.0f도, 거리 x%.2f -> %.0fcm)"), RootMotionRedirectYaw,
			RootMotionPushScale, PushDistance) : TEXT(""));

	UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
				this,
				NAME_None,
				HitReactMontage,
				HitReactConfig->MontagePlayRate,
				NAME_None,
				true,
				1.0f,
				0.0f,
				false);

	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnHitReactEnded);
	MontageTask->OnBlendOut.AddDynamic(this, &ThisClass::OnHitReactEnded);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnHitReactEnded);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnHitReactEnded);
	MontageTask->ReadyForActivation();
}

bool UVBGA_HitReact::ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayEventData* Payload) const
{
	if (!Super::ShouldAbilityRespondToEvent(ActorInfo, Payload))
	{
		return false;
	}
	// 등급 태그가 없는 이벤트(패링 되받기 등 옛 경로)는 Light 로 읽혀 종전처럼 켜진다(VBHitImpactFromTags).
	const EVBHitImpact Incoming = Payload ? VBHitImpactFromTags(Payload->InstigatorTags) : EVBHitImpact::Light;
	if (Incoming == EVBHitImpact::None)
	{
		return false;
	}
	// 지금 반응보다 약한 타격은 반응을 끊지 않는다(피해, 발광, 넉백은 허브에서 그대로 든다).
	return !(IsActive() && Incoming < PlayingImpact);
}

void UVBGA_HitReact::OnHitReactEnded()
{
	// 몽타주 4개 델리게이트 공통 종료. 정리는 EndAbility 가 한다.
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UVBGA_HitReact::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	EndRootMotionRedirect();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

float UVBGA_HitReact::ComputeRootMotionPushScale(float DesiredCm, float ClipCm, float MaxScale)
{
	if (!(DesiredCm > 0.0f) || !(ClipCm >= 1.0f))
	{
		return 1.0f;
	}

	return FMath::Clamp(DesiredCm / ClipCm, 1.0f, FMath::Max(1.0f, MaxScale));
}

bool UVBGA_HitReact::BeginRootMotionRedirect(UAnimMontage* Montage, float PushYaw, float PushDistanceCm)
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Montage || !Movement || !Mesh || !Montage->HasRootMotion())
	{
		return false;
	}
	// 플레이어는 걸지 않는다 - 플레이어 이동은 소유 클라가 예측해 서버만 돌리면 매 타 위치 보정이 난다.
	if (Character->IsPlayerControlled())
	{
		return false;
	}

	// 이 클립이 이 캐릭터에서 미는 방향. 전 구간 루트모션을 재서 월드로 옮긴다(bExtractRootMotion 기본값 false 라 명시).
	FVector ClipLocal;
	if (const FVector* Cached = ClipTranslationCache.Find(Montage))
	{
		ClipLocal = *Cached;
	}
	else
	{
		const FAnimExtractContext Context(0.0, /*bExtractRootMotion*/ true);
		ClipLocal = Montage->ExtractRootMotionFromTrackRange(0.0f, Montage->GetPlayLength(), Context).GetTranslation();
		ClipTranslationCache.Add(Montage, ClipLocal);
	}

	const FVector ClipWorld = Mesh->GetComponentTransform().TransformVectorNoScale(ClipLocal);
	if (FVector2D(ClipWorld.X, ClipWorld.Y).Size() < 1.0f)
	{
		return false; // 수평 이동이 없는 클립 - 돌릴 것이 없다.
	}

	float Correction = static_cast<float>(
		FRotator::NormalizeAxis(PushYaw - FMath::RadiansToDegrees(FMath::Atan2(ClipWorld.Y, ClipWorld.X))));
	// 밀린 방향과 클립이 크게 어긋나면 방향은 그대로 두고 거리만 늘린다.
	if (FMath::Abs(Correction) > HitReactConfig->MaxRedirectDegrees)
	{
		VB_LOG(Log, "%s: 밀림 방향 보정 안 함 - 클립과 밀린 방향이 %.0f도 어긋난다(상한 %.0f)",
			*GetNameSafe(Character), Correction, HitReactConfig->MaxRedirectDegrees);
		Correction = 0.0f;
	}

	const float PushScale = ComputeRootMotionPushScale(
		PushDistanceCm, static_cast<float>(FVector2D(ClipWorld.X, ClipWorld.Y).Size()), HitReactConfig->MaxRootMotionPushScale);
	if (Correction == 0.0f && PushScale == 1.0f)
	{
		return false; // 돌릴 것도 늘릴 것도 없다 - 묶지 않는다.
	}
	// 이 델리게이트는 이 GA 하나만 사용한다(엔진 MotionWarping 은 Pre 쪽을 쓴다). 다른 사용처가 생겼을 때 조용히 빼앗지 않게 덮지 않고 알린다.
	if (Movement->ProcessRootMotionPostConvertToWorld.IsBound() &&
		!Movement->ProcessRootMotionPostConvertToWorld.IsBoundToObject(this))
	{
		VB_LOG(Warning, "%s: CMC ProcessRootMotionPostConvertToWorld 에 다른 주인이 있다 - 밀림 방향 보정을 건너뛴다", *GetNameSafe(Character));
		return false;
	}

	RootMotionRedirectYaw = Correction;
	RootMotionRedirectRotation = FRotator(0.0f, Correction, 0.0f).Quaternion();
	RootMotionPushScale = PushScale;
	RedirectedMontage = Montage;
	RedirectedMovement = Movement;
	Movement->ProcessRootMotionPostConvertToWorld.BindUObject(this, &ThisClass::RedirectRootMotion);
	return true;
}

void UVBGA_HitReact::EndRootMotionRedirect()
{
	if (UCharacterMovementComponent* Movement = RedirectedMovement.Get())
	{
		if (Movement->ProcessRootMotionPostConvertToWorld.IsBoundToObject(this))
		{
			Movement->ProcessRootMotionPostConvertToWorld.Unbind();
		}
	}

	RedirectedMovement.Reset();
	RedirectedMontage.Reset();
	RootMotionRedirectYaw = 0.0f;
	RootMotionRedirectRotation = FQuat::Identity;
	RootMotionPushScale = 1.0f;
}

FTransform UVBGA_HitReact::RedirectRootMotion(const FTransform& WorldRootMotion, UCharacterMovementComponent* Movement,
	float /*DeltaSeconds*/)
{
	// 이 반응 몽타주가 루트모션을 내는 동안만. 공격, 경직 몽타주가 이어받았으면 손대지 않는다.
	const ACharacter* Character = Movement ? Movement->GetCharacterOwner() : nullptr;
	const FAnimMontageInstance* Instance = Character ? Character->GetRootMotionAnimMontageInstance() : nullptr;
	if (!Instance || !RedirectedMontage.IsValid() || Instance->Montage != RedirectedMontage.Get())
	{
		return WorldRootMotion;
	}
	// 수평만 돌리고 늘린다. 수직과 회전은 클립 그대로다.
	const FVector Translation = WorldRootMotion.GetTranslation();
	const FVector Horizontal = RootMotionRedirectRotation.RotateVector(FVector(Translation.X, Translation.Y, 0.0f)) * RootMotionPushScale;
	FTransform Redirected = WorldRootMotion;
	Redirected.SetTranslation(FVector(Horizontal.X, Horizontal.Y, Translation.Z));
	return Redirected;
}
