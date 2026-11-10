// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AI/VBEnemyCombatComponent.h"
#include "Enemy/VBBossEncounterSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "Unit/VBEnemyBarTestBoss.h"
#include "Unit/VBUITestFixture.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Data/VBHUDLayoutData.h"
#include "Enemy/VBEnemyRegistrySubsystem.h"
#include "UI/VBBossHealthBarWidget.h"
#include "UI/VBEnemyBarRules.h"
#include "UI/VBNearbyEnemyBarsWidget.h"
#include "UI/VBEnemyHealthBarWidget.h"
#include "UI/VBHealthBarWidget.h"
#include "UI/VBTargetHealthIndicatorWidget.h"
#include "UI/VBWorldIndicatorLayerWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"

// 적 체력 바 - 보스 목록, 전투 있음/없음 알림, 보스의 연결.

class FVBEnemyBarTestAccess
{
public:
	static void InCombatChanged(AVBBossBase& Boss, bool bInCombat) { Boss.HandleInCombatChanged(bInCombat); }
	static UVBEnemyHealthBarWidget* BossBar(const UVBBossHealthBarWidget& Widget) { return Widget.BossBar; }
	static TSubclassOf<UVBEnemyHealthBarWidget> BarClass(const UVBBossHealthBarWidget& Widget) { return Widget.BarClass; }
	static TSubclassOf<UVBEnemyHealthBarWidget> BarClass(const UVBTargetHealthIndicatorWidget& Widget) { return Widget.BarClass; }
	static TSubclassOf<UVBEnemyHealthBarWidget> BarClass(const UVBNearbyEnemyBarsWidget& Widget) { return Widget.BarClass; }
};

namespace VBEnemyBarTest
{
	// 격리된 게임 월드. BeginPlay 는 돌지 않는다(InitializeActorsForPlay 없음) - 보스의 구독 대신 시험이 연결을 직접 부른다.
	UWorld* MakeWorld(FAutomationTestBase& Test)
	{
		const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
			.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
			.ShouldSimulatePhysics(false).SetTransactional(false);
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
		Test.TestNotNull(TEXT("isolated world"), World);
		return World;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyBarBossRegistryTest, "VB.Unit.EnemyBar.BossRegistry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnemyBarBossRegistryTest::RunTest(const FString& Parameters)
{
	UWorld* World = VBEnemyBarTest::MakeWorld(*this);
	if (!World)
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };

	UVBBossEncounterSubsystem* Registry = World->GetSubsystem<UVBBossEncounterSubsystem>();
	if (!TestNotNull(TEXT("registry"), Registry))
	{
		return false;
	}

	AVBEnemyBarTestBoss* First = World->SpawnActor<AVBEnemyBarTestBoss>();
	AVBEnemyBarTestBoss* Second = World->SpawnActor<AVBEnemyBarTestBoss>();
	if (!TestNotNull(TEXT("first boss"), First) || !TestNotNull(TEXT("second boss"), Second))
	{
		return false;
	}

	TArray<AVBBossBase*> Announced;
	Registry->OnEngagedBossChanged.AddLambda([&Announced](AVBBossBase* Boss) { Announced.Add(Boss); });

	TestNull(TEXT("empty at start"), Registry->GetEngagedBoss());
	Registry->SetEngaged(First, false);
	TestEqual(TEXT("unengaging an unknown boss announces nothing"), Announced.Num(), 0);

	Registry->SetEngaged(First, true);
	Registry->SetEngaged(First, true);
	TestEqual(TEXT("same value twice announces once"), Announced.Num(), 1);
	TestTrue(TEXT("first boss shown"), Registry->GetEngagedBoss() == First);

	Registry->SetEngaged(Second, true);
	TestTrue(TEXT("most recent boss shown"), Registry->GetEngagedBoss() == Second);
	Registry->SetEngaged(Second, false);
	TestTrue(TEXT("falls back to the earlier boss"), Registry->GetEngagedBoss() == First);
	TestEqual(TEXT("three changes announced"), Announced.Num(), 3);

	Registry->SetEngaged(First, false);
	TestNull(TEXT("empty again"), Registry->GetEngagedBoss());
	TestTrue(TEXT("empty is announced as null"), Announced.Num() == 4 && Announced.Last() == nullptr);

	// 파괴된 보스는 목록에서 사라진다(약참조).
	Registry->SetEngaged(First, true);
	First->Destroy();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestNull(TEXT("destroyed boss is dropped"), Registry->GetEngagedBoss());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyBarInCombatEventTest, "VB.Unit.EnemyBar.InCombatEvent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnemyBarInCombatEventTest::RunTest(const FString& Parameters)
{
	UWorld* World = VBEnemyBarTest::MakeWorld(*this);
	if (!World)
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };

	AActor* Owner = World->SpawnActor<AActor>();
	AActor* TargetA = World->SpawnActor<AActor>();
	AActor* TargetB = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("owner"), Owner) || !TestNotNull(TEXT("target A"), TargetA) || !TestNotNull(TEXT("target B"), TargetB))
	{
		return false;
	}

	UVBEnemyCombatComponent* Combat = NewObject<UVBEnemyCombatComponent>(Owner);
	Combat->RegisterComponent();

	TArray<bool> Events;
	Combat->OnInCombatChanged.AddLambda([&Events](bool bInCombat) { Events.Add(bInCombat); });

	Combat->SetCombatTarget(nullptr);
	TestEqual(TEXT("no combat -> no combat is silent"), Events.Num(), 0);
	Combat->SetCombatTarget(TargetA);
	Combat->SetCombatTarget(TargetA);
	Combat->SetCombatTarget(TargetB);
	TestTrue(TEXT("entering announces once; same or switched target is silent"), Events.Num() == 1 && Events[0]);
	TestTrue(TEXT("IsInCombat follows"), Combat->IsInCombat());

	// 타겟이 파괴돼 약참조가 먼저 비어도 다음 없음은 알린다.
	TargetB->Destroy();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	Combat->SetCombatTarget(nullptr);
	TestTrue(TEXT("leaving after the target was destroyed is announced"), Events.Num() == 2 && !Events[1]);
	TestFalse(TEXT("IsInCombat cleared"), Combat->IsInCombat());

	Combat->SetCombatTarget(TargetA);
	Combat->LeaveCombat();
	TestEqual(TEXT("LeaveCombat announces through the same path"), Events.Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyBarBossEngagedTest, "VB.Unit.EnemyBar.BossEngaged",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnemyBarBossEngagedTest::RunTest(const FString& Parameters)
{
	UWorld* World = VBEnemyBarTest::MakeWorld(*this);
	if (!World)
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };

	UVBBossEncounterSubsystem* Registry = World->GetSubsystem<UVBBossEncounterSubsystem>();
	AVBEnemyBarTestBoss* Boss = World->SpawnActor<AVBEnemyBarTestBoss>();
	if (!TestNotNull(TEXT("registry"), Registry) || !TestNotNull(TEXT("boss"), Boss))
	{
		return false;
	}

	if (!TestTrue(TEXT("spawned boss has authority (server path)"), Boss->HasAuthority()))
	{
		return false;
	}

	FVBEnemyBarTestAccess::InCombatChanged(*Boss, true);
	TestTrue(TEXT("in combat -> engaged"), Boss->IsEngaged());
	TestTrue(TEXT("engaged boss reaches the registry (listen host has no OnRep)"), Registry->GetEngagedBoss() == Boss);

	FVBEnemyBarTestAccess::InCombatChanged(*Boss, false);
	TestFalse(TEXT("out of combat -> not engaged"), Boss->IsEngaged());
	TestNull(TEXT("registry cleared"), Registry->GetEngagedBoss());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyBarNumberFormatTest, "VB.Unit.EnemyBar.NumberFormat",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnemyBarNumberFormatTest::RunTest(const FString& Parameters)
{
	// 바 숫자 - 현재 = 올림(살아 있으면 0 이 안 보이게), 최대 = 반올림.
	auto Text = [](float Health, float Max, EVBBarNumberFormat Format) { return UVBAttributeBarWidget::FormatHealthNumber(Health, Max, Format).ToString(); };
	TestEqual(TEXT("current and max"), Text(85.0f, 120.0f, EVBBarNumberFormat::CurrentAndMax), FString(TEXT("85 / 120")));
	TestEqual(TEXT("current only"), Text(85.0f, 120.0f, EVBBarNumberFormat::Current), FString(TEXT("85")));
	TestEqual(TEXT("hidden is empty"), Text(85.0f, 120.0f, EVBBarNumberFormat::Hidden), FString());
	TestEqual(TEXT("a sliver of health shows 1, not 0"), Text(0.4f, 120.0f, EVBBarNumberFormat::Current), FString(TEXT("1")));
	TestEqual(TEXT("fraction rounds current up"), Text(84.2f, 120.0f, EVBBarNumberFormat::Current), FString(TEXT("85")));
	TestEqual(TEXT("max rounds to nearest"), Text(10.0f, 114.6f, EVBBarNumberFormat::CurrentAndMax), FString(TEXT("10 / 115")));
	TestEqual(TEXT("full bar with a fractional max never reads above max"),
		Text(119.4f, 119.4f, EVBBarNumberFormat::CurrentAndMax), FString(TEXT("119 / 119")));
	TestEqual(TEXT("dead or negative shows 0"), Text(-3.0f, 120.0f, EVBBarNumberFormat::Current), FString(TEXT("0")));
	TestEqual(TEXT("NaN shows 0"), Text(std::numeric_limits<float>::quiet_NaN(), 120.0f, EVBBarNumberFormat::Current), FString(TEXT("0")));

	// 자산 배선: 숫자가 보이려면 바 WBP 에 HealthText 가 있어야 한다(선택 바인딩이라 빠져도 조용하다 - 그래서 여기서 잡는다).
	for (const TCHAR* Path : {TEXT("/Game/VB/UI/WBP_HealthBar.WBP_HealthBar_C"), TEXT("/Game/VB/UI/WBP_EnemyHealthBar.WBP_EnemyHealthBar_C")})
	{
		const UWidgetBlueprintGeneratedClass* Class = LoadObject<UWidgetBlueprintGeneratedClass>(nullptr, Path);
		const UWidgetTree* Tree = Class ? Class->GetWidgetTreeArchetype() : nullptr;
		TestTrue(FString::Printf(TEXT("%s has a HealthText text block"), Path), Tree
			&& Cast<UTextBlock>(Tree->FindWidget(TEXT("HealthText"))));
	}

	return true;
}

// 근처 적 바 고르기(10절): 반경, 화면 안, 살아 있음, 락온 대상과 보스 제외, 가까운 순 최대 개수.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyBarPickNearbyTest, "VB.Unit.EnemyBar.PickNearby",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnemyBarPickNearbyTest::RunTest(const FString& Parameters)
{
	UWorld* World = VBEnemyBarTest::MakeWorld(*this);
	if (!World)
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
	TArray<AActor*> A;
	for (int32 Index = 0; Index < 7; ++Index)
	{
		A.Add(World->SpawnActor<AActor>());
	}

	auto Make = [](AActor* Actor, float Distance, bool bOnScreen = true, bool bDead = false)
	{
		FVBNearbyBarCandidate Candidate;
		Candidate.Actor = Actor;
		Candidate.DistanceSquared = Distance * Distance;
		Candidate.bOnScreen = bOnScreen;
		Candidate.bDead = bDead;
		return Candidate;
	};
	const TArray<FVBNearbyBarCandidate> Candidates = {
		Make(A[0], 900.0f), Make(A[1], 300.0f), Make(A[2], 600.0f),
		Make(A[3], 2000.0f), // 반경 밖
		Make(A[4], 100.0f, /*bOnScreen=*/false),
		Make(A[5], 200.0f, true, /*bDead=*/true),
		Make(A[6], 150.0f), // 락온 대상(제외)
	};
	const TArray<AActor*> Picked = VBEnemyBars::PickNearby(Candidates, 1500.0f, 2, A[6], nullptr);
	TestEqual(TEXT("max count"), Picked.Num(), 2);
	TestTrue(TEXT("nearest first, skipping off-screen, dead and the locked target"), Picked.Num() == 2 && Picked[0] == A[1]
		&& Picked[1] == A[2]);
	const TArray<AActor*> All = VBEnemyBars::PickNearby(Candidates, 1500.0f, 10, A[6], A[2]);
	TestTrue(TEXT("second exclusion (boss) is skipped too"), !All.Contains(A[2]) && All.Contains(A[0]) && All.Contains(A[1]));
	TestFalse(TEXT("beyond the radius is never picked"), All.Contains(A[3]));
	TestEqual(TEXT("zero max picks nothing"), VBEnemyBars::PickNearby(Candidates, 1500.0f, 0, nullptr, nullptr).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyBarRegistryTest, "VB.Unit.EnemyBar.EnemyRegistry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnemyBarRegistryTest::RunTest(const FString& Parameters)
{
	UWorld* World = VBEnemyBarTest::MakeWorld(*this);
	if (!World)
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
	UVBEnemyRegistrySubsystem* Registry = World->GetSubsystem<UVBEnemyRegistrySubsystem>();
	AVBEnemyBarTestBoss* First = World->SpawnActor<AVBEnemyBarTestBoss>();
	AVBEnemyBarTestBoss* Second = World->SpawnActor<AVBEnemyBarTestBoss>();
	if (!TestNotNull(TEXT("registry"), Registry) || !TestNotNull(TEXT("first"), First) || !TestNotNull(TEXT("second"), Second))
	{
		return false;
	}

	Registry->Register(First);
	Registry->Register(First);
	Registry->Register(Second);
	TestEqual(TEXT("no duplicate rows"), Registry->GetEnemies().Num(), 2);
	Registry->Unregister(First);
	TestEqual(TEXT("unregister removes"), Registry->GetEnemies().Num(), 1);
	Second->Destroy();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestEqual(TEXT("destroyed enemy is swept on read"), Registry->GetEnemies().Num(), 0);
	return true;
}

// 실제 자산 배선: 이 줄이 끊기면 바가 조용히 안 뜬다(층, HUD 는 경고만) - 그래서 자산으로 잠근다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyBarRoutesTest, "VB.Unit.EnemyBar.Routes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnemyBarRoutesTest::RunTest(const FString& Parameters)
{
	const UVBHUDLayoutData* Layout = LoadObject<UVBHUDLayoutData>(nullptr, TEXT("/Game/VB/UI/DA_HUDLayout.DA_HUDLayout"));
	if (!TestNotNull(TEXT("DA_HUDLayout"), Layout))
	{
		return false;
	}

	TestTrue(TEXT("boss bar slot is placed"), Layout->Slots.ContainsByPredicate(
		[](const FVBUISlotPlacement& Placement) { return Placement.Slot == VBGameplayTags::UI_Slot_HUD_BossBar; }));
	const FVBUISlotDefaultWidget* BossRow = Layout->Widgets.FindByPredicate(
		[](const FVBUISlotDefaultWidget& Row) { return Row.Slot == VBGameplayTags::UI_Slot_HUD_BossBar; });
	if (!TestNotNull(TEXT("boss bar slot has a default widget"), BossRow))
	{
		return false;
	}

	if (!TestTrue(TEXT("default widget is a boss bar"), BossRow->WidgetClass
		&& BossRow->WidgetClass->IsChildOf(UVBBossHealthBarWidget::StaticClass())))
	{
		return false;
	}

	TestTrue(TEXT("boss bar WBP sets BarClass"),
		FVBEnemyBarTestAccess::BarClass(*GetDefault<UVBBossHealthBarWidget>(BossRow->WidgetClass)) != nullptr);

	UClass* LayerClass = LoadClass<UVBWorldIndicatorLayerWidget>(nullptr,
		TEXT("/Game/VB/UI/WBP_WorldIndicatorLayer.WBP_WorldIndicatorLayer_C"));
	if (!TestNotNull(TEXT("WBP_WorldIndicatorLayer"), LayerClass))
	{
		return false;
	}

	const FClassProperty* IndicatorProp = FindFProperty<FClassProperty>(UVBWorldIndicatorLayerWidget::StaticClass(),
		TEXT("TargetHealthIndicatorClass"));
	UClass* IndicatorClass = IndicatorProp ? Cast<UClass>(IndicatorProp->GetPropertyValue_InContainer(LayerClass->GetDefaultObject())) : nullptr;
	if (!TestTrue(TEXT("indicator layer has the lock-on bar branch"), IndicatorClass
		&& IndicatorClass->IsChildOf(UVBTargetHealthIndicatorWidget::StaticClass())))
	{
		return false;
	}

	TestTrue(TEXT("lock-on bar WBP sets BarClass"),
		FVBEnemyBarTestAccess::BarClass(*GetDefault<UVBTargetHealthIndicatorWidget>(IndicatorClass)) != nullptr);
	const FClassProperty* NearbyProp = FindFProperty<FClassProperty>(UVBWorldIndicatorLayerWidget::StaticClass(),
		TEXT("NearbyEnemyBarsClass"));
	UClass* NearbyClass = NearbyProp ? Cast<UClass>(NearbyProp->GetPropertyValue_InContainer(LayerClass->GetDefaultObject())) : nullptr;
	if (TestTrue(TEXT("indicator layer has the nearby enemy bars branch"), NearbyClass
		&& NearbyClass->IsChildOf(UVBNearbyEnemyBarsWidget::StaticClass())))
	{
		TestTrue(TEXT("nearby bars WBP sets BarClass"),
			FVBEnemyBarTestAccess::BarClass(*GetDefault<UVBNearbyEnemyBarsWidget>(NearbyClass)) != nullptr);
	}

	// 플레이어 바는 부모를 뽑아낸 뒤에도 같은 갈래다.
	UClass* PlayerBar = LoadClass<UUserWidget>(nullptr, TEXT("/Game/VB/UI/WBP_HealthBar.WBP_HealthBar_C"));
	TestTrue(TEXT("WBP_HealthBar is still a player health bar"), PlayerBar && PlayerBar->IsChildOf(UVBHealthBarWidget::StaticClass()));
	return true;
}

// 보스 바의 수명(리뷰 F1): 싸움이 먼저 시작된 뒤 HUD 가 떠도 보이고, 해체에서 놓고, 같은 UObject 재구성에서 정확히 한 번 다시 묶는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyBarBossBarLifecycleTest, "VB.Unit.EnemyBar.BossBarLifecycle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnemyBarBossBarLifecycleTest::RunTest(const FString& Parameters)
{
	VBUITest::FRowFixture Fixture;
	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UVBBossEncounterSubsystem* Registry = Fixture.World->GetSubsystem<UVBBossEncounterSubsystem>();
	// 이 월드는 액터 초기화가 켜져 있다 - 그대로 스폰하면 AI 컨트롤러가 빙의해 행동 트리를 연다. 시험에 필요 없는 길이라 끈다.
	AVBEnemyBarTestBoss* Boss = Fixture.World->SpawnActorDeferred<AVBEnemyBarTestBoss>(AVBEnemyBarTestBoss::StaticClass(),
		FTransform::Identity);
	if (Boss)
	{
		Boss->AutoPossessAI = EAutoPossessAI::Disabled;
		Boss->FinishSpawning(FTransform::Identity);
	}

	UAbilitySystemComponent* BossASC = Boss ? Boss->GetAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("registry"), Registry) || !TestNotNull(TEXT("boss ASC"), BossASC))
	{
		return false;
	}

	BossASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), 200.0f);
	BossASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 200.0f);

	Registry->SetEngaged(Boss, true); // HUD 보다 먼저
	UVBBossHealthBarWidget* Widget = Fixture.MakeRow<UVBBossHealthBarWidget>(*this,
		TEXT("/Game/VB/UI/WBP_BossHealthBar.WBP_BossHealthBar_C"));
	if (!Widget)
	{
		return false;
	}

	UVBEnemyHealthBarWidget* Bar = FVBEnemyBarTestAccess::BossBar(*Widget);
	if (!TestNotNull(TEXT("bar built from BarClass"), Bar))
	{
		return false;
	}

	VBUITest::FSlateLife Life;
	Life.Construct(*Widget);
	TestTrue(TEXT("late HUD shows the already engaged boss"), Widget->GetVisibility() != ESlateVisibility::Collapsed);
	TestTrue(TEXT("bar bound to the boss ASC"), Bar->GetBoundASC() == BossASC);

	Life.Destruct(*Widget);
	TestFalse(TEXT("destruct leaves the registry"), Registry->OnEngagedBossChanged.IsBoundToObject(Widget));
	TestNull(TEXT("destruct releases the boss ASC"), Bar->GetBoundASC());

	Life.Construct(*Widget);
	TestTrue(TEXT("reconstruct rebinds the boss ASC"), Bar->GetBoundASC() == BossASC);
	TestTrue(TEXT("bar listens to the boss health after reconstruct"),
		BossASC->GetGameplayAttributeValueChangeDelegate(UVBHealthAttributeSet::GetHealthAttribute()).IsBoundToObject(Bar));

	Registry->SetEngaged(Boss, false);
	TestEqual(TEXT("disengage collapses the bar"), Widget->GetVisibility(), ESlateVisibility::Collapsed);
	TestNull(TEXT("disengage releases the ASC"), Bar->GetBoundASC());
	TestEqual(TEXT("registry listens exactly once after reconstruct"), Registry->OnEngagedBossChanged.RemoveAll(Widget), 1);
	Life.Destruct(*Widget);
	return true;
}

#endif
