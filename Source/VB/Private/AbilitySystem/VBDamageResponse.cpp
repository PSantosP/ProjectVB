// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBDamageResponse.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayEffectContext.h"
#include "AbilitySystem/VBHitImpact.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "Character/VBDownedImpactReceiver.h"
#include "Debug/VBCombatReadoutSubsystem.h"
#include "Perception/AISense_Damage.h"
#include "VB/VB.h"

// 값을 읽는 시점이 계약이다: Dead 는 창 이벤트 뒤, Downed 는 넘어짐 이벤트 전, 루트모션은 피격 이벤트 뒤.
namespace
{
	// 공격자 액터(위치, 회전, 팀). context Instigator 는 ASC 소유자라 플레이어가 때리면 PlayerState 이므로
	//  EffectCauser(아바타)를 먼저 쓴다.
	AActor* ResolveAttackerBody(const FGameplayEffectContextHandle& Context)
	{
		return Context.GetEffectCauser() ? Context.GetEffectCauser() : Context.GetInstigator();
	}

	// 피해 이벤트의 공통 칸. 등급과 방향은 보내는 쪽이 덧붙인다.
	FGameplayEventData MakeDamageEvent(const FVBAcceptedDamage& Hit, const FGameplayTag& EventTag, float Magnitude)
	{
		FGameplayEventData Event;
		Event.EventTag = EventTag;
		Event.Instigator = Hit.Spec.GetContext().GetInstigator();
		Event.Target = Hit.Victim.GetAvatarActor();
		Event.EventMagnitude = Magnitude;
		Event.ContextHandle = Hit.Spec.GetContext();
		return Event;
	}

	// 세트가 없는 액터의 속성은 Default 로 읽는다. GetNumericAttribute 는 없는 속성에 0 을 돌려줘 안 밀리게 된다.
	float ReadCombatAttribute(const UAbilitySystemComponent& Victim, const FGameplayAttribute& Attribute, float Default)
	{
		return Victim.HasAttributeSetForAttribute(Attribute) ? Victim.GetNumericAttribute(Attribute) : Default;
	}

	// 체력 문턱 처형 창 요청. 문턱 판정은 창 GA 가 하고 여기는 남은 체력 비율만 싣는다. 이 타격이 창을 켰으면 true.
	// 막힌 타격도 감쇠된 피해로 문턱을 넘길 수 있어 가드는 보지 않는다.
	bool RequestHealthFinisherWindow(const FVBAcceptedDamage& Hit)
	{
		UAbilitySystemComponent* const OwnerASC = &Hit.Victim;
		if (UVBAbilitySystemStatics::IsFinisherWindowArmed(OwnerASC) || UVBAbilitySystemStatics::IsDead(OwnerASC)
			|| Hit.MaxHealth <= 0.0f || Hit.HealthDamage <= 0.0f
			|| OwnerASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_PostureExecutable))
		{
			return false;
		}

		FGameplayEventData WindowEvent = MakeDamageEvent(Hit, VBGameplayTags::Event_Combat_FinisherWindow, Hit.HealthAfter / Hit.MaxHealth);
		OwnerASC->HandleGameplayEvent(VBGameplayTags::Event_Combat_FinisherWindow, &WindowEvent);
		return UVBAbilitySystemStatics::IsFinisherWindowArmed(OwnerASC);
	}

	// 한 타격의 반응 값. 넘어짐, 플린치, 넉백이 같은 등급과 방향을 쓰게 한곳에 모은다.
	struct FVBHitReaction
	{
		AActor* Attacker = nullptr;
		ACharacter* VictimChar = nullptr;
		EVBHitImpact DeclaredImpact = EVBHitImpact::Light;
		float ImpactResistance = 0.0f;
		// 저항만 반영한 등급. 쓰러진 캐릭터는 넘어짐 GA 가 막혀도 타격이 약해진 것이 아니므로 이 값을 쓴다.
		EVBHitImpact ResolvedImpact = EVBHitImpact::Light;
		// 넘어짐 GA 가 받지 못하면 Heavy 로 내려간 등급. 플린치, 물리 피격, 이동이 쓴다.
		EVBHitImpact Impact = EVBHitImpact::Light;
		// 수평 단위 벡터. 넉백, 반응 GA, 넘어짐이 한 방향이 되게 한 번만 정한다.
		FVector PushDir = FVector::ZeroVector;
		// 비근접 피해는 스펙에 방향을 싣지 않는다.
		bool bSpecHasPushYaw = false;
		bool bVictimDead = false;
		// 넘어짐 이벤트가 Downed 를 붙이기 전의 값. 뒤에 읽으면 방금 넘어뜨린 캐릭터의 이동이 통째로 빠진다.
		bool bDownedBeforeHit = false;
		int32 KnockdownTaken = 0;
		float KnockStrength = 0.0f;
		float LaunchHeight = 0.0f;
		float EstimatedPushCm = 0.0f;
	};

	// 공격자가 스펙에 실은 판정 방향을 쓴다. 여기서는 조준 보정과 스윙 축이 안 보여 공격자 정면은 예비 경로다.
	FVector ResolvePushDirection(const FVBAcceptedDamage& Hit, const FVBGameplayEffectContext* HitFacts,
		const AActor* Attacker, bool& bOutFromSpec)
	{
		FVector PushDir = FVector::ZeroVector;
		float KnockYaw = 0.0f;
		if (HitFacts && HitFacts->GetPushYaw(KnockYaw))
		{
			PushDir = FRotator(0.0f, KnockYaw, 0.0f).Vector();
			bOutFromSpec = true;
		}

		if (PushDir.IsNearlyZero() && Attacker)
		{
			PushDir = Attacker->GetActorForwardVector();
			PushDir.Z = 0.0f;
		}

		const AActor* Victim = Hit.Victim.GetAvatarActor();
		if (PushDir.IsNearlyZero() && Attacker && Victim)
		{
			// 정면이 퇴화한 경우(액터 회전이 수직 등)에만 위치선을 쓴다.
			PushDir = Victim->GetActorLocation() - Attacker->GetActorLocation();
			PushDir.Z = 0.0f;
		}

		return PushDir.GetSafeNormal();
	}

	// 사실을 싣지 않은 효과(비근접, 기본형 컨텍스트)는 Light / 방향 없음 / 높이 0 으로 읽는다.
	FVBHitReaction ResolveHitReaction(const FVBAcceptedDamage& Hit, const FVBGameplayEffectContext* HitFacts)
	{
		UAbilitySystemComponent* const OwnerASC = &Hit.Victim;
		FVBHitReaction Reaction;
		Reaction.Attacker = ResolveAttackerBody(Hit.Spec.GetContext());
		Reaction.VictimChar = Cast<ACharacter>(OwnerASC->GetAvatarActor());
		Reaction.DeclaredImpact = HitFacts ? HitFacts->GetDeclaredHitImpact() : EVBHitImpact::Light;
		Reaction.ImpactResistance = ReadCombatAttribute(*OwnerASC, UVBCombatAttributeSet::GetImpactResistanceAttribute(), 0.0f);
		Reaction.ResolvedImpact = VBResolveHitImpact(Reaction.DeclaredImpact, Reaction.ImpactResistance);
		Reaction.Impact = Reaction.ResolvedImpact;
		Reaction.bVictimDead = UVBAbilitySystemStatics::IsDead(OwnerASC);
		Reaction.bDownedBeforeHit = UVBAbilitySystemStatics::IsDowned(OwnerASC);
		Reaction.PushDir = ResolvePushDirection(Hit, HitFacts, Reaction.Attacker, Reaction.bSpecHasPushYaw);
		return Reaction;
	}

	// 넘어뜨림 이상은 넘어짐 GA 에 먼저 묻고 받지 못하면 Heavy 로 내린다.
	// Hit 보다 먼저 보내야 넘어짐 GA 가 붙인 Downed 가 뒤따르는 플린치를 막는다.
	void SendKnockdownEvent(const FVBAcceptedDamage& Hit, FVBHitReaction& Reaction)
	{
		if (Reaction.Impact < EVBHitImpact::Knockdown || Reaction.bVictimDead)
		{
			return;
		}

		FGameplayEventData DownEvent = MakeDamageEvent(Hit, VBGameplayTags::Event_Combat_KnockedDown, Hit.Damage);
		DownEvent.InstigatorTags.AddTag(VBHitImpactToTag(Reaction.Impact));
		if (Reaction.bSpecHasPushYaw && !Reaction.PushDir.IsNearlyZero())
		{
			VBSetPushYaw(DownEvent, Reaction.PushDir.Rotation().Yaw);
		}

		Reaction.KnockdownTaken = Hit.Victim.HandleGameplayEvent(VBGameplayTags::Event_Combat_KnockedDown, &DownEvent);
		if (Reaction.KnockdownTaken == 0)
		{
			Reaction.Impact = EVBHitImpact::Heavy;
		}
	}

	// 등급이 None 이어도 보낸다. 엎드린 캐릭터의 다운 피격(UVBGA_Knockdown 의 Hit 구독)은 등급과 무관하게 반응한다.
	void SendHitEvent(const FVBAcceptedDamage& Hit, const FVBGameplayEffectContext* HitFacts, FVBHitReaction& Reaction)
	{
		FGameplayEventData EventData = MakeDamageEvent(Hit, VBGameplayTags::Event_Combat_Hit, Hit.Damage);
		EventData.InstigatorTags.AddTag(VBHitImpactToTag(Reaction.Impact));
		// 넉백 세기와 높이는 넘어짐의 등급 내림 뒤, 이벤트 전에 읽는다. 반응 GA 와 이동이 같은 값을 쓴다.
		const float KnockStrengthRaw = Hit.Spec.GetSetByCallerMagnitude(
			VBGameplayTags::Data_KnockbackStrength, /*WarnIfNotFound=*/false, /*DefaultIfNotFound=*/0.0f)
			* ReadCombatAttribute(Hit.Victim, UVBCombatAttributeSet::GetKnockbackTakenScaleAttribute(), 1.0f);
		// 음수, NaN 세기는 뒤로 당기거나 속도를 깨뜨리므로 0 으로 읽는다.
		Reaction.KnockStrength = FMath::IsFinite(KnockStrengthRaw) ? FMath::Max(0.0f, KnockStrengthRaw) : 0.0f;
		Reaction.LaunchHeight = Reaction.Impact == EVBHitImpact::Launch && HitFacts ? HitFacts->GetLaunchHeight() : 0.0f;
		if (Reaction.bSpecHasPushYaw && !Reaction.PushDir.IsNearlyZero())
		{
			VBSetPushYaw(EventData, Reaction.PushDir.Rotation().Yaw);
			// 넉백이 물리로 밀었다면 갔을 거리. 루트모션 피격 클립이 이 거리까지 늘린다.
			const UCharacterMovementComponent* VictimMove = Reaction.VictimChar ? Reaction.VictimChar->GetCharacterMovement() : nullptr;
			if (VictimMove && Reaction.KnockStrength > 0.0f)
			{
				Reaction.EstimatedPushCm = VBEstimateKnockbackDistance(Reaction.KnockStrength, Reaction.LaunchHeight,
					FMath::Abs(VictimMove->GetGravityZ()), VictimMove->BrakingDecelerationWalking);
				VBSetPushDistance(EventData, Reaction.EstimatedPushCm);
			}
		}

		Hit.Victim.HandleGameplayEvent(VBGameplayTags::Event_Combat_Hit, &EventData);
	}

	// 물리 피격 층(연출). 엎드린 캐릭터, 죽은 캐릭터는 전신을 몽타주나 래그돌이 점유하므로 쏘지 않는다.
	// 넘어짐이 받은 타는 쏜다. 클라이언트의 앞 타 출렁임을 끄는 신호라 안 쏘면 넘어짐 몽타주 위에 상체 시뮬레이션이 남는다.
	void SendHitPhysicsCue(const FVBAcceptedDamage& Hit, const FVBHitReaction& Reaction)
	{
		const bool bPhysicsStop = Reaction.KnockdownTaken > 0;
		if (Reaction.bVictimDead || Reaction.bDownedBeforeHit || Reaction.Impact == EVBHitImpact::None
			|| (!bPhysicsStop && Reaction.PushDir.IsNearlyZero()))
		{
			return;
		}

		FGameplayCueParameters PhysicsCueParams;
		PhysicsCueParams.EffectContext = Hit.Spec.GetContext();
		PhysicsCueParams.Normal = Reaction.PushDir;
		PhysicsCueParams.RawMagnitude = static_cast<float>(Reaction.Impact);
		Hit.Victim.ExecuteGameplayCue(VBGameplayTags::GameplayCue_Combat_HitPhysics, PhysicsCueParams);
	}

	// 살아 있고 엎드리지 않은 대상만 움직인다. 연속 타격은 합산하지 않고 마지막 수평 속도로 갱신한다.
	FVBHitMovement ApplyHitMovement(const FVBHitReaction& Reaction, bool bClipOwnsHorizontal)
	{
		FVBHitMovement Movement;
		ACharacter* const VictimChar = Reaction.VictimChar;
		UCharacterMovementComponent* VictimMove = VictimChar ? VictimChar->GetCharacterMovement() : nullptr;
		if (!VictimMove || Reaction.bVictimDead || Reaction.bDownedBeforeHit)
		{
			return Movement;
		}

		Movement = VBResolveHitMovement(Reaction.PushDir, Reaction.KnockStrength, bClipOwnsHorizontal, Reaction.LaunchHeight,
			FMath::Abs(VictimMove->GetGravityZ()));
		if (Movement.Mode == EVBHitMovement::Push)
		{
			// 수직 속도와 이동 모드는 보존한다.
			VictimMove->Velocity.X = Movement.Velocity.X;
			VictimMove->Velocity.Y = Movement.Velocity.Y;
			VictimMove->UpdateComponentVelocity();
		}
		else if (Movement.Mode == EVBHitMovement::Launch)
		{
			VictimChar->LaunchCharacter(Movement.Velocity, Movement.Velocity.SizeSquared2D() > UE_KINDA_SMALL_NUMBER, true);
		}

		if (UVBCombatReadoutSubsystem* Readout = UVBCombatReadoutSubsystem::GetActive(VictimChar))
		{
			const TCHAR* Mode = Movement.Mode == EVBHitMovement::Launch ? TEXT("launch")
				: Movement.Mode == EVBHitMovement::Push ? TEXT("push") : bClipOwnsHorizontal ? TEXT("clip") : TEXT("none");
			Readout->NoteKnockback(VictimChar, Reaction.Attacker, Reaction.KnockStrength, Reaction.EstimatedPushCm, Mode);
		}

		return Movement;
	}

	// 물리로 쓰러진 캐릭터(처형을 버텨낸 래그돌)는 넉백 대신 맞은 자리에 충격을 받는다. 밀지 말지와 세기는 받는 쪽이 정한다.
	float ApplyDownedImpact(const FVBAcceptedDamage& Hit, const FVBHitReaction& Reaction)
	{
		IVBDownedImpactReceiver* const DownedBody = Cast<IVBDownedImpactReceiver>(Hit.Victim.GetAvatarActor());
		if (!DownedBody || Reaction.bVictimDead || !Reaction.bDownedBeforeHit || !DownedBody->IsBodyDownedInPhysics())
		{
			return 0.0f;
		}

		return DownedBody->ReceiveDownedImpact(Hit.Spec.GetContext().GetHitResult(), Reaction.PushDir, Reaction.ResolvedImpact,
			ReadCombatAttribute(Hit.Victim, UVBCombatAttributeSet::GetKnockbackTakenScaleAttribute(), 1.0f));
	}

	// 보내는 쪽 로그(「선언 등급」)와 대조하면 플린치가 된 까닭이 저항인지, 넘어짐 GA 인지, 클립인지 로그만으로 갈린다.
	void LogHitReaction(const FVBAcceptedDamage& Hit, const FVBHitReaction& Reaction, const FVBHitMovement& Movement,
		bool bClipOwnsHorizontal, float DownedImpactSpeed)
	{
		VB_LOG(Log, "반응[%s]: 선언 %s -> 실효 %s (저항 %.1f) / 넘어짐 GA %d / 이동 %s 수평 %.0f 수직 %.0f",
			*GetNameSafe(Hit.Victim.GetAvatarActor()), VBHitImpactName(Reaction.DeclaredImpact), VBHitImpactName(Reaction.Impact),
			Reaction.ImpactResistance, Reaction.KnockdownTaken,
			(!Reaction.VictimChar || Reaction.bVictimDead) ? TEXT("없음(사망)")
			: Reaction.bDownedBeforeHit ? (DownedImpactSpeed > 0.0f ? TEXT("충격(누운 몸)") : TEXT("없음(엎드림)"))
			: bClipOwnsHorizontal ? TEXT("클립") : (Movement.Mode != EVBHitMovement::None ? TEXT("넉백") : TEXT("없음")),
			Movement.Velocity.Size2D(), Movement.Velocity.Z);
	}

	// 막지 않은 피격의 반응. 순서가 계약이다: 넘어짐 -> 플린치 -> 물리 피격 -> 이동 -> 쓰러진 캐릭터 충격.
	void ApplyHitReaction(const FVBAcceptedDamage& Hit)
	{
		const FVBGameplayEffectContext* const HitFacts = FVBGameplayEffectContext::Get(Hit.Spec.GetContext());
		FVBHitReaction Reaction = ResolveHitReaction(Hit, HitFacts);
		SendKnockdownEvent(Hit, Reaction);
		SendHitEvent(Hit, HitFacts, Reaction);
		SendHitPhysicsCue(Hit, Reaction);
		// 피격 이벤트가 켠 몽타주까지 보려고 이벤트 뒤에 읽는다.
		const bool bClipOwnsHorizontal = Reaction.VictimChar && Reaction.VictimChar->IsPlayingRootMotion();
		const FVBHitMovement Movement = ApplyHitMovement(Reaction, bClipOwnsHorizontal);
		const float DownedImpactSpeed = ApplyDownedImpact(Hit, Reaction);
		LogHitReaction(Hit, Reaction, Movement, bClipOwnsHorizontal, DownedImpactSpeed);
	}
}

void VBDamageResponse::Respond(const FVBAcceptedDamage& Hit)
{
	UAbilitySystemComponent* const OwnerASC = &Hit.Victim;
	if (!OwnerASC->IsOwnerActorAuthoritative())
	{
		return;
	}

	// 체력 문턱 창 요청이 반응보다 먼저다. 이 타격이 창을 켰거나 창이 켜진 적에게 온 처형 피해면 반응, 넉백을 내지 않는다 -
	//   준비 자세와 처형 장면이 움직임을 정한다. 창이 켜진 적에게 온 일반 타격은 반응한다.
	//   기절 게이지로 여는 창은 이 함수 뒤(게이지 세트)에서 열리고 열 때 반응을 끊는다.
	const bool bSkipReaction = RequestHealthFinisherWindow(Hit)
		|| (UVBAbilitySystemStatics::IsFinisherWindowArmed(OwnerASC) && UVBAbilitySystemStatics::IsFinisherDamage(Hit.Spec));

	// 맞으면 시야 밖이어도 피해자 AI 에게 공격자가 드러난다. 피해가 실제로 들어간 여기서 알린다(가드한 타격도).
	//  AI handler 는 적대 출처만 남기고, AI listener 가 없는 피해자는 무시한다.
	if (AActor* Attacker = ResolveAttackerBody(Hit.Spec.GetContext()); Attacker && OwnerASC->GetAvatarActor())
	{
		AActor* Victim = OwnerASC->GetAvatarActor();
		UAISense_Damage::ReportDamageEvent(Victim, Victim, Attacker, Hit.Damage, Attacker->GetActorLocation(), Victim->GetActorLocation());
	}

	// 반응 트리거 - 받는 쪽에서 쏘므로 회피 무적 등 받지 않은 타격은 여기 닿지도 않고, 대상마다 한 번씩 돈다(서버 권위 전용).
	//  사망 타격도 보내지만 State.Dead 가 먼저 붙어 UVBGA_HitReact 가 막힌다. 막았으면(bGuarded) 플린치, 넉백을 쏘지 않는다.
	if (!Hit.bGuarded)
	{
		if (bSkipReaction)
		{
			VB_LOG(Log, "반응[%s]: 창을 연 타격 또는 처형 피해 - 반응·넉백 없음", *GetNameSafe(OwnerASC->GetAvatarActor()));
		}
		else
		{
			ApplyHitReaction(Hit);
		}

		// 피 이펙트 큐 - 공격이 컨텍스트에 실어 보낸 것(SetImpactCue). 플래시와 같은 이유로 여기서 쏜다: 막거나 피하거나
		//   받지 않은 접촉에는 피가 나지 않는다. 비었으면 근접이 아닌 피해다.
		const FVBGameplayEffectContext* const HitFacts = FVBGameplayEffectContext::Get(Hit.Spec.GetContext());
		if (HitFacts && HitFacts->GetImpactCue().IsValid())
		{
			FGameplayCueParameters ImpactCueParams;
			ImpactCueParams.EffectContext = Hit.Spec.GetContext();
			OwnerASC->ExecuteGameplayCue(HitFacts->GetImpactCue(), ImpactCueParams);
		}

		// 피격 플래시 큐(순수 코스메틱) - 받은 타격 안쪽이라 회피/패링에는 저절로 안 번쩍이고, 피격자 ASC 로 쏴 MyTarget 이 곧 피격자다.
		//  킬링블로우도 일부러 번쩍인다(Dead 게이트 없음). 무기 메시는 별도 컴포넌트라 대상이 아니다.
		FGameplayCueParameters FlashCueParams;
		FlashCueParams.RawMagnitude = Hit.Damage; // 컴포넌트가 데미지->강도 커브 입력으로 쓴다
		FlashCueParams.EffectContext = Hit.Spec.GetContext();
		OwnerASC->ExecuteGameplayCue(VBGameplayTags::GameplayCue_Combat_HitFlash, FlashCueParams);
	}
}
