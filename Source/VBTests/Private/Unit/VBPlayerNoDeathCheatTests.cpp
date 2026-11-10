#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameModes/VBGameMode.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

// 실제 피해 처리에서 치트와 GameMode의 플레이어 비사망 규칙을 검사한다.
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayerNoDeathCheatTest, "VB.Unit.Player.NoDeathCheat",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBPlayerNoDeathCheatTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* Cheat = IConsoleManager::Get().FindConsoleVariable(TEXT("vb.Combat.PlayerNoDeath"));
	if (!TestNotNull(TEXT("cvar registered"), Cheat))
	{
		return false;
	}

	const int32 Previous = Cheat->GetInt();

	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		Cheat->Set(Previous, ECVF_SetByCode);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		OwnedPackage->SetDirtyFlag(false);
	};

	// 일부러 손으로 만든 IncomingDamage modifier - UVBDamageExecution 을 건너뛰어 배율 없이 health set 만 본다.
	//   배율은 VB.Unit.Damage.EffectAssets 와 VB.Unit.Difficulty.DamageHub 가 본다.
	const TStrongObjectPtr<UGameplayEffect> Damage(NewObject<UGameplayEffect>(GetTransientPackage()));
	Damage->DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UVBHealthAttributeSet::GetIncomingDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(500.0f));
	Damage->Modifiers.Add(Modifier);

	auto MakeTarget = [&](bool bPlayer) -> UAbilitySystemComponent*
	{
		ACharacter* Pawn = World->SpawnActor<ACharacter>();
		if (!Pawn)
		{
			return nullptr;
		}

		if (bPlayer)
		{
			// APawn::IsPlayerControlled 는 game mode 만 만드는 PlayerState 를 읽는데 이 월드엔 없다.
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			if (!Controller)
			{
				return nullptr;
			}

			Controller->PlayerState = World->SpawnActor<APlayerState>();
			if (!Controller->PlayerState)
			{
				return nullptr;
			}

			Controller->Possess(Pawn);
		}

		UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Pawn);
		Pawn->AddInstanceComponent(ASC);
		ASC->RegisterComponent();
		ASC->InitAbilityActorInfo(Pawn, Pawn);
		UVBHealthAttributeSet* Health = NewObject<UVBHealthAttributeSet>(Pawn);
		Health->InitMaxHealth(100.0f);
		Health->InitHealth(100.0f);
		ASC->AddAttributeSetSubobject(Health);
		return ASC;
	};
	auto LethalHitFromFull = [&](UAbilitySystemComponent* ASC) -> float
	{
		ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);
		ASC->ApplyGameplayEffectSpecToSelf(FGameplayEffectSpec(Damage.Get(), ASC->MakeEffectContext(), 1.0f));
		return ASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute());
	};

	UAbilitySystemComponent* Player = MakeTarget(true);
	UAbilitySystemComponent* Enemy = MakeTarget(false);
	if (!TestNotNull(TEXT("player"), Player) || !TestNotNull(TEXT("enemy"), Enemy))
	{
		return false;
	}

	TestTrue(TEXT("player pawn is player-controlled"), Cast<APawn>(Player->GetAvatarActor())->IsPlayerControlled());

	Cheat->Set(0, ECVF_SetByCode);
	TestEqual(TEXT("cheat off: lethal hit kills the player"), LethalHitFromFull(Player), 0.0f);

	Cheat->Set(1, ECVF_SetByCode);
	TestEqual(TEXT("cheat on: player floors at 1"), LethalHitFromFull(Player), 1.0f);
	TestEqual(TEXT("cheat on: enemy still dies"), LethalHitFromFull(Enemy), 0.0f);

	Cheat->Set(0, ECVF_SetByCode);
	const TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
	World->SetGameInstance(Instance.Get());
	World->GetWorldSettings()->DefaultGameMode = AVBGameMode::StaticClass();
	if (!TestTrue(TEXT("authoritative game mode created"), World->SetGameMode(FURL())))
	{
		return false;
	}

	AVBGameMode* Mode = World->GetAuthGameMode<AVBGameMode>();
	if (!TestNotNull(TEXT("game mode"), Mode))
	{
		return false;
	}

	FBoolProperty* Policy = FindFProperty<FBoolProperty>(AVBGameMode::StaticClass(), TEXT("bNonLethalPlayerCombat"));
	if (!TestNotNull(TEXT("authored combat policy"), Policy))
	{
		return false;
	}

	TestFalse(TEXT("normal mode defaults to lethal combat"), Mode->IsPlayerCombatNonLethal());
	TestEqual(TEXT("mode off: lethal damage kills player"), LethalHitFromFull(Player), 0.0f);
	Policy->SetPropertyValue_InContainer(Mode, true);
	TestEqual(TEXT("mode on without cheat: player floors at 1"), LethalHitFromFull(Player), 1.0f);
	Player->ApplyGameplayEffectSpecToSelf(FGameplayEffectSpec(Damage.Get(), Player->MakeEffectContext(), 1.0f));
	TestEqual(TEXT("repeated lethal damage keeps player alive"),
		Player->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()), 1.0f);
	TestEqual(TEXT("mode on: enemy still dies"), LethalHitFromFull(Enemy), 0.0f);
	Player->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);
	Damage->Modifiers[0].ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(30.0f));
	Player->ApplyGameplayEffectSpecToSelf(FGameplayEffectSpec(Damage.Get(), Player->MakeEffectContext(), 1.0f));
	TestEqual(TEXT("mode on: ordinary damage still lowers health"),
		Player->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()), 70.0f);
	return true;
}
#endif
