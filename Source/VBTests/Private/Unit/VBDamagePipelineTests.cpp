// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Unit/VBDamagePipelineTestVictim.h"

#include "Misc/AutomationTest.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AI/VBAIController.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/VBGameplayEffectContext.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBHitImpact.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBParryConfig.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "Misc/ScopeExit.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense_Damage.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

void AVBDamagePipelineTestVictim::PlayGuardAccept(EVBHitDirection Side)
{
	if (Log)
	{
		Log->Add(TEXT("guard accept"));
	}
}

AVBDamagePipelineTestVictim::AVBDamagePipelineTestVictim()
{
	ParryConfig = CreateDefaultSubobject<UVBParryConfig>(TEXT("ParryConfig"));
	ParryConfig->ParryPoiseDamage = 0.0f;
}

const UVBParryConfig* AVBDamagePipelineTestVictim::GetParryConfig() const
{
	return ParryConfig;
}

void AVBDamagePipelineTestVictim::OpenRiposteWindow(float Duration, AActor* Attacker)
{
	if (Log)
	{
		Log->Add(FString::Printf(TEXT("guard riposte %.2f %s"), Duration, *GetNameSafe(Attacker)));
	}
}

void AVBDamagePipelineTestVictim::ApplyGuardStaminaDelta(float Delta)
{
	if (Log)
	{
		Log->Add(FString::Printf(TEXT("guard stamina %+.1f"), Delta));
	}
}

FVBGuardResult AVBDamagePipelineTestVictim::ResolveIncomingHit(UAbilitySystemComponent& VictimASC, const FGameplayEffectSpec& Spec,
	float Damage)
{
	if (!bOwnGuardRule)
	{
		return IVBGuardable::ResolveIncomingHit(VictimASC, Spec, Damage);
	}

	if (Log)
	{
		Log->Add(TEXT("guard own rule"));
	}

	FVBGuardResult Result;
	Result.Damage = 1.0f;
	Result.bGuarded = true;
	return Result;
}

void AVBDamagePipelineTestVictim::HandleGameplayCue(UObject* Self, FGameplayTag GameplayCueTag, EGameplayCueEvent::Type EventType,
	const FGameplayCueParameters& Parameters)
{
	if (Log && EventType == EGameplayCueEvent::Executed)
	{
		Log->Add(FString::Printf(TEXT("cue %s %.1f"), *GameplayCueTag.ToString(), Parameters.RawMagnitude));
		LastCue = Parameters;
	}
}

#if WITH_DEV_AUTOMATION_TESTS

// 타격 한 번이 하는 일 - 수치와, 피해 허브가 보내는 모든 event, cue, guard 호출의 순서를 못박는다. 피해 효과는 출하본(DA_Attack_Fighter_Light)이다.
//   이 피해자는 기절 게이지로 여는 적이 아니라(State.Combat.PostureExecutable 없음) 허브가 체력 문턱 창 요청을 보낸다.
//   AI damage sense, 난이도, 안 죽기 바닥은 각자 시험이 보고, 죽음 처리는 대역 줄(22)이 있다.
namespace VBDamagePipelineTest
{
	struct FHit
	{
		float Damage = 10.0f;
		float Poise = 0.0f;
		EVBHitImpact Impact = EVBHitImpact::Light;
		float KnockStrength = 300.0f;
		float KnockYaw = 90.0f;
		bool bExecution = false;
		bool bUnblockable = false;
		// 휘두름이 고르는 gore cue(FVBGameplayEffectContext::SetImpactCue) - 옛 로그를 못박는 줄에서는 비어 있다.
		FGameplayTag ImpactCue;
	};

	FString Ev(const FGameplayTag& Tag, float Magnitude) { return FString::Printf(TEXT("event %s %.2f"), *Tag.ToString(), Magnitude); }
	FString Cue(const FGameplayTag& Tag, float Raw) { return FString::Printf(TEXT("cue %s %.1f"), *Tag.ToString(), Raw); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDamagePipelineTest, "VB.Unit.Damage.Pipeline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDamagePipelineTest::RunTest(const FString& Parameters)
{
	using namespace VBDamagePipelineTest;
	namespace Tags = VBGameplayTags;

	const UVBAttackConfig* Attack = LoadObject<UVBAttackConfig>(nullptr,
		TEXT("/Game/VB/Data/DA_Attack_Fighter_Light.DA_Attack_Fighter_Light"));
	if (!TestNotNull(TEXT("shipped attack config"), Attack) || !TestNotNull(TEXT("its damage effect"), Attack->DamageEffectClass.Get()))
	{
		return false;
	}

	const UGameplayEffect* DamageEffect = Attack->DamageEffectClass->GetDefaultObject<UGameplayEffect>();
	const UVBParryConfig* Parry = GetDefault<UVBParryConfig>();

	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		OwnedPackage->SetDirtyFlag(false);
	};
	World->InitializeActorsForPlay(FURL());

	TArray<FString> Log;

	// 피해자 = 적 모양(ASC 주인 = avatar), health, combat set.
	AVBDamagePipelineTestVictim* Victim = World->SpawnActor<AVBDamagePipelineTestVictim>();
	ACharacter* Attacker = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("victim"), Victim) || !TestNotNull(TEXT("attacker"), Attacker))
	{
		return false;
	}

	Victim->Log = &Log;
	auto MakeASC = [](AActor* Owner)
	{
		UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Owner);
		Owner->AddInstanceComponent(ASC);
		ASC->RegisterComponent();
		ASC->InitAbilityActorInfo(Owner, Owner);
		return ASC;
	};
	UAbilitySystemComponent* VictimASC = MakeASC(Victim);
	UAbilitySystemComponent* AttackerASC = MakeASC(Attacker);
	// damage execution 이 공격자의 BaseDamage 를 잡는다 - 실제 공격자는 모두 combat set 을 가진다(player state, 적 기반).
	AttackerASC->AddAttributeSetSubobject(NewObject<UVBCombatAttributeSet>(Attacker));
	UVBHealthAttributeSet* Health = NewObject<UVBHealthAttributeSet>(Victim);
	VictimASC->AddAttributeSetSubobject(Health);
	UVBCombatAttributeSet* Combat = NewObject<UVBCombatAttributeSet>(Victim);
	VictimASC->AddAttributeSetSubobject(Combat);

	// 허브가 양쪽에 보낼 수 있는 모든 event 를 cue, guard 호출과 같은 로그에 적는다.
	const FGameplayTag VictimEvents[] = { Tags::Event_Combat_FinisherWindow, Tags::Event_Combat_KnockedDown, Tags::Event_Combat_Hit, Tags::Event_Combat_PoiseBroken };
	TArray<TPair<FGameplayTag, FDelegateHandle>> VictimHandles;
	for (const FGameplayTag& Tag : VictimEvents)
	{
		VictimHandles.Emplace(Tag,
			VictimASC->GenericGameplayEventCallbacks.FindOrAdd(Tag).AddLambda([&Log](const FGameplayEventData* Payload)
		{
			FString Line = Ev(Payload->EventTag, Payload->EventMagnitude);
			for (const FGameplayTag& Extra : Payload->InstigatorTags)
			{
				Line += TEXT(" ") + Extra.ToString();
			}
			// 반응 ability 가 받는 밀기(TargetData): 방향과 넉백 거리 추정.
			float Yaw = 0.f, Distance = 0.f;
			if (VBGetPushYaw(*Payload, Yaw))
			{
				Line += FString::Printf(TEXT(" yaw=%.1f"), Yaw);
			}

			if (VBGetPushDistance(*Payload, Distance))
			{
				Line += FString::Printf(TEXT(" dist=%.1f"), Distance);
			}

			Log.Add(Line);
		}));
	}

	FGameplayEventData LastAttackerEvent;
	const FDelegateHandle AttackerHandle = AttackerASC->GenericGameplayEventCallbacks.FindOrAdd(Tags::Event_Combat_Hit).AddLambda([&Log, &LastAttackerEvent](const FGameplayEventData* Payload)
	{
		Log.Add(TEXT("attacker ") + Ev(Payload->EventTag, Payload->EventMagnitude));
		LastAttackerEvent = *Payload;
	});
	ON_SCOPE_EXIT
	{
		for (const TPair<FGameplayTag, FDelegateHandle>& Pair : VictimHandles)
		{
			VictimASC->GenericGameplayEventCallbacks.FindOrAdd(Pair.Key).Remove(Pair.Value);
		}

		AttackerASC->GenericGameplayEventCallbacks.FindOrAdd(Tags::Event_Combat_Hit).Remove(AttackerHandle);
	};

	TArray<FGameplayTag> LooseTags;
	auto Reset = [&](float StartHealth, float Shield, float MaxPoise, float DamageTakenScale)
	{
		for (const FGameplayTag& Tag : LooseTags)
		{
			VictimASC->RemoveLooseGameplayTag(Tag);
		}

		LooseTags.Reset();
		VictimASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), 100.0f);
		VictimASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), StartHealth);
		VictimASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxShieldAttribute(), 50.0f);
		VictimASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetShieldAttribute(), Shield);
		VictimASC->SetNumericAttributeBase(UVBCombatAttributeSet::GetMaxPoiseAttribute(), MaxPoise);
		VictimASC->SetNumericAttributeBase(UVBCombatAttributeSet::GetPoiseAttribute(), MaxPoise);
		VictimASC->SetNumericAttributeBase(UVBCombatAttributeSet::GetDamageTakenScaleAttribute(), DamageTakenScale);
		Victim->bGuarding = false;
		Victim->GuardElapsed = 0.0f;
		// 각 사례는 이전 타격의 이동 상태를 남기지 않는다.
		Victim->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Victim->GetCharacterMovement()->PendingLaunchVelocity = FVector::ZeroVector;
		Victim->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		Log.Reset();
	};
	auto AddTag = [&](const FGameplayTag& Tag) { VictimASC->AddLooseGameplayTag(Tag); LooseTags.Add(Tag); };
	auto Apply = [&](const FHit& Hit)
	{
		FGameplayEffectContextHandle Context = AttackerASC->MakeEffectContext();
		// 타격 사실은 근접 생산자가 쓰는 것처럼 context 에 실린다.
		if (FVBGameplayEffectContext* Facts = FVBGameplayEffectContext::Get(Context))
		{
			Facts->SetPushYaw(Hit.KnockYaw);
			Facts->SetDeclaredHitImpact(Hit.Impact);
			Facts->SetLaunchHeight(0.0f);
			Facts->SetImpactCue(Hit.ImpactCue);
		}
		else
		{
			AddError(TEXT("MakeEffectContext did not give FVBGameplayEffectContext - AbilitySystemGlobalsClassName is not set"));
		}

		FGameplayEffectSpec Spec(DamageEffect, Context, 1.0f);
		Spec.SetSetByCallerMagnitude(Tags::Effect_Damage_Physical, Hit.Damage);
		Spec.SetSetByCallerMagnitude(Tags::Data_KnockbackStrength, Hit.KnockStrength);
		Spec.SetSetByCallerMagnitude(Tags::Data_PoiseDamage, Hit.Poise);
		if (Hit.bExecution)
		{
			Spec.AddDynamicAssetTag(Tags::Effect_Execute);
		}

		if (Hit.bUnblockable)
		{
			Spec.AddDynamicAssetTag(Tags::Attack_Answer_DodgeOnly);
		}

		VictimASC->ApplyGameplayEffectSpecToSelf(Spec);
	};
	auto HealthNow = [&] { return VictimASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()); };
	auto PoiseNow = [&] { return VictimASC->GetNumericAttribute(UVBCombatAttributeSet::GetPoiseAttribute()); };
	auto ExpectLog = [&](const TCHAR* Case, const TArray<FString>& Expected)
	{
		const FString Got = FString::Join(Log, TEXT(" | "));
		const FString Want = FString::Join(Expected, TEXT(" | "));
		TestEqual(FString::Printf(TEXT("%s: event/cue/guard order"), Case), Got, Want);
	};
	const FString Light = VBHitImpactToTag(EVBHitImpact::Light).ToString();
	const FString Heavy = VBHitImpactToTag(EVBHitImpact::Heavy).ToString();
	const FString Knockdown = VBHitImpactToTag(EVBHitImpact::Knockdown).ToString();
	const FVector Push = FRotator(0.0f, 90.0f, 0.0f).Vector();
	const float Gravity = FMath::Abs(Victim->GetCharacterMovement()->GetGravityZ());
	const FString PushText = FString::Printf(TEXT(" yaw=%.1f"), Push.Rotation().Yaw);
	const FString PushDistText = PushText + FString::Printf(TEXT(" dist=%.1f"),
		VBEstimateKnockbackDistance(300.0f, 0.0f, Gravity, Victim->GetCharacterMovement()->BrakingDecelerationWalking));
	auto HitLine = [&](float Magnitude, const FString& Impact) { return Ev(Tags::Event_Combat_Hit, Magnitude) + TEXT(" ") + Impact + PushDistText; };

	// 1. 보통 타격: 창 요청 -> 반응 -> physics cue -> 넉백 -> flash.
	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	Apply(FHit());
	TestEqual(TEXT("plain: health"), HealthNow(), 90.0f, 0.01f);
	ExpectLog(TEXT("plain"), { Ev(Tags::Event_Combat_FinisherWindow, 0.9f), HitLine(10.0f, Light),
		Cue(Tags::GameplayCue_Combat_HitPhysics, static_cast<float>(EVBHitImpact::Light)), Cue(Tags::GameplayCue_Combat_HitFlash, 10.0f) });
	{
		UCharacterMovementComponent* Move = Victim->GetCharacterMovement();
		TestTrue(TEXT("plain: horizontal push is applied"), Move->Velocity.Equals(Push * 300.0f, 0.01));
		TestTrue(TEXT("plain: no pending launch"), Move->PendingLaunchVelocity.IsZero());
		Move->HandlePendingLaunch();
		TestTrue(TEXT("plain: stays grounded"), Move->IsMovingOnGround());
		Apply(FHit());
		TestTrue(TEXT("same-frame hits replace speed rather than accumulate"), Move->Velocity.Equals(Push * 300.0f, 0.01));
		Move->SetMovementMode(MOVE_Falling);
		for (const float Vertical : { -200.0f, 626.0f })
		{
			Move->Velocity.Z = Vertical;
			Apply(FHit());
			TestNearlyEqual(TEXT("airborne hit preserves vertical velocity"), static_cast<float>(Move->Velocity.Z), Vertical, 0.01f);
			TestTrue(TEXT("airborne hit does not queue launch"), Move->PendingLaunchVelocity.IsZero());
		}

		Move->SetMovementMode(MOVE_Walking);
		Victim->LaunchCharacter(FVector(300.0, 0.0, 0.0), true, false);
		Move->HandlePendingLaunch();
		TestTrue(TEXT("negative control: even zero-Z LaunchCharacter forces Falling"), Move->IsFalling());
	}

	// 2-3. 회피 무적, 처형 중 무적: 체력도 안 깎이고 아무것도 안 보낸다.
	const FGameplayTag Invulnerable[] = { Tags::State_Combat_Dodging, Tags::State_Combat_Finishing };
	for (const FGameplayTag& Immune : Invulnerable)
	{
		Reset(100.0f, 0.0f, 0.0f, 1.0f);
		AddTag(Immune);
		Apply(FHit());
		TestEqual(FString::Printf(TEXT("%s: health"), *Immune.ToString()), HealthNow(), 100.0f, 0.01f);
		ExpectLog(*Immune.ToString(), {});
	}

	// 4. 창이 켜진 적에 일반 타격: 체력이 깎이고 보통 타격처럼 반응한다.
	//   5. 같은 상태에 처형 피해: 체력이 깎이고 반응은 없다(처형 장면이 움직임을 정한다), 섬광은 난다.
	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	AddTag(Tags::State_Combat_FinisherWindowArmed);
	Apply(FHit());
	TestEqual(TEXT("window + normal: health"), HealthNow(), 90.0f, 0.01f);
	ExpectLog(TEXT("window + normal"), { HitLine(10.0f, Light),
		Cue(Tags::GameplayCue_Combat_HitPhysics, static_cast<float>(EVBHitImpact::Light)), Cue(Tags::GameplayCue_Combat_HitFlash, 10.0f) });
	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	AddTag(Tags::State_Combat_FinisherWindowArmed);
	{
		FHit Hit;
		Hit.bExecution = true;
		Apply(Hit);
	}

	TestEqual(TEXT("window + execution: health"), HealthNow(), 90.0f, 0.01f);
	ExpectLog(TEXT("window + execution"), { Cue(Tags::GameplayCue_Combat_HitFlash, 10.0f) });

	// 6. 막기(패링 창 지나서 막음): 피해 x BlockDamageMultiplier, accept + 스태미나 비용 + block cue, 움찔, flash 없음.
	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	Victim->bGuarding = true;
	Victim->GuardElapsed = Parry->ParryWindow + 1.0f;
	Apply(FHit());
	TestEqual(TEXT("block: health"), HealthNow(), 100.0f - 10.0f * Parry->BlockDamageMultiplier, 0.01f);
	ExpectLog(TEXT("block"), { TEXT("guard accept"), FString::Printf(TEXT("guard stamina %+.1f"), -Parry->StaminaCostOnBlock),
		Cue(Tags::GameplayCue_Combat_GuardBlock, 0.0f), Ev(Tags::Event_Combat_FinisherWindow, (100.0f - 10.0f * Parry->BlockDamageMultiplier) / 100.0f) });

	// 7. 패링(창 안): 피해 없음, 스태미나 회복, 공격자에 riposte 호출, 경직, parry cue. 그 뒤 없음.
	//   피해자가 riposte 를 안 가져 창 길이는 0 - 몸은 여전히 lock 하지만 약공격을 막는 창은 없다(VBGuardRules::RiposteWindowSeconds).
	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	Victim->bGuarding = true;
	Victim->GuardElapsed = 0.0f;
	Apply(FHit());
	TestEqual(TEXT("parry: health"), HealthNow(), 100.0f, 0.01f);
	ExpectLog(TEXT("parry"), { TEXT("guard accept"), FString::Printf(TEXT("guard stamina %+.1f"), Parry->StaminaGainOnParry),
		FString::Printf(TEXT("guard riposte %.2f %s"), 0.0f, *Attacker->GetName()),
		TEXT("attacker ") + Ev(Tags::Event_Combat_Hit, Parry->StaggerHitReactMagnitude), Cue(Tags::GameplayCue_Combat_Parry, 0.0f) });

	// 7b. 피해자가 출하 riposte 를 가지면(게임에선 손의 무기가 BP_GA_Riposte 를 준다) 같은 패링이 설정된 창을 연다.
	//   Blueprint 가 아직 Ability.Attack.Riposte 를 가진다는 증명도 된다.
	TestTrue(TEXT("parry config opens a window at all"), Parry->RiposteWindow > 0.0f);
	if (UClass* Riposte = StaticLoadClass(UGameplayAbility::StaticClass(), nullptr,
		TEXT("/Game/VB/GAS/Abilities/BP_GA_Riposte.BP_GA_Riposte_C"));
		TestNotNull(TEXT("shipped BP_GA_Riposte"), Riposte))
	{
		const FGameplayAbilitySpecHandle RiposteHandle = VictimASC->GiveAbility(FGameplayAbilitySpec(Riposte));
		Reset(100.0f, 0.0f, 0.0f, 1.0f);
		Victim->bGuarding = true;
		Victim->GuardElapsed = 0.0f;
		Apply(FHit());
		ExpectLog(TEXT("parry with riposte"), { TEXT("guard accept"), FString::Printf(TEXT("guard stamina %+.1f"), Parry->StaminaGainOnParry),
			FString::Printf(TEXT("guard riposte %.2f %s"), Parry->RiposteWindow, *Attacker->GetName()),
			TEXT("attacker ") + Ev(Tags::Event_Combat_Hit, Parry->StaggerHitReactMagnitude), Cue(Tags::GameplayCue_Combat_Parry, 0.0f) });
		VictimASC->ClearAbility(RiposteHandle);
	}

	// 8. 막는 중 막을 수 없는 공격: 막기를 무시하고 그대로 맞는다.
	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	Victim->bGuarding = true;
	{
		FHit Hit;
		Hit.bUnblockable = true;
		Apply(Hit);
	}

	TestEqual(TEXT("unblockable: health"), HealthNow(), 90.0f, 0.01f);
	ExpectLog(TEXT("unblockable"), { Ev(Tags::Event_Combat_FinisherWindow, 0.9f), HitLine(10.0f, Light),
		Cue(Tags::GameplayCue_Combat_HitPhysics, static_cast<float>(EVBHitImpact::Light)), Cue(Tags::GameplayCue_Combat_HitFlash, 10.0f) });

	// 9. 받는 배율 x2(DamageTakenScale): 뒤에 읽는 쪽은 모두 배율 적용 값을 본다.
	Reset(100.0f, 0.0f, 0.0f, 2.0f);
	Apply(FHit());
	TestEqual(TEXT("scale x2: health"), HealthNow(), 80.0f, 0.01f);
	ExpectLog(TEXT("scale x2"), { Ev(Tags::Event_Combat_FinisherWindow, 0.8f), HitLine(20.0f, Light),
		Cue(Tags::GameplayCue_Combat_HitPhysics, static_cast<float>(EVBHitImpact::Light)), Cue(Tags::GameplayCue_Combat_HitFlash, 20.0f) });

	// 10. 실드가 먼저 흡수. 11. 실드가 다 받음: 체력이 안 줄어 창 요청은 없고 반응은 돈다.
	Reset(100.0f, 4.0f, 0.0f, 1.0f);
	Apply(FHit());
	TestEqual(TEXT("shield 4: health"), HealthNow(), 94.0f, 0.01f);
	TestEqual(TEXT("shield 4: shield"), VictimASC->GetNumericAttribute(UVBHealthAttributeSet::GetShieldAttribute()), 0.0f, 0.01f);
	ExpectLog(TEXT("shield 4"), { Ev(Tags::Event_Combat_FinisherWindow, 0.94f), HitLine(10.0f, Light),
		Cue(Tags::GameplayCue_Combat_HitPhysics, static_cast<float>(EVBHitImpact::Light)), Cue(Tags::GameplayCue_Combat_HitFlash, 10.0f) });
	Reset(100.0f, 30.0f, 0.0f, 1.0f);
	Apply(FHit());
	TestEqual(TEXT("shield 30: health"), HealthNow(), 100.0f, 0.01f);
	ExpectLog(TEXT("shield 30"), { HitLine(10.0f, Light),
		Cue(Tags::GameplayCue_Combat_HitPhysics, static_cast<float>(EVBHitImpact::Light)), Cue(Tags::GameplayCue_Combat_HitFlash, 10.0f) });

	// 12. 몸에 knockdown ability 가 없는 Knockdown: 먼저 묻고 Heavy 로 낮춘다.
	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	{
		FHit Hit;
		Hit.Impact = EVBHitImpact::Knockdown;
		Apply(Hit);
	}

	ExpectLog(TEXT("knockdown, no taker"), { Ev(Tags::Event_Combat_FinisherWindow, 0.9f), Ev(Tags::Event_Combat_KnockedDown, 10.0f) + TEXT(" ") + Knockdown + PushText,
		HitLine(10.0f, Heavy), Cue(Tags::GameplayCue_Combat_HitPhysics, static_cast<float>(EVBHitImpact::Heavy)), Cue(Tags::GameplayCue_Combat_HitFlash, 10.0f) });

	// 13. 경직 게이지 파탄: 비우고 다시 채우며, 파탄 event 는 체력 허브 일 뒤에 온다.
	Reset(100.0f, 0.0f, 10.0f, 1.0f);
	{
		FHit Hit;
		Hit.Poise = 10.0f;
		Apply(Hit);
	}

	TestEqual(TEXT("gauge break: refilled"), PoiseNow(), 10.0f, 0.01f);
	ExpectLog(TEXT("gauge break"), { Ev(Tags::Event_Combat_FinisherWindow, 0.9f), HitLine(10.0f, Light),
		Cue(Tags::GameplayCue_Combat_HitPhysics, static_cast<float>(EVBHitImpact::Light)), Cue(Tags::GameplayCue_Combat_HitFlash, 10.0f),
		Ev(Tags::Event_Combat_PoiseBroken, 0.0f) });

	// 14. 실드가 다 받는 막기: guard 호출, 체력이 안 줄어 창 요청 없음, 그 뒤 없음.
	Reset(100.0f, 30.0f, 0.0f, 1.0f);
	Victim->bGuarding = true;
	Victim->GuardElapsed = Parry->ParryWindow + 1.0f;
	Apply(FHit());
	TestEqual(TEXT("block + shield: health"), HealthNow(), 100.0f, 0.01f);
	TestEqual(TEXT("block + shield: shield"), VictimASC->GetNumericAttribute(UVBHealthAttributeSet::GetShieldAttribute()),
		30.0f - 10.0f * Parry->BlockDamageMultiplier, 0.01f);
	ExpectLog(TEXT("block + shield"), { TEXT("guard accept"), FString::Printf(TEXT("guard stamina %+.1f"), -Parry->StaminaCostOnBlock),
		Cue(Tags::GameplayCue_Combat_GuardBlock, 0.0f) });

	// 14a. guard 주인이 정한다: 자기 규칙(막았고 1 남김, 막기 상태 없음)을 가진 몸은 그대로 따른다 - 피해 1, 그 1 의 창 요청, 반응, flash 없음.
	//   health set 에 guard 규칙이 남아 있으면 이 답을 무시하게 된다.
	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	Victim->bOwnGuardRule = true;
	Apply(FHit());
	Victim->bOwnGuardRule = false;
	TestEqual(TEXT("own guard rule: health"), HealthNow(), 99.0f, 0.01f);
	ExpectLog(TEXT("own guard rule"), { TEXT("guard own rule"), Ev(Tags::Event_Combat_FinisherWindow, 0.99f) });

	//   패링 창 안에서 막는 중에도 같은 자기 규칙: health set 은 자기 guard 규칙을 돌리면 안 된다(공용 규칙의 accept, 스태미나, cue 없음), 호출은 한 번.
	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	Victim->bGuarding = true;
	Victim->GuardElapsed = 0.0f;
	Victim->bOwnGuardRule = true;
	Apply(FHit());
	Victim->bOwnGuardRule = false;
	TestEqual(TEXT("own guard rule while guarding: health"), HealthNow(), 99.0f, 0.01f);
	ExpectLog(TEXT("own guard rule while guarding"), { TEXT("guard own rule"), Ev(Tags::Event_Combat_FinisherWindow, 0.99f) });

	// 14c. 패링 payload: 패링당한 공격자는 instigator 가 아니라 EffectCauser - 경직은 그 ASC 로 가고(Instigator = 패링한 몸, Target = 공격자),
	//   guard cue 는 hit result 의 접점과 법선을 싣는다.
	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	Victim->bGuarding = true;
	Victim->GuardElapsed = 0.05f; // 창 길이 조정과 무관하게 창 안 - 창에서 계산하지 않은 고정된 작은 값
	{
		// Instigator = 공격자(그 ASC 가 damage execution 의 BaseDamage 를 가진다), EffectCauser = 자기 ASC 를 가진 둘째 몸.
		ACharacter* Causer = World->SpawnActor<ACharacter>();
		UAbilitySystemComponent* CauserASC = MakeASC(Causer);
		FGameplayEventData CauserEvent;
		const FDelegateHandle CauserHandle = CauserASC->GenericGameplayEventCallbacks.FindOrAdd(Tags::Event_Combat_Hit)
			.AddLambda([&CauserEvent](const FGameplayEventData* Payload) { CauserEvent = *Payload; });
		ON_SCOPE_EXIT { CauserASC->GenericGameplayEventCallbacks.FindOrAdd(Tags::Event_Combat_Hit).Remove(CauserHandle); };
		AActor* SomeoneElse = Attacker;
		FGameplayEffectContextHandle Context = AttackerASC->MakeEffectContext();
		Context.AddInstigator(Attacker, Causer);
		FHitResult Contact;
		Contact.ImpactPoint = FVector(1.0, 2.0, 3.0);
		Contact.ImpactNormal = FVector(0.0, 0.0, 1.0);
		Context.AddHitResult(Contact);
		FGameplayEffectSpec Spec(DamageEffect, Context, 1.0f);
		Spec.SetSetByCallerMagnitude(Tags::Effect_Damage_Physical, 10.0f);
		LastAttackerEvent = FGameplayEventData();
		VictimASC->ApplyGameplayEffectSpecToSelf(Spec);
		TestEqual(TEXT("parry payload: health"), HealthNow(), 100.0f, 0.01f);
		TestTrue(TEXT("parry payload: the instigator's ASC gets no stagger"), LastAttackerEvent.Target == nullptr);
		TestTrue(TEXT("parry payload: stagger instigator is the parrying body"), CauserEvent.Instigator == Victim);
		TestTrue(TEXT("parry payload: stagger target is the effect causer"), CauserEvent.Target == Causer);
		TestEqual(TEXT("parry payload: stagger magnitude"), CauserEvent.EventMagnitude, Parry->StaggerHitReactMagnitude, 0.001f);
		TestTrue(TEXT("parry payload: cue at the contact point"), Victim->LastCue.Location.Equals(FVector(1.0, 2.0, 3.0)));
		TestTrue(TEXT("parry payload: cue normal"), Victim->LastCue.Normal.Equals(FVector(0.0, 0.0, 1.0)));
		TestTrue(TEXT("parry payload: cue instigator from the context"), Victim->LastCue.Instigator.Get() == SomeoneElse);
	}

	// 14b. 받는 배율 0 의 막기: 배율은 damage execution 이 guard 보다 먼저 적용해서, 0 이면 guard 가 판단할 피해가 없어 guard 호출도 없다.
	Reset(100.0f, 0.0f, 0.0f, 0.0f);
	Victim->bGuarding = true;
	Victim->GuardElapsed = Parry->ParryWindow + 1.0f;
	Apply(FHit());
	TestEqual(TEXT("block at scale 0: health"), HealthNow(), 100.0f, 0.01f);
	ExpectLog(TEXT("block at scale 0"), {});

	// 15-16. 마무리 타격: set 이 효과의 사실로 OnOutOfHealth 를 한 번 알리고(UVBHealthComponent 가 듣는다), 이 일반 몸에선 반응도 돈다.
	//   처형 피해면 죽음을 medical 로 표시한다.
	for (const bool bExecution : { false, true })
	{
		Reset(5.0f, 0.0f, 0.0f, 1.0f);
		TArray<FVBDeathFacts> Announced;
		const FDelegateHandle OutHandle = Health->OnOutOfHealth.AddLambda([&](const FVBDeathFacts& Facts) { Announced.Add(Facts); });
		FHit Hit;
		Hit.bExecution = bExecution;
		Apply(Hit);
		Health->OnOutOfHealth.Remove(OutHandle);
		const TCHAR* Case = bExecution ? TEXT("lethal execution") : TEXT("lethal");
		TestEqual(FString::Printf(TEXT("%s: health"), Case), HealthNow(), 0.0f, 0.01f);
		if (TestEqual(FString::Printf(TEXT("%s: announced once"), Case), Announced.Num(), 1))
		{
			TestTrue(FString::Printf(TEXT("%s: dead actor is the victim"), Case), Announced[0].DeadActor == Victim);
			TestTrue(FString::Printf(TEXT("%s: death instigator is the attacker"), Case), Announced[0].Instigator == Attacker);
			TestEqual(FString::Printf(TEXT("%s: medical flag"), Case), Announced[0].bMedical, bExecution);
		}

		ExpectLog(Case, { Ev(Tags::Event_Combat_FinisherWindow, 0.0f), HitLine(10.0f, Light),
			Cue(Tags::GameplayCue_Combat_HitPhysics, static_cast<float>(EVBHitImpact::Light)), Cue(Tags::GameplayCue_Combat_HitFlash, 10.0f) });
	}

	// 17-18. 체력이 받지 않는 타격은 기절 게이지도 받지 않는다 - 깎이지 않고(10 중 4 를 맞아도 10) 파탄 사건도 없다(10 중 10).
	const FGameplayTag Ignored[] = { Tags::State_Combat_Dodging, Tags::State_Combat_Finishing };
	for (const FGameplayTag& Immune : Ignored)
	{
		for (const float PoiseHit : { 4.0f, 10.0f })
		{
			Reset(100.0f, 0.0f, 10.0f, 1.0f);
			AddTag(Immune);
			FHit Hit;
			Hit.Poise = PoiseHit;
			Apply(Hit);
			const FString Case = FString::Printf(TEXT("ignored hit %s, gauge hit %.0f"), *Immune.ToString(), PoiseHit);
			TestEqual(Case + TEXT(": health refused"), HealthNow(), 100.0f, 0.01f);
			TestEqual(Case + TEXT(": gauge untouched"), PoiseNow(), 10.0f, 0.01f);
			ExpectLog(*Case, {});
		}
	}

	// 19-20. 창이 켜지면 게이지는 다 찬 채이고 체력은 모든 타격을 받는다. 일반 타격은 반응하고 처형 타격은 반응하지 않는다.
	for (const bool bExecution : { false, true })
	{
		for (const float PoiseHit : { 4.0f, 10.0f })
		{
			Reset(100.0f, 0.0f, 10.0f, 1.0f);
			AddTag(Tags::State_Combat_FinisherWindowArmed);
			FHit Hit;
			Hit.bExecution = bExecution;
			Hit.Poise = PoiseHit;
			Apply(Hit);
			const FString Case = FString::Printf(TEXT("armed window, %s, gauge hit %.0f"),
				bExecution ? TEXT("execution") : TEXT("normal"), PoiseHit);
			TestEqual(Case + TEXT(": health taken"), HealthNow(), 90.0f, 0.01f);
			TestEqual(Case + TEXT(": gauge stays full"), PoiseNow(), 10.0f, 0.01f);
			TestFalse(Case + TEXT(": no break event"), Log.Contains(Ev(Tags::Event_Combat_PoiseBroken, 0.0f)));
			TestEqual(Case + TEXT(": reaction only for the normal hit"), Log.Contains(HitLine(10.0f, Light)), !bExecution);
		}
	}

	// 21. 창을 켜는 일반 타격(창 능력 대역이 창 요청에 켜짐 태그를 붙인다): 체력은 받고, 같은 타격의 게이지 피해는 켜진 창을 만나 거절된다.
	//   이 타격은 반응을 내지 않는다 - 창이 열리며 준비 자세가 된다.
	Reset(100.0f, 0.0f, 10.0f, 1.0f);
	const FDelegateHandle ArmHandle = VictimASC->GenericGameplayEventCallbacks.FindOrAdd(Tags::Event_Combat_FinisherWindow)
		.AddLambda([&](const FGameplayEventData*) { AddTag(Tags::State_Combat_FinisherWindowArmed); });
	{
		FHit Hit;
		Hit.Poise = 4.0f;
		Apply(Hit);
	}

	VictimASC->GenericGameplayEventCallbacks.FindOrAdd(Tags::Event_Combat_FinisherWindow).Remove(ArmHandle);
	TestEqual(TEXT("hit arms the window: health"), HealthNow(), 90.0f, 0.01f);
	TestEqual(TEXT("hit arms the window: gauge refused"), PoiseNow(), 10.0f, 0.01f);
	ExpectLog(TEXT("hit arms the window"), { Ev(Tags::Event_Combat_FinisherWindow, 0.9f), Cue(Tags::GameplayCue_Combat_HitFlash, 10.0f) });

	// 21b. 기절 게이지로 여는 적(State.Combat.PostureExecutable)에게는 체력 문턱 창 요청을 보내지 않는다 - 같은 일반 타격의 대조군.
	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	AddTag(Tags::State_Combat_PostureExecutable);
	Apply(FHit());
	TestFalse(TEXT("posture enemy: no health window request"), Log.Contains(Ev(Tags::Event_Combat_FinisherWindow, 0.9f)));

	// 22. 죽음 대역: OnOutOfHealth listener 가 UVBHealthComponent 처럼 SetHealth 안에서 죽인 자를 적고 State.Dead 를 붙인다.
	//   그러면 치명 knockdown 은 죽음이 먼저, 창 요청, knockdown event 없음, hit event 는 감(Dead 는 반응 ability 만 막는다), physics cue, 발사 없음, flash.
	//   SetHealth 전에 반응하거나 반응 뒤에 알리면 이 줄이 실패한다.
	Reset(5.0f, 0.0f, 0.0f, 1.0f);
	const FDelegateHandle DeathHandle = Health->OnOutOfHealth.AddLambda([&](const FVBDeathFacts& Facts)
		{
			Log.Add(FString::Printf(TEXT("death record %s"), *GetNameSafe(Facts.Instigator)));
			AddTag(Tags::State_Dead);
		});
	{
		FHit Hit;
		Hit.Impact = EVBHitImpact::Knockdown;
		Apply(Hit);
	}

	Health->OnOutOfHealth.Remove(DeathHandle);
	TestEqual(TEXT("death stand-in: health"), HealthNow(), 0.0f, 0.01f);
	ExpectLog(TEXT("death stand-in"), { FString::Printf(TEXT("death record %s"), *Attacker->GetName()), HitLine(10.0f, Knockdown),
		Cue(Tags::GameplayCue_Combat_HitFlash, 10.0f) });
	TestTrue(TEXT("death stand-in: no movement"), Victim->GetCharacterMovement()->PendingLaunchVelocity.IsZero()
		&& Victim->GetCharacterMovement()->Velocity.IsZero());

	// 23. 피는 실제로 맞을 때만: 휘두름은 gore cue 만 고르고, 피해자 응답이 guard 판단 뒤 flash 바로 전에 돌린다. 막기, 패링, 회피는 안 보인다.
	//   반대 대조: Respond 에서 guard 검사 전에 cue 를 돌리거나(막기, 패링 줄), 공격자 ASC 에서 돌리면(실제 타격 줄) 실패한다.
	const FGameplayTag Gore = Tags::GameplayCue_Damage_Physical;
	const FString GoreLine = Cue(Gore, 0.0f);
	auto HitWithGore = [&] { FHit Hit; Hit.ImpactCue = Gore; return Hit; };
	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	Apply(HitWithGore());
	{
		const int32 GoreAt = Log.IndexOfByKey(GoreLine);
		const int32 FlashAt = Log.IndexOfByKey(Cue(Tags::GameplayCue_Combat_HitFlash, 10.0f));
		TestTrue(FString::Printf(TEXT("real hit: the victim runs the gore cue once, before the flash (%s)"),
			*FString::Join(Log, TEXT(" | "))),
			GoreAt != INDEX_NONE && FlashAt != INDEX_NONE && GoreAt < FlashAt && Log.FilterByPredicate([&](const FString& Line) { return Line == GoreLine; }).Num() == 1);
	}

	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	Victim->bGuarding = true;
	Victim->GuardElapsed = Parry->ParryWindow + 1.0f;
	Apply(HitWithGore());
	TestFalse(TEXT("block: no gore"), Log.Contains(GoreLine));
	TestTrue(TEXT("block: its own cue stays"), Log.Contains(Cue(Tags::GameplayCue_Combat_GuardBlock, 0.0f)));
	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	Victim->bGuarding = true;
	Victim->GuardElapsed = 0.0f;
	Apply(HitWithGore());
	TestFalse(TEXT("parry: no gore"), Log.Contains(GoreLine));
	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	AddTag(Tags::State_Combat_Dodging);
	Apply(HitWithGore());
	ExpectLog(TEXT("dodge with a gore cue"), {});

	Reset(100.0f, 0.0f, 0.0f, 1.0f);
	return true;
}

// AI damage 보고는 health set 이 넘기는 응답의 일부다 - AVBAIController 가 빙의한 몸에 출하 피해 효과를 걸 뿐 ReportDamageEvent 는 안 부른다.
//   stimulus 는 Instigator 가 아니라 EffectCauser(공격한 몸)를 가리킨다. 실드가 다 받은 타격, 막기는 보고하고, 패링(남은 피해 없음)은 안 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDamageSenseFromAppliedHitTest, "VB.Unit.Damage.SenseFromAppliedHit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDamageSenseFromAppliedHitTest::RunTest(const FString& Parameters)
{
	namespace Tags = VBGameplayTags;

	const UVBAttackConfig* Attack = LoadObject<UVBAttackConfig>(nullptr,
		TEXT("/Game/VB/Data/DA_Attack_Fighter_Light.DA_Attack_Fighter_Light"));
	if (!TestNotNull(TEXT("shipped attack config"), Attack) || !TestNotNull(TEXT("its damage effect"), Attack->DamageEffectClass.Get()))
	{
		return false;
	}

	const UGameplayEffect* DamageEffect = Attack->DamageEffectClass->GetDefaultObject<UGameplayEffect>();
	const UVBParryConfig* Parry = GetDefault<UVBParryConfig>();

	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		OwnedPackage->SetDirtyFlag(false);
	};

	AVBAIController* Controller = World->SpawnActor<AVBAIController>();
	AVBDamagePipelineTestVictim* Victim = World->SpawnActor<AVBDamagePipelineTestVictim>();
	ACharacter* Instigator = World->SpawnActor<ACharacter>();
	UAIPerceptionSystem* Perception = UAIPerceptionSystem::GetCurrent(World);
	if (!TestNotNull(TEXT("controller"), Controller) || !TestNotNull(TEXT("victim"), Victim) || !TestNotNull(TEXT("instigator"), Instigator)
		|| !TestNotNull(TEXT("perception system"), Perception))
	{
		return false;
	}

	UAIPerceptionComponent* Listener = Controller->GetPerceptionComponent();
	if (!TestNotNull(TEXT("perception component"), Listener))
	{
		return false;
	}

	Controller->Possess(Victim);

	auto MakeASC = [](AActor* Owner)
	{
		UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Owner);
		Owner->AddInstanceComponent(ASC);
		ASC->RegisterComponent();
		ASC->InitAbilityActorInfo(Owner, Owner);
		return ASC;
	};
	UAbilitySystemComponent* VictimASC = MakeASC(Victim);
	UAbilitySystemComponent* InstigatorASC = MakeASC(Instigator);
	InstigatorASC->AddAttributeSetSubobject(NewObject<UVBCombatAttributeSet>(Instigator));
	VictimASC->AddAttributeSetSubobject(NewObject<UVBHealthAttributeSet>(Victim));
	VictimASC->AddAttributeSetSubobject(NewObject<UVBCombatAttributeSet>(Victim));

	const FAISenseID DamageId = UAISense::GetSenseID<UAISense_Damage>();
	auto Heard = [&](AActor* Source)
	{
		FActorPerceptionBlueprintInfo Info;
		return Listener->GetActorsPerception(Source,
			Info) && Info.LastSensedStimuli.ContainsByPredicate([DamageId](const FAIStimulus& Stimulus)
		{
			return Stimulus.Type == DamageId && Stimulus.WasSuccessfullySensed();
		});
	};

	// 피해가 안 남는 막기는 보고되지 않아 BlockDamageMultiplier 를 0 으로 하면 막기 줄이 조용히 빠진다 - 그래서 말한다.
	if (Parry->BlockDamageMultiplier <= 0.0f)
	{
		AddWarning(TEXT("BlockDamageMultiplier <= 0: the block case no longer exercises the block-reports path"));
	}

	struct FCase { const TCHAR* Name; float Shield; bool bGuarding; float GuardElapsed; bool bHeard; };
	const FCase Cases[] = {
		{ TEXT("plain hit"), 0.0f, false, 0.0f, true },
		{ TEXT("shield takes it whole"), 30.0f, false, 0.0f, true },
		{ TEXT("block"), 0.0f, true, Parry->ParryWindow + 1.0f, Parry->BlockDamageMultiplier > 0.0f },
		{ TEXT("parry"), 0.0f, true, 0.05f, false },
	};
	for (const FCase& Case : Cases)
	{
		VictimASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), 100.0f);
		VictimASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);
		VictimASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxShieldAttribute(), 50.0f);
		VictimASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetShieldAttribute(), Case.Shield);
		VictimASC->SetNumericAttributeBase(UVBCombatAttributeSet::GetDamageTakenScaleAttribute(), 1.0f);
		Victim->bGuarding = Case.bGuarding;
		Victim->GuardElapsed = Case.GuardElapsed;

		// 사례마다 새 공격 몸 - 한 사례의 stimulus 가 다른 사례를 대신 답하지 않게.
		AActor* Causer = World->SpawnActor<AActor>();
		FGameplayEffectContextHandle Context = InstigatorASC->MakeEffectContext();
		Context.AddInstigator(Instigator, Causer);
		FGameplayEffectSpec Spec(DamageEffect, Context, 1.0f);
		Spec.SetSetByCallerMagnitude(Tags::Effect_Damage_Physical, 10.0f);
		VictimASC->ApplyGameplayEffectSpecToSelf(Spec);
		Perception->Tick(0.1f);

		TestEqual(FString::Printf(TEXT("%s: damage sense from the attacking body"), Case.Name), Heard(Causer), Case.bHeard);
	}

	TestFalse(TEXT("the instigator is never the reported attacker"), Heard(Instigator));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
