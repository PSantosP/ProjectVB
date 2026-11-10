// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "Data/VBWeaponStateConfig.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 맞는 것은 전투 자극이다: health set 이 버스(Message.Combat.HitReceived)에 알리고 피격자의 무기 상태가 전투에 들어간다.
//   다른 몸이 맞으면 변화 없고, 이미 전투 중이면 종료 타이머를 다시 건다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitStimulusTest, "VB.Unit.Weapon.HitStimulus",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBHitStimulusTest::RunTest(const FString& Parameters)
{
	// 버스는 GameInstance subsystem 이라 격리 world 가 실행 중인 GameInstance 를 빌린다(-game automation).
	UGameInstance* Instance = nullptr;
	if (!TestNotNull(TEXT("engine"), GEngine))
	{
		return false;
	}

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World()
			&& Context.World()->GetGameInstance())
		{
			Instance = Context.World()->GetGameInstance();
			break;
		}
	}

	if (!TestNotNull(TEXT("initialized game instance required (-game automation)"), Instance))
	{
		return false;
	}

	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> Package(World->GetPackage());
	World->SetGameInstance(Instance);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		Package->SetDirtyFlag(false);
	};
	World->InitializeActorsForPlay(FURL());

	AVBCharacter* Body = World->SpawnActor<AVBCharacter>();
	AVBCharacter* Other = World->SpawnActor<AVBCharacter>(FVector(0.0, 100000.0, 0.0), FRotator::ZeroRotator); // far outside the enemy-proximity radius
	if (!TestNotNull(TEXT("body"), Body) || !TestNotNull(TEXT("other body"), Other))
	{
		return false;
	}

	ON_SCOPE_EXIT { Body->Destroy(); Other->Destroy(); };
	Body->DispatchBeginPlay();
	Other->DispatchBeginPlay();
	UVBWeaponStateComponent* Weapons = UVBWeaponStateComponent::FindFor(Body);
	if (!TestNotNull(TEXT("weapon state"), Weapons))
	{
		return false;
	}

	if (!TestEqual(TEXT("precondition: starts out of combat"), Weapons->GetLocomotionState(), EVBLocomotionState::Unarmed))
	{
		return false;
	}

	// 피격은 실제 health set 을 거친다: avatar 가 몸인 ASC(플레이어 것은 PlayerState 에 있다).
	auto MakeVictimASC = [World](AActor* Avatar)
	{
		AActor* Holder = World->SpawnActor<AActor>();
		UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Holder);
		Holder->AddInstanceComponent(ASC);
		ASC->RegisterComponent();
		ASC->InitAbilityActorInfo(Holder, Avatar);
		UVBHealthAttributeSet* Health = NewObject<UVBHealthAttributeSet>(Holder);
		Health->InitMaxHealth(1000.0f);
		Health->InitHealth(1000.0f);
		ASC->AddAttributeSetSubobject(Health);
		return ASC;
	};
	UAbilitySystemComponent* BodyASC = MakeVictimASC(Body);
	UAbilitySystemComponent* OtherASC = MakeVictimASC(Other);
	const TStrongObjectPtr<UGameplayEffect> Damage(NewObject<UGameplayEffect>(GetTransientPackage()));
	Damage->DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UVBHealthAttributeSet::GetIncomingDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(5.0f));
	Damage->Modifiers.Add(Modifier);
	auto Hit = [&Damage](UAbilitySystemComponent* ASC)
	{
		ASC->ApplyGameplayEffectSpecToSelf(FGameplayEffectSpec(Damage.Get(), ASC->MakeEffectContext(), 1.0f));
	};
	// 틱마다 엔진 프레임 두 번 - 아니면 이번 프레임에 건 타이머가 대기 상태로 남는다.
	auto RunTimers = [World](float Seconds)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(0.0f);
		++GFrameCounter;
		World->GetTimerManager().Tick(Seconds);
	};

	Hit(OtherASC);
	TestEqual(TEXT("another body's hit: still out of combat"), Weapons->GetLocomotionState(), EVBLocomotionState::Unarmed);

	Hit(BodyASC);
	TestEqual(TEXT("hit: enters combat, weapon not drawn"), Weapons->GetLocomotionState(), EVBLocomotionState::CombatSheathed);

	// 종료 타이머가 멈춘 전투 상태: 시간만으로는 그대로고, 피격이 타이머를 다시 걸어 같은 대기 뒤 전투를 벗어난다.
	const float ExitDelay = GetDefault<UVBWeaponStateConfig>()->CombatExitDelay;
	Weapons->StopCombatTimer();
	RunTimers(ExitDelay + 1.0f);
	TestEqual(TEXT("no timer: stays in combat"), Weapons->GetLocomotionState(), EVBLocomotionState::CombatSheathed);
	Hit(BodyASC);
	RunTimers(ExitDelay + 1.0f);
	TestEqual(TEXT("hit in combat re-arms the exit timer"), Weapons->GetLocomotionState(), EVBLocomotionState::Unarmed);
	return true;
}
#endif
