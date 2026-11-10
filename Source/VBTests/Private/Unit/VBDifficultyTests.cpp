#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "CoreLoop/VBDifficultyConfig.h"
#include "CoreLoop/VBDifficultyRules.h"
#include "CoreLoop/VBDifficultySettings.h"
#include "CoreLoop/VBDifficultyStateComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect.h"
#include "Misc/DataValidation.h"
#include "Misc/ScopeExit.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "UI/VBDifficultyPickerWidget.h"
#include "UI/VBUISettings.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 난이도 단계, 배수, 배선의 단위 시험.
namespace
{
	FGameplayTag DifficultyTag(const TCHAR* Name) { return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false); }

	// 태그 비교 - 실패하면 두 이름을 보인다.
	bool SameTag(FAutomationTestBase& Test, const TCHAR* What, FGameplayTag Actual, FGameplayTag Expected)
	{
		if (Actual != Expected)
		{
			Test.AddError(FString::Printf(TEXT("%s: expected %s, got %s"), What, *Expected.ToString(), *Actual.ToString()));
			return false;
		}

		return true;
	}

	UVBDifficultyConfig* MakeConfig(float EasyPlayer, float EasyEnemy, float HardPlayer, float HardEnemy)
	{
		UVBDifficultyConfig* Config = NewObject<UVBDifficultyConfig>(GetTransientPackage());
		auto Add = [Config](const TCHAR* Name, float Player, float Enemy)
		{
			FVBDifficultyLevel& Level = Config->Levels.AddDefaulted_GetRef();
			Level.Tag = DifficultyTag(Name);
			Level.DisplayName = FText::FromString(Name);
			Level.PlayerDamageTakenScale = Player;
			Level.EnemyDamageTakenScale = Enemy;
		};
		Add(TEXT("Difficulty.Easy"), EasyPlayer, EasyEnemy);
		Add(TEXT("Difficulty.Normal"), 1.0f, 1.0f);
		Add(TEXT("Difficulty.Hard"), HardPlayer, HardEnemy);
		Config->DefaultLevel = DifficultyTag(TEXT("Difficulty.Normal"));
		return Config;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDifficultyRulesTest, "VB.Unit.Difficulty.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDifficultyRulesTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Easy = DifficultyTag(TEXT("Difficulty.Easy"));
	const FGameplayTag Hard = DifficultyTag(TEXT("Difficulty.Hard"));
	if (!TestTrue(TEXT("difficulty tags registered"), Easy.IsValid() && Hard.IsValid()))
	{
		return false;
	}

	const TStrongObjectPtr<UVBDifficultyConfig> Config(MakeConfig(0.6f, 1.4f, 1.5f, 0.8f));

	TestEqual(TEXT("hard: player takes x1.5"), VBDifficultyRules::ResolveDamageScale(Config.Get(), Hard, true, false), 1.5f);
	TestEqual(TEXT("hard: enemy takes x0.8"), VBDifficultyRules::ResolveDamageScale(Config.Get(), Hard, false, false), 0.8f);
	TestEqual(TEXT("easy: enemy takes x1.4"), VBDifficultyRules::ResolveDamageScale(Config.Get(), Easy, false, false), 1.4f);
	TestEqual(TEXT("execution damage is never scaled (player)"),
		VBDifficultyRules::ResolveDamageScale(Config.Get(), Hard, true, true), 1.0f);
	TestEqual(TEXT("execution damage is never scaled (enemy)"),
		VBDifficultyRules::ResolveDamageScale(Config.Get(), Hard, false, true), 1.0f);
	TestEqual(TEXT("unknown tag uses the default level"),
		VBDifficultyRules::ResolveDamageScale(Config.Get(), FGameplayTag(), true, false), 1.0f);
	TestEqual(TEXT("no config: x1"), VBDifficultyRules::ResolveDamageScale(nullptr, Hard, true, false), 1.0f);

	// 검증 없이 들어온 나쁜 값은 1 로 떨어진다(NaN 은 음수 검사를 지난다).
	const float Bad[] = {0.0f, -2.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
	for (const float Value : Bad)
	{
		const TStrongObjectPtr<UVBDifficultyConfig> BadConfig(MakeConfig(1.0f, 1.0f, Value, Value));
		TestEqual(FString::Printf(TEXT("invalid scale %f falls back to x1"), Value),
			VBDifficultyRules::ResolveDamageScale(BadConfig.Get(), Hard, true, false), 1.0f);
		TestFalse(FString::Printf(TEXT("IsValidScale(%f) is false"), Value), VBDifficultyRules::IsValidScale(Value));
	}

	TestTrue(TEXT("IsValidScale(0.5)"), VBDifficultyRules::IsValidScale(0.5f));

	// 정규화: 아는 단계는 그대로, 모르는, 빈 태그는 기본, 설정이 없으면 빈 태그(부른 쪽 태그를 디스크로 보내지 않는다).
	SameTag(*this, TEXT("normalize keeps a listed level"), VBDifficultyRules::NormalizeLevel(Config.Get(), Hard), Hard);
	SameTag(*this, TEXT("normalize maps an unlisted tag to the default"),
		VBDifficultyRules::NormalizeLevel(Config.Get(), DifficultyTag(TEXT("Difficulty"))), DifficultyTag(TEXT("Difficulty.Normal")));
	SameTag(*this, TEXT("normalize without config gives an empty tag"), VBDifficultyRules::NormalizeLevel(nullptr, Hard), FGameplayTag());

	// 고르기 화면의 설명은 배수에서 만든다 - 숫자를 바꾸면 설명도 따라온다(따로 저작한 문장이 어긋나지 않게).
	const FString HardText = VBDifficultyRules::DescribeLevel(*Config->FindLevel(Hard)).ToString();
	TestTrue(TEXT("description shows the player-taken scale"), HardText.Contains(TEXT("1.5")));
	TestTrue(TEXT("description shows the enemy-taken scale"), HardText.Contains(TEXT("0.8")));
	TestFalse(TEXT("description does not show the other level's numbers"), HardText.Contains(TEXT("1.4")));

#if WITH_EDITOR
	{
		// 검사가 다 지나면 결과는 Super 의 것(NotValidated)이다 - 프로젝트 설정 자산이 다 그렇다. 그래서 "Invalid 아님 + 에러 0" 을 본다.
		FDataValidationContext Context;
		TestNotEqual(TEXT("well-formed config is not invalid"), Config->IsDataValid(Context), EDataValidationResult::Invalid);
		TestEqual(TEXT("well-formed config: no errors"), static_cast<int32>(Context.GetNumErrors()), 0);
	}

	{
		const TStrongObjectPtr<UVBDifficultyConfig> Broken(MakeConfig(0.6f, 1.4f, std::numeric_limits<float>::quiet_NaN(), 0.8f));
		Broken->Levels[0].Tag = Broken->Levels[1].Tag;
		Broken->DefaultLevel = DifficultyTag(TEXT("Difficulty"));
		FDataValidationContext Context;
		TestEqual(TEXT("duplicate tag + NaN + missing default: invalid"), Broken->IsDataValid(Context), EDataValidationResult::Invalid);
		TestEqual(TEXT("three errors reported"), static_cast<int32>(Context.GetNumErrors()), 3);
	}
#endif
	return true;
}

// 실제 자산: 옛 슬롯 = "보통" 약속은 기본 단계가 Normal 이고 그 배수가 1 일 때만 선다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDifficultyAssetTest, "VB.Unit.Difficulty.ShippedConfig",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDifficultyAssetTest::RunTest(const FString& Parameters)
{
	const UVBDifficultyConfig* Config = UVBDifficultySettings::LoadConfig();
	if (!TestNotNull(TEXT("settings point at a loadable config"), Config))
	{
		return false;
	}

	TestEqual(TEXT("three levels (user 10-17)"), Config->Levels.Num(), 3);
	SameTag(*this, TEXT("default level is Normal"), Config->DefaultLevel, DifficultyTag(TEXT("Difficulty.Normal")));
	const FVBDifficultyLevel* Normal = Config->FindLevel(DifficultyTag(TEXT("Difficulty.Normal")));
	if (TestNotNull(TEXT("Normal exists"), Normal))
	{
		TestEqual(TEXT("Normal: player x1 (legacy slots play as before)"), Normal->PlayerDamageTakenScale, 1.0f);
		TestEqual(TEXT("Normal: enemy x1"), Normal->EnemyDamageTakenScale, 1.0f);
	}
#if WITH_EDITOR
	FDataValidationContext Context;
	TestNotEqual(TEXT("shipped config is not invalid"), Config->IsDataValid(Context), EDataValidationResult::Invalid);
	TestEqual(TEXT("shipped config: no errors"), static_cast<int32>(Context.GetNumErrors()), 0);
#endif
	return true;
}

// 새 게임 고르기 화면의 배선: 태그가 Modal 층의 실제 WBP 로 가고, 그 WBP 가 줄 위젯을 들고 뒤로 가기를 받는다.
//   이 배선이 끊기면 "새 게임" 이 아무것도 안 띄운다(UI 관리자는 경고만) - 그래서 실제 자산으로 잠근다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDifficultyPickerRouteTest, "VB.Unit.Difficulty.PickerRoute",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDifficultyPickerRouteTest::RunTest(const FString& Parameters)
{
	const FVBScreenRoute* Route = GetDefault<UVBUISettings>()->Screens.Find(VBGameplayTags::UI_Screen_DifficultyPicker);
	if (!TestNotNull(TEXT("picker screen has a route (DefaultGame.ini Screens)"), Route))
	{
		return false;
	}

	SameTag(*this, TEXT("route layer is Modal (menu and slot list stay underneath)"), Route->Layer,
		FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.Modal"), false));
	UClass* ScreenClass = Route->ScreenClass.LoadSynchronous();
	if (!TestNotNull(TEXT("route class loads"), ScreenClass))
	{
		return false;
	}

	TestTrue(TEXT("route class is a difficulty picker"), ScreenClass->IsChildOf(UVBDifficultyPickerWidget::StaticClass()));
	const UVBDifficultyPickerWidget* Defaults = GetDefault<UVBDifficultyPickerWidget>(ScreenClass);
	const FClassProperty* RowClass = FindFProperty<FClassProperty>(UVBDifficultyPickerWidget::StaticClass(), TEXT("ChoiceWidgetClass"));
	TestTrue(TEXT("WBP sets ChoiceWidgetClass"), RowClass && RowClass->GetPropertyValue_InContainer(Defaults) != nullptr);
	const FBoolProperty* BackHandler = FindFProperty<FBoolProperty>(UCommonActivatableWidget::StaticClass(), TEXT("bIsBackHandler"));
	TestTrue(TEXT("WBP handles Back"), BackHandler && BackHandler->GetPropertyValue_InContainer(Defaults));
	return true;
}

// 실제 피해 중심(PostGameplayEffectExecute)을 지난다: 플레이어(ASC 주인 = PlayerState)와 적(주인 = 자기)의 배수, 처형 면제, 도중 변경.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDifficultyDamageHubTest, "VB.Unit.Difficulty.DamageHub",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDifficultyDamageHubTest::RunTest(const FString& Parameters)
{
	const UVBDifficultyConfig* Config = UVBDifficultySettings::LoadConfig();
	const FGameplayTag Normal = DifficultyTag(TEXT("Difficulty.Normal"));
	const FGameplayTag Hard = DifficultyTag(TEXT("Difficulty.Hard"));
	const FVBDifficultyLevel* HardLevel = Config ? Config->FindLevel(Hard) : nullptr;
	if (!TestNotNull(TEXT("shipped Hard level"), HardLevel))
	{
		return false;
	}

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

	// 이것 없이는 액터가 초기화되지 않아 RegisterComponent 가 InitializeComponent 를 건너뛴다(structure-batch 함정).
	World->InitializeActorsForPlay(FURL());
	AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
	if (!TestNotNull(TEXT("game state"), GameState))
	{
		return false;
	}

	World->SetGameState(GameState);
	UVBDifficultyStateComponent* Difficulty = NewObject<UVBDifficultyStateComponent>(GameState);
	GameState->AddInstanceComponent(Difficulty);
	Difficulty->RegisterComponent();
	SameTag(*this, TEXT("no active save: starts at the default level"), Difficulty->GetLevel(), Normal);

	// 실제 damage effect 를 쓴다 - 난이도 배수는 health set 이 아니라 UVBDamageExecution 안에 있어 손으로 만든 modifier GE 는 건너뛴다.
	//   공격자는 execution 이 BaseDamage 를 읽는 combat set 을 든다.
	const UClass* DamageClass = LoadClass<UGameplayEffect>(nullptr,
		TEXT("/Game/VB/GAS/Effects/Damage/GE_PhysicalDamage.GE_PhysicalDamage_C"));
	if (!TestNotNull(TEXT("shipped damage effect"), DamageClass))
	{
		return false;
	}

	const UGameplayEffect* Damage = DamageClass->GetDefaultObject<UGameplayEffect>();
	ACharacter* AttackerBody = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("attacker"), AttackerBody))
	{
		return false;
	}

	UAbilitySystemComponent* AttackerASC = NewObject<UAbilitySystemComponent>(AttackerBody);
	AttackerBody->AddInstanceComponent(AttackerASC);
	AttackerASC->RegisterComponent();
	AttackerASC->InitAbilityActorInfo(AttackerBody, AttackerBody);
	AttackerASC->AddAttributeSetSubobject(NewObject<UVBCombatAttributeSet>(AttackerBody));
	auto MakeHit = [&](bool bExecution)
	{
		FGameplayEffectSpec Spec(Damage, AttackerASC->MakeEffectContext(), 1.0f);
		Spec.SetSetByCallerMagnitude(VBGameplayTags::Effect_Damage_Physical, 10.0f);
		if (bExecution)
		{
			Spec.AddDynamicAssetTag(VBGameplayTags::Effect_Execute); // IsFinisherDamage 가 보는 이름표
		}

		return Spec;
	};

	auto MakeTarget = [&](bool bPlayer) -> UAbilitySystemComponent*
	{
		ACharacter* Pawn = World->SpawnActor<ACharacter>();
		AActor* Owner = Pawn;
		if (bPlayer)
		{
			Owner = World->SpawnActor<APlayerState>(); // 플레이어 ASC 의 주인은 PlayerState(VBPlayerState.cpp)
		}

		if (!Pawn || !Owner)
		{
			return nullptr;
		}

		UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Owner);
		Owner->AddInstanceComponent(ASC);
		ASC->RegisterComponent();
		ASC->InitAbilityActorInfo(Owner, Pawn);
		UVBHealthAttributeSet* Health = NewObject<UVBHealthAttributeSet>(Owner);
		Health->InitMaxHealth(100.0f);
		Health->InitHealth(100.0f);
		ASC->AddAttributeSetSubobject(Health);
		return ASC;
	};
	auto HitFromFull = [&](UAbilitySystemComponent* ASC, bool bExecution) -> float
	{
		ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);
		ASC->ApplyGameplayEffectSpecToSelf(MakeHit(bExecution));
		return 100.0f - ASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute());
	};

	UAbilitySystemComponent* Player = MakeTarget(true);
	UAbilitySystemComponent* Enemy = MakeTarget(false);
	if (!TestNotNull(TEXT("player"), Player) || !TestNotNull(TEXT("enemy"), Enemy))
	{
		return false;
	}

	TestEqual(TEXT("normal: player loses 10"), HitFromFull(Player, false), 10.0f, 0.01f);
	TestEqual(TEXT("normal: enemy loses 10"), HitFromFull(Enemy, false), 10.0f, 0.01f);

	// Normal 에서 만든 타격이 Hard 로 바뀐 뒤 들어가면 Hard 를 받는다 - 배수는 spec 을 만들 때가 아니라 damage 가 들어갈 때 읽는다.
	const FGameplayEffectSpec EarlySpec = MakeHit(false);
	TestTrue(TEXT("switch to Hard mid-game"), Difficulty->SetLevel(Hard));
	Enemy->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);
	Enemy->ApplyGameplayEffectSpecToSelf(EarlySpec);
	TestEqual(TEXT("spec built at Normal, applied at Hard: Hard scale"),
		100.0f - Enemy->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()), 10.0f * HardLevel->EnemyDamageTakenScale, 0.01f);
	TestEqual(TEXT("hard: player loss scaled"), HitFromFull(Player, false), 10.0f * HardLevel->PlayerDamageTakenScale, 0.01f);
	TestEqual(TEXT("hard: enemy loss scaled"), HitFromFull(Enemy, false), 10.0f * HardLevel->EnemyDamageTakenScale, 0.01f);
	TestEqual(TEXT("hard: execution damage on an enemy is not scaled"), HitFromFull(Enemy, true), 10.0f, 0.01f);
	TestEqual(TEXT("hard: execution damage on the player is not scaled"), HitFromFull(Player, true), 10.0f, 0.01f);

	TestFalse(TEXT("unknown level is refused"), Difficulty->SetLevel(DifficultyTag(TEXT("Difficulty"))));
	SameTag(*this, TEXT("refused request keeps Hard"), Difficulty->GetLevel(), Hard);

	// 저장 왕복: 쓰기는 지금 단계, 읽기는 정규화(빈 태그, 모르는 태그 -> 기본).
	const TStrongObjectPtr<UVBSaveGame> Save(NewObject<UVBSaveGame>(GetTransientPackage()));
	Difficulty->WriteSave(*Save.Get(), FVBSaveContext());
	SameTag(*this, TEXT("save writes the current level"), Save->Difficulty.Level, Hard);
	Save->Difficulty.Level = FGameplayTag();
	Difficulty->ReadSave(*Save.Get(), FVBSaveContext());
	SameTag(*this, TEXT("legacy slot (empty tag) reads as the default level"), Difficulty->GetLevel(), Normal);
	return true;
}

// 실제 시작 길(2판 F1): 저장 서브시스템의 공개 입구로 새 게임을 시작하면, 그 뒤에 생기는 GameState 의 난이도 주인이
//   ReadSave 없이 고른 단계로 시작하고, 게임 중 변경은 활성 세이브에 바로 적힌다. 슬롯 입구는 디스크를 써서 여기서 돌리지 않는다(PIE 몫).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDifficultyNewGameTest, "VB.Unit.Difficulty.NewGameStart",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDifficultyNewGameTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Easy = DifficultyTag(TEXT("Difficulty.Easy"));
	const FGameplayTag Hard = DifficultyTag(TEXT("Difficulty.Hard"));
	if (!TestTrue(TEXT("tags"), Easy.IsValid() && Hard.IsValid()))
	{
		return false;
	}

	// 이 실행의 진짜 게임 인스턴스와 월드를 쓴다(헤드리스 -game). 시험용 게임 인스턴스를 새로 만들면 안 된다 - UGameInstance::Init 이
	//   전역 네트워크 델리게이트를 다시 묶고, InitializeStandalone 의 월드는 엔진 목록에 남아 다음 틱에 죽은 채 돌아 크래시가 난다.
	//   대가: 이 실행의 활성 세이브가 새 게임 세이브로 바뀐다(디스크에는 안 씀). 다른 시험은 자기 저장 서브시스템을 만든다.
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.OwningGameInstance
			&& Context.World())
		{
			World = Context.World();
			break;
		}
	}

	if (!TestNotNull(TEXT("a game world owned by a game instance (run headless -game or in PIE)"), World))
	{
		return false;
	}

	UVBSaveGameSubsystem* SaveSubsystem = World->GetGameInstance()->GetSubsystem<UVBSaveGameSubsystem>();
	if (!TestNotNull(TEXT("save subsystem"), SaveSubsystem))
	{
		return false;
	}

	TestTrue(TEXT("new game with Hard is accepted"), SaveSubsystem->StartNewGameWithDifficulty(Hard));
	SameTag(*this, TEXT("the new save carries Hard before any world reads it"), SaveSubsystem->GetActiveGameSave()->Difficulty.Level, Hard);

	AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
	if (!TestNotNull(TEXT("game state"), GameState))
	{
		return false;
	}

	ON_SCOPE_EXIT { GameState->Destroy(); };
	UVBDifficultyStateComponent* Difficulty = NewObject<UVBDifficultyStateComponent>(GameState);
	GameState->AddInstanceComponent(Difficulty);
	Difficulty->RegisterComponent(); // InitializeComponent - ReadSave 를 부르지 않는다
	SameTag(*this, TEXT("the new world starts at the chosen level without ReadSave"), Difficulty->GetLevel(), Hard);

	TestTrue(TEXT("change in game"), Difficulty->SetLevel(Easy));
	SameTag(*this, TEXT("in-game change is written to the active save at once (next level reads it)"),
		SaveSubsystem->GetActiveGameSave()->Difficulty.Level, Easy);

	TestTrue(TEXT("old entry (no difficulty) still starts"), SaveSubsystem->StartNewGameWithDifficulty(FGameplayTag()));
	SameTag(*this, TEXT("old entry normalizes to the default level"), SaveSubsystem->GetActiveGameSave()->Difficulty.Level,
		DifficultyTag(TEXT("Difficulty.Normal")));
	return true;
}
#endif
