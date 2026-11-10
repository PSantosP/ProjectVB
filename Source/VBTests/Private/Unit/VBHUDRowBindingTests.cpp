// ProjectVB Copyright (c) 2025. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Blueprint/UserWidget.h"
#include "Components/ProgressBar.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameModes/VBGameState.h"
#include "Input/VBInputDeviceManager.h"
#include "Inventory/VBInventoryComponent.h"
#include "Narrative/Quest/VBQuestLogComponent.h"
#include "Progression/VBProgressionComponent.h"
#include "Narrative/Dialogue/VBSpokenLineComponent.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Narrative/Tutorial/VBTutorialComponent.h"
#include "Player/VBPlayerController.h"
#include "Player/VBPlayerState.h"
#include "Player/VBWeaponWheelComponent.h"
#include "TimerManager.h"
#include "UI/Dialogue/VBSubtitleWidget.h"
#include "UI/Inventory/VBQuickSlotBarWidget.h"
#include "UI/Progress/VBProgressionLineWidget.h"
#include "UI/Progress/VBTitleCardWidget.h"
#include "UI/Quest/VBQuestTrackerWidget.h"
#include "UI/Tutorial/VBTutorialHintWidget.h"
#include "UI/VBHealthBarWidget.h"
#include "UI/VBWeaponWheelWidget.h"
#include "VBUITestFixture.h"
#include "AbilitySystem/VBThreatTrackerSubsystem.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractorComponent.h"
#include "UI/Quest/VBObjectiveMarkerLayerWidget.h"
#include "UI/Indicator/VBRegisteredIndicatorsWidget.h"
#include "UI/VBInteractPromptWidget.h"
#include "UI/VBNearbyEnemyBarsWidget.h"
#include "UI/VBOffscreenThreatWidget.h"
#include "UI/VBTargetHealthIndicatorWidget.h"
#include "UI/VBScreenEdge.h"
#include "UI/VBWorldIndicatorLayerWidget.h"
#include "UI/VBWorldIndicatorWidget.h"

// HUD 줄 위젯이 스스로 묶는가. 구성, 해체는 실제 Slate 길(TakeWidget -> NativeConstruct, Slate 해제 -> NativeDestruct)로 탄다.
// 같은 UObject 를 다시 구성해 "다시 정확히 한 번" 을 본다.
// 못 보는 것: HUD 캔버스의 전파, 원격 클라의 늦은 PlayerState, GameState 복제 순서, 실제 화면(PIE 몫).
class FVBHUDRowTestAccess
{
public:
	static bool IsWaitingForGameState(const UVBTitleCardWidget& Card) { return Card.StoryBinding.GetStory() == nullptr; }
	static const UVBStoryStateComponent* BaselineStory(const UVBTitleCardWidget& Card) { return Card.BaselineStory.Get(); }
	static bool& BaselineSeen(UVBTitleCardWidget& Card) { return Card.ChapterBaseline.bSeen; }
	static bool IsDelayTimerActive(const UVBHealthBarWidget& Bar, const UWorld& World)
	{
		return World.GetTimerManager().IsTimerActive(Bar.DelayBarTimerHandle);
	}
	// 칸 줄이 이 장치 관리자의 장치 바뀜을 실제로 듣는가(동적 델리게이트 목록에 그 함수가 있는가).
	static bool ListensToDevice(const UVBQuickSlotBarWidget& Bar, const UVBInputDeviceManager& Manager)
	{
		return Manager.OnInputDeviceChanged.Contains(&Bar, GET_FUNCTION_NAME_CHECKED(UVBQuickSlotBarWidget, HandleInputDeviceChanged));
	}

	// 월드 표시기.
	static void SetFocus(UVBInteractorComponent& Interactor, UVBInteractableComponent* Focus) { Interactor.SetFocus(Focus); }
	static const UVBInteractableComponent* PromptTarget(const UVBInteractPromptWidget& Prompt) { return Prompt.Target.Get(); }
	static ECommonInputType PromptInputType(const UVBInteractPromptWidget& Prompt) { return Prompt.InputType; }
	static void BindPawn(UVBInteractPromptWidget& Prompt, APawn* Pawn) { Prompt.BindPawn(Pawn); }
	static const TArray<TObjectPtr<UVBWorldIndicatorWidget>>& Indicators(const UVBWorldIndicatorLayerWidget& Layer) { return Layer.Indicators; }
	static const UCanvasPanel* IndicatorCanvas(const UVBWorldIndicatorLayerWidget& Layer) { return Layer.IndicatorCanvas; }
	static void Tick(UVBWorldIndicatorLayerWidget& Layer, const FVector2D& Size)
	{
		Layer.NativeTick(FGeometry::MakeRoot(Size, FSlateLayoutTransform()), 0.0f);
	}
};

namespace
{
	// 시험 월드, Slate 수명은 공용 셋업(VBUITestFixture.h)에 있다 - UI 슬롯 시험과 같이 쓴다.
	using VBUITest::FRowFixture;
	using VBUITest::FSlateLife;

	FOnGameplayAttributeValueChange& HealthDelegate(UAbilitySystemComponent& ASC)
	{
		return ASC.GetGameplayAttributeValueChangeDelegate(UVBHealthAttributeSet::GetHealthAttribute());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHUDRowsBindThemselvesTest, "VB.Unit.HUD.RowsBindThemselves",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBHUDRowsBindThemselvesTest::RunTest(const FString& Parameters)
{
	using Access = FVBHUDRowTestAccess;
	FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		Fixture.Shutdown();
		return false;
	}

	AVBPlayerState* StateA = Fixture.SpawnPlayerState(*this);
	AVBPlayerState* StateB = Fixture.SpawnPlayerState(*this);
	if (!StateA || !StateB)
	{
		Fixture.Shutdown();
		return false;
	}

	Fixture.SetPlayerState(StateA);

	UVBHealthBarWidget* Health = Fixture.MakeRow<UVBHealthBarWidget>(*this, TEXT("/Game/VB/UI/WBP_HealthBar.WBP_HealthBar_C"));
	UVBQuickSlotBarWidget* QuickSlots = Fixture.MakeRow<UVBQuickSlotBarWidget>(*this,
		TEXT("/Game/VB/UI/Inventory/WBP_QuickSlotBar.WBP_QuickSlotBar_C"));
	UVBTutorialHintWidget* Hint = Fixture.MakeRow<UVBTutorialHintWidget>(*this,
		TEXT("/Game/VB/UI/Tutorial/WBP_TutorialHint.WBP_TutorialHint_C"));
	UVBSubtitleWidget* Subtitle = Fixture.MakeRow<UVBSubtitleWidget>(*this, TEXT("/Game/VB/UI/Dialogue/WBP_Subtitle.WBP_Subtitle_C"));
	UVBWeaponWheelWidget* Wheel = Fixture.MakeRow<UVBWeaponWheelWidget>(*this, nullptr);
	UVBTitleCardWidget* Chapter = Fixture.MakeRow<UVBTitleCardWidget>(*this,
		TEXT("/Game/VB/UI/Progress/WBP_ChapterCard.WBP_ChapterCard_C"));
	UVBTitleCardWidget* Region = Fixture.MakeRow<UVBTitleCardWidget>(*this,
		TEXT("/Game/VB/UI/Progress/WBP_RegionBanner.WBP_RegionBanner_C"));
	UVBSpokenLineComponent* Lines = Fixture.Controller->FindComponentByClass<UVBSpokenLineComponent>();
	UVBWeaponWheelComponent* WheelSource = Fixture.Controller->GetWeaponWheel();
	if (!Health || !QuickSlots || !Hint || !Subtitle || !Wheel || !Chapter || !Region
		|| !TestNotNull(TEXT("spoken lines on the controller"), Lines) || !TestNotNull(TEXT("weapon wheel on the controller"), WheelSource))
	{
		Fixture.Shutdown();
		return false;
	}
	// 역할은 WBP 클래스 기본값이다(설계 3-1d 10.5) - 시험이 주입하지 않는다. 잘못 저작된 자산을 가리지 않으려고.
	TestTrue(TEXT("WBP_ChapterCard listens to chapters (class default)"), Chapter->GetSource() == EVBTitleCardSource::Chapter);
	TestTrue(TEXT("WBP_RegionBanner listens to regions (class default)"), Region->GetSource() == EVBTitleCardSource::Region);
	// 줄 위젯은 주인 PC 로 데이터를 찾는다 - 시험 문맥에서 주인이 서지 않으면 아래 단언 전부가 원인을 숨긴 채 실패한다.
	if (!TestTrue(TEXT("rows see the fixture controller as their owning player"), Subtitle->GetOwningPlayer() == Fixture.Controller))
	{
		AddInfo(FString::Printf(TEXT("owning player = %s, local player valid = %d"), *GetNameSafe(Subtitle->GetOwningPlayer()),
			Subtitle->GetOwningLocalPlayer() != nullptr));
	}

	UAbilitySystemComponent* AscA = StateA->GetAbilitySystemComponent();
	UAbilitySystemComponent* AscB = StateB->GetAbilitySystemComponent();
	UVBInventoryComponent* InventoryA = UVBInventoryComponent::FindFor(StateA);
	UVBInventoryComponent* InventoryB = UVBInventoryComponent::FindFor(StateB);
	UVBTutorialComponent* TutorialA = UVBTutorialComponent::FindFor(StateA);
	UVBTutorialComponent* TutorialB = UVBTutorialComponent::FindFor(StateB);
	if (!TestNotNull(TEXT("ASC A"), AscA) || !TestNotNull(TEXT("ASC B"), AscB) || !TestNotNull(TEXT("inventory A"), InventoryA)
		|| !TestNotNull(TEXT("inventory B"), InventoryB) || !TestNotNull(TEXT("tutorial A"),
			TutorialA) || !TestNotNull(TEXT("tutorial B"), TutorialB))
	{
		Fixture.Shutdown();
		return false;
	}

	// 구성 전에는 아무것도 듣지 않는다 - 묶는 자리가 생성이 아니라 구성임을 확인한다.
	TestFalse(TEXT("health bar does not bind before construct"), HealthDelegate(*AscA).IsBoundToObject(Health));
	TestFalse(TEXT("subtitle does not bind before construct"), Lines->OnLineStarted.IsBoundToObject(Subtitle));

	TArray<UUserWidget*> Rows = {Health, QuickSlots, Hint, Subtitle, Wheel, Chapter, Region};
	TArray<FSlateLife> Lives;
	Lives.SetNum(Rows.Num());

	// GameState 가 아직 없다 - 챕터 카드는 GameState 신호를 기다린다(원격 클라의 늦은 GameState 와 같은 상태).
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Lives[Index].Construct(*Rows[Index]);
	}

	TestTrue(TEXT("health bar binds the ASC of the controller's PlayerState"), HealthDelegate(*AscA).IsBoundToObject(Health));
	TestTrue(TEXT("quick slots bind the PlayerState inventory"), InventoryA->OnQuickSlotsChanged.IsBoundToObject(QuickSlots));
	TestTrue(TEXT("hint binds the PlayerState tutorial"), TutorialA->OnTutorialChanged.IsBoundToObject(Hint));
	TestTrue(TEXT("subtitle binds the controller's spoken lines"), Lines->OnLineStarted.IsBoundToObject(Subtitle));
	TestTrue(TEXT("weapon wheel binds the controller's wheel"), WheelSource->OnChanged.IsBoundToObject(Wheel));
	TestTrue(TEXT("region banner binds the controller's region signal"), Fixture.Controller->OnRegionEntered.IsBoundToObject(Region));
	TestFalse(TEXT("chapter card does not listen to regions"), Fixture.Controller->OnRegionEntered.IsBoundToObject(Chapter));
	TestTrue(TEXT("chapter card waits for the GameState"), Access::IsWaitingForGameState(*Chapter));

	AVBGameState* GameStateA = Fixture.World->SpawnActor<AVBGameState>();
	UVBStoryStateComponent* StoryA = GameStateA ? GameStateA->FindComponentByClass<UVBStoryStateComponent>() : nullptr;
	if (!TestNotNull(TEXT("story state on the game state"), StoryA))
	{
		Fixture.Shutdown();
		return false;
	}

	Fixture.World->SetGameState(GameStateA);
	TestTrue(TEXT("chapter card binds the story when the GameState arrives"), StoryA->OnStoryStateChanged.IsBoundToObject(Chapter));
	TestFalse(TEXT("chapter card stops waiting once bound"), Access::IsWaitingForGameState(*Chapter));
	TestTrue(TEXT("chapter baseline belongs to that story"), Access::BaselineStory(*Chapter) == StoryA);

	// 체력 바: 같은 ASC 를 다시 줘도 한 번. 체력이 바뀌면 잔상 바 타이머가 돈다 - 해체가 그것도 멈춰야 한다.
	Health->InitializeWithASC(AscA);
	AscA->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), 100.0f);
	AscA->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 60.0f);
	AscA->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 40.0f);
	TestTrue(TEXT("a health drop starts the delay bar timer"), Access::IsDelayTimerActive(*Health, *Fixture.World));

	// 기준선이 재구성에서 살아남는지 보려고 "본 적 있음" 을 켜 둔다(카탈로그가 없는 시험 월드라 스스로는 안 켜진다).
	Access::BaselineSeen(*Chapter) = true;

	// 해체 - 전부 놓는다.
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Lives[Index].Destruct(*Rows[Index]);
	}

	TestFalse(TEXT("health bar releases the ASC on destruct"), HealthDelegate(*AscA).IsBoundToObject(Health));
	TestFalse(TEXT("destruct stops the delay bar timer"), Access::IsDelayTimerActive(*Health, *Fixture.World));
	TestFalse(TEXT("quick slots release on destruct"), InventoryA->OnQuickSlotsChanged.IsBoundToObject(QuickSlots));
	TestFalse(TEXT("hint releases on destruct"), TutorialA->OnTutorialChanged.IsBoundToObject(Hint));
	TestFalse(TEXT("subtitle releases on destruct"), Lines->OnLineStarted.IsBoundToObject(Subtitle));
	TestFalse(TEXT("weapon wheel releases on destruct"), WheelSource->OnChanged.IsBoundToObject(Wheel));
	TestFalse(TEXT("region banner releases on destruct"), Fixture.Controller->OnRegionEntered.IsBoundToObject(Region));
	TestFalse(TEXT("chapter card releases on destruct"), StoryA->OnStoryStateChanged.IsBoundToObject(Chapter));

	// 같은 UObject 를 다시 구성 - 다시 묶이고, 정확히 한 번이다(RemoveAll 이 지운 개수로 센다).
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Lives[Index].Construct(*Rows[Index]);
	}

	TestTrue(TEXT("chapter baseline survives a reconstruct on the same story"), Access::BaselineSeen(*Chapter));
	// 묶였다는 것만이 아니라 실제 사건이 바에 닿는지 - 재구성 뒤 체력을 바꿔 바의 비율을 읽는다.
	if (const UProgressBar* Bar = Cast<UProgressBar>(Health->GetWidgetFromName(TEXT("HealthBar")));
		TestNotNull(TEXT("health progress bar in WBP_HealthBar"), Bar))
	{
		AscA->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 20.0f);
		TestEqual(TEXT("a health change after reconstruct reaches the bar"), Bar->GetPercent(), 0.2f, 0.001f);
	}

	TestEqual(TEXT("subtitle rebinds exactly once"), Lines->OnLineStarted.RemoveAll(Subtitle), 1);
	TestEqual(TEXT("weapon wheel rebinds exactly once"), WheelSource->OnChanged.RemoveAll(Wheel), 1);
	TestEqual(TEXT("region banner rebinds exactly once"), Fixture.Controller->OnRegionEntered.RemoveAll(Region), 1);
	TestEqual(TEXT("chapter card rebinds exactly once"), StoryA->OnStoryStateChanged.RemoveAll(Chapter), 1);

	// PlayerState 교체 A -> B: 옛 것에서 떼고 새 것에 묶는다(체력은 B 의 ASC, 칸, 힌트는 B 의 부품).
	Fixture.SetPlayerState(StateB);
	TestFalse(TEXT("health bar leaves ASC A"), HealthDelegate(*AscA).IsBoundToObject(Health));
	TestEqual(TEXT("health bar binds ASC B exactly once"), HealthDelegate(*AscB).RemoveAll(Health), 1);
	TestFalse(TEXT("quick slots leave inventory A"), InventoryA->OnQuickSlotsChanged.IsBoundToObject(QuickSlots));
	TestEqual(TEXT("quick slots bind inventory B exactly once"), InventoryB->OnQuickSlotsChanged.RemoveAll(QuickSlots), 1);
	TestFalse(TEXT("hint leaves tutorial A"), TutorialA->OnTutorialChanged.IsBoundToObject(Hint));
	TestEqual(TEXT("hint binds tutorial B exactly once"), TutorialB->OnTutorialChanged.RemoveAll(Hint), 1);

	// 다른 세계 사실에 묶이면 기준선을 새로 잡는다.
	Lives[5].Destruct(*Chapter);
	AVBGameState* GameStateB = Fixture.World->SpawnActor<AVBGameState>();
	UVBStoryStateComponent* StoryB = GameStateB ? GameStateB->FindComponentByClass<UVBStoryStateComponent>() : nullptr;
	if (TestNotNull(TEXT("second story state"), StoryB))
	{
		Fixture.World->SetGameState(GameStateB);
		Lives[5].Construct(*Chapter);
		TestTrue(TEXT("chapter card binds the new story"), StoryB->OnStoryStateChanged.IsBoundToObject(Chapter));
		TestTrue(TEXT("baseline moves to the new story"), Access::BaselineStory(*Chapter) == StoryB);
		TestFalse(TEXT("baseline restarts for a different story"), Access::BaselineSeen(*Chapter));
		// 구성된 채로 GameState 가 바뀌어도 새 세계 사실을 듣고 기준선을 옮긴다(해체, 재구성 없이).
		Fixture.World->SetGameState(GameStateA);
		TestTrue(TEXT("replaced GameState: chapter card listens to the new story"), StoryA->OnStoryStateChanged.IsBoundToObject(Chapter));
		TestFalse(TEXT("replaced GameState: chapter card releases the old story"), StoryB->OnStoryStateChanged.IsBoundToObject(Chapter));
		TestTrue(TEXT("replaced GameState: baseline moves with it"), Access::BaselineStory(*Chapter) == StoryA);
	}

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Lives[Index].Destruct(*Rows[Index]);
	}

	Fixture.Shutdown();
	return true;
}

// 퀘스트 목표 줄, 성장 줄(HUD 가 직접 짓던 줄을 옮긴 네이티브 위젯)과 퀵슬롯 칸 줄의 키 이름 소스(폰의 장치 관리자)를 본다(설계 11.5 b2).
// 못 보는 것: 키 이름의 글 자체(시험 PC 에는 퀵슬롯 액션, IMC 가 없다 - 규칙은 VBInputKeyText 하나라 HUD 프롬프트와 같다), 메뉴를 닫고 돌아올 때의 다시 읽기(CommonUI 라우터 - PIE 몫).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHUDLinesAndKeySourceTest, "VB.Unit.HUD.LinesAndKeySource",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBHUDLinesAndKeySourceTest::RunTest(const FString& Parameters)
{
	using Access = FVBHUDRowTestAccess;
	FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		Fixture.Shutdown();
		return false;
	}

	AVBPlayerState* StateA = Fixture.SpawnPlayerState(*this);
	AVBPlayerState* StateB = Fixture.SpawnPlayerState(*this);
	APawn* PawnA = Fixture.World->SpawnActor<APawn>();
	APawn* PawnB = Fixture.World->SpawnActor<APawn>();
	if (!StateA || !StateB || !TestNotNull(TEXT("pawn A"), PawnA) || !TestNotNull(TEXT("pawn B"), PawnB))
	{
		Fixture.Shutdown();
		return false;
	}

	UVBInputDeviceManager* DevicesA = NewObject<UVBInputDeviceManager>(PawnA);
	DevicesA->RegisterComponent();
	UVBInputDeviceManager* DevicesB = NewObject<UVBInputDeviceManager>(PawnB);
	DevicesB->RegisterComponent();
	Fixture.SetPlayerState(StateA);
	Fixture.Controller->Possess(PawnA);

	UVBQuestTrackerWidget* Tracker = Fixture.MakeRow<UVBQuestTrackerWidget>(*this, nullptr);
	UVBProgressionLineWidget* Line = Fixture.MakeRow<UVBProgressionLineWidget>(*this, nullptr);
	UVBQuickSlotBarWidget* QuickSlots = Fixture.MakeRow<UVBQuickSlotBarWidget>(*this,
		TEXT("/Game/VB/UI/Inventory/WBP_QuickSlotBar.WBP_QuickSlotBar_C"));
	UVBQuestLogComponent* QuestsA = UVBQuestLogComponent::FindFor(StateA);
	UVBQuestLogComponent* QuestsB = UVBQuestLogComponent::FindFor(StateB);
	UVBProgressionComponent* GrowthA = UVBProgressionComponent::FindFor(StateA);
	UVBProgressionComponent* GrowthB = UVBProgressionComponent::FindFor(StateB);
	if (!Tracker || !Line || !QuickSlots || !TestNotNull(TEXT("quest log A"), QuestsA) || !TestNotNull(TEXT("quest log B"), QuestsB)
		|| !TestNotNull(TEXT("progression A"), GrowthA) || !TestNotNull(TEXT("progression B"), GrowthB))
	{
		Fixture.Shutdown();
		return false;
	}

	TArray<UUserWidget*> Rows = {Tracker, Line, QuickSlots};
	TArray<FSlateLife> Lives;
	Lives.SetNum(Rows.Num());
	TestFalse(TEXT("progression line is collapsed before it binds"), Line->IsVisible());

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Lives[Index].Construct(*Rows[Index]);
	}

	TestTrue(TEXT("quest tracker binds the quest log list"), QuestsA->OnQuestLogChanged.IsBoundToObject(Tracker));
	TestTrue(TEXT("quest tracker also redraws on quest notices"), QuestsA->OnQuestNotice.IsBoundToObject(Tracker));
	TestTrue(TEXT("progression line binds progression"), GrowthA->OnProgressionChanged.IsBoundToObject(Line));
	TestTrue(TEXT("progression line opens once bound"), Line->IsVisible());
	TestTrue(TEXT("quick slots listen to the possessed pawn's device manager"), Access::ListensToDevice(*QuickSlots, *DevicesA));

	// 폰 교체 A -> B: 키 이름 소스를 새 폰의 장치 관리자로 옮긴다.
	Fixture.Controller->UnPossess();
	Fixture.Controller->Possess(PawnB);
	TestFalse(TEXT("quick slots leave pawn A's device manager"), Access::ListensToDevice(*QuickSlots, *DevicesA));
	TestTrue(TEXT("quick slots follow pawn B's device manager"), Access::ListensToDevice(*QuickSlots, *DevicesB));

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Lives[Index].Destruct(*Rows[Index]);
	}

	TestFalse(TEXT("quest tracker releases on destruct"), QuestsA->OnQuestLogChanged.IsBoundToObject(Tracker));
	TestFalse(TEXT("quest notice released on destruct"), QuestsA->OnQuestNotice.IsBoundToObject(Tracker));
	TestFalse(TEXT("progression line releases on destruct"), GrowthA->OnProgressionChanged.IsBoundToObject(Line));
	TestFalse(TEXT("quick slots release the device manager on destruct"), Access::ListensToDevice(*QuickSlots, *DevicesB));

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Lives[Index].Construct(*Rows[Index]);
	}

	TestEqual(TEXT("quest tracker rebinds the list exactly once"), QuestsA->OnQuestLogChanged.RemoveAll(Tracker), 1);
	TestEqual(TEXT("quest tracker rebinds notices exactly once"), QuestsA->OnQuestNotice.RemoveAll(Tracker), 1);
	TestTrue(TEXT("quick slots listen again after reconstruct"), Access::ListensToDevice(*QuickSlots, *DevicesB));

	// PlayerState 교체 A -> B.
	Fixture.SetPlayerState(StateB);
	TestFalse(TEXT("progression line leaves A"), GrowthA->OnProgressionChanged.IsBoundToObject(Line));
	TestEqual(TEXT("progression line binds B exactly once"), GrowthB->OnProgressionChanged.RemoveAll(Line), 1);
	TestEqual(TEXT("quest tracker binds B's list exactly once"), QuestsB->OnQuestLogChanged.RemoveAll(Tracker), 1);

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Lives[Index].Destruct(*Rows[Index]);
	}

	Fixture.Shutdown();
	return true;
}

// PC 의 PlayerState 진단(설계 11.4) - 늦음과 다른 클래스를 구분하고, 다른 클래스는 한 번만 말한다.
// 못 보는 것: 10초 티커가 BeginPlay 에서 걸리는지(시험 월드는 BeginPlay 를 돌리지 않는다 - 확인 함수를 직접 부른다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayerStateDiagnosticsTest, "VB.Unit.HUD.PlayerStateDiagnostics",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBPlayerStateDiagnosticsTest::RunTest(const FString& Parameters)
{
	FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		Fixture.Shutdown();
		return false;
	}
	// 정확히 한 번씩: 늦음(PlayerState 없을 때만), 다른 클래스(같은 PC 에서 두 번 와도 한 번).
	AddExpectedMessagePlain(TEXT("10초째 PlayerState 가 안 왔다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("AVBPlayerState 가 아니다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

	Fixture.Controller->WarnIfPlayerStateLate();
	APlayerState* Plain =Fixture.World->SpawnActor<APlayerState>();
	if (!TestNotNull(TEXT("plain player state"), Plain))
	{
		Fixture.Shutdown();
		return false;
	}

	Fixture.SetPlayerState(Plain);
	Fixture.SetPlayerState(Plain);
	// PlayerState 가 왔으면 늦음은 말하지 않는다.
	Fixture.Controller->WarnIfPlayerStateLate();
	// 맞는 클래스는 말하지 않는다.
	if (AVBPlayerState* Proper = Fixture.SpawnPlayerState(*this))
	{
		Fixture.SetPlayerState(Proper);
	}

	Fixture.Shutdown();
	return true;
}

namespace
{
	// 동적 델리게이트에 이 대상이 몇 번 묶였나 - 상태를 바꾸지 않고 센다(RemoveAll 은 void 다, 설계 3-1c 11.5).
	template <class TDelegate>
	int32 CountBound(const TDelegate& Delegate, const UObject* Target)
	{
		int32 Count = 0;
		for (const UObject* Bound : Delegate.GetAllObjects())
		{
			Count += Bound == Target ? 1 : 0;
		}

		return Count;
	}

	struct FPromptPawn
	{
		APawn* Pawn = nullptr;
		UVBInteractorComponent* Interactor = nullptr;
		UVBInputDeviceManager* Devices = nullptr;
		UVBInteractableComponent* Item = nullptr;

		bool Spawn(FAutomationTestBase& Test, UWorld& World, const TCHAR* Name)
		{
			Pawn = World.SpawnActor<APawn>();
			AActor* Prop = World.SpawnActor<AActor>();
			if (!Test.TestNotNull(*FString::Printf(TEXT("pawn %s"), Name), Pawn) || !Test.TestNotNull(TEXT("prop"), Prop))
			{
				return false;
			}

			Interactor = NewObject<UVBInteractorComponent>(Pawn);
			Interactor->RegisterComponent();
			Devices = NewObject<UVBInputDeviceManager>(Pawn);
			Devices->RegisterComponent();
			Item = NewObject<UVBInteractableComponent>(Prop);
			Item->RegisterComponent();
			return true;
		}
	};
}

// 상호작용 프롬프트가 스스로 묶는가.
// 못 보는 것: 키 이름의 글(시험 폰은 AVBCharacter 가 아니라 상호작용 액션이 없다), 화면(PIE 몫).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractPromptBindingTest, "VB.Unit.HUD.InteractPromptBinding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBInteractPromptBindingTest::RunTest(const FString& Parameters)
{
	using Access = FVBHUDRowTestAccess;
	FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		Fixture.Shutdown();
		return false;
	}

	FPromptPawn A;
	FPromptPawn B;
	UVBInteractPromptWidget* Prompt = nullptr;
	if (!A.Spawn(*this, *Fixture.World, TEXT("A")) || !B.Spawn(*this, *Fixture.World, TEXT("B"))
		|| !(Prompt = Fixture.MakeRow<UVBInteractPromptWidget>(*this, TEXT("/Game/VB/UI/WBP_InteractPrompt.WBP_InteractPrompt_C"))))
	{
		Fixture.Shutdown();
		return false;
	}

	Fixture.Controller->Possess(A.Pawn);
	// 구성 전에 이미 잡힌 초점 - 같은 초점은 다시 방송되지 않으므로 구성 때 바로 받아야 한다.
	Access::SetFocus(*A.Interactor, A.Item);

	FSlateLife Life;
	Life.Construct(*Prompt);
	TestEqual(TEXT("prompt binds pawn A's focus once"), CountBound(A.Interactor->OnFocusChanged, Prompt), 1);
	TestEqual(TEXT("prompt binds pawn A's devices once"), CountBound(A.Devices->OnInputDeviceChanged, Prompt), 1);
	TestTrue(TEXT("focus held before construct is shown"), Access::PromptTarget(*Prompt) == A.Item);
	TestTrue(TEXT("device type is read on bind"), Access::PromptInputType(*Prompt) == A.Devices->GetCurrentDeviceType());
	A.Devices->OnInputDeviceChanged.Broadcast(ECommonInputType::Gamepad);
	TestTrue(TEXT("device change reaches the prompt"), Access::PromptInputType(*Prompt) == ECommonInputType::Gamepad);

	// 같은 폰 신호가 또 와도 구독은 하나.
	Access::BindPawn(*Prompt, A.Pawn);
	TestEqual(TEXT("same pawn twice keeps one focus binding"), CountBound(A.Interactor->OnFocusChanged, Prompt), 1);

	// 폰 교체 A -> B. 옛 대상, 장치가 남지 않는다.
	Fixture.Controller->UnPossess();
	TestEqual(TEXT("no pawn releases A's focus"), CountBound(A.Interactor->OnFocusChanged, Prompt), 0);
	TestTrue(TEXT("no pawn clears the target"), Access::PromptTarget(*Prompt) == nullptr);
	TestFalse(TEXT("no pawn hides the prompt"), Prompt->IsVisible());
	Fixture.Controller->Possess(B.Pawn);
	TestEqual(TEXT("A's devices released"), CountBound(A.Devices->OnInputDeviceChanged, Prompt), 0);
	TestEqual(TEXT("B's focus bound once"), CountBound(B.Interactor->OnFocusChanged, Prompt), 1);
	TestEqual(TEXT("B's devices bound once"), CountBound(B.Devices->OnInputDeviceChanged, Prompt), 1);
	B.Devices->OnInputDeviceChanged.Broadcast(ECommonInputType::Touch);
	TestTrue(TEXT("B's device change reaches the prompt"), Access::PromptInputType(*Prompt) == ECommonInputType::Touch);
	TestTrue(TEXT("B has no focus yet"), Access::PromptTarget(*Prompt) == nullptr);
	Access::SetFocus(*B.Interactor, B.Item);
	TestTrue(TEXT("B's focus change reaches the prompt"), Access::PromptTarget(*Prompt) == B.Item);

	// 해체, 재구성: 풀었다가 다시 정확히 한 번, 지금 초점을 다시 받는다.
	Life.Destruct(*Prompt);
	TestEqual(TEXT("destruct releases B's focus"), CountBound(B.Interactor->OnFocusChanged, Prompt), 0);
	TestEqual(TEXT("destruct releases the pawn signal"), CountBound(Fixture.Controller->OnPossessedPawnChanged, Prompt), 0);
	Life.Construct(*Prompt);
	TestEqual(TEXT("reconstruct binds B's focus exactly once"), CountBound(B.Interactor->OnFocusChanged, Prompt), 1);
	TestEqual(TEXT("reconstruct binds the pawn signal once"), CountBound(Fixture.Controller->OnPossessedPawnChanged, Prompt), 1);
	TestTrue(TEXT("reconstruct seeds the current focus"), Access::PromptTarget(*Prompt) == B.Item);

	Life.Destruct(*Prompt);
	Fixture.Shutdown();
	return true;
}

// 월드 표시기 층: 갈래를 한 번만 만들고, 자리를 잇고, 매 틱 갈래를 움직인다.
// 시험 PC 는 뷰포트가 없어 엔진 투영이 실패한다 - 위협(EdgeOnly)은 그래서 "화면 밖" 으로 가장자리에 뜬다. 그 성질로 연결을 본다.
// 못 보는 것: 화면 안 자리(투영이 되는 뷰포트), 실제 겹침 순서(PIE 몫).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWorldIndicatorLayerTest, "VB.Unit.HUD.WorldIndicatorLayer",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBWorldIndicatorLayerTest::RunTest(const FString& Parameters)
{
	using Access = FVBHUDRowTestAccess;
	FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		Fixture.Shutdown();
		return false;
	}

	FPromptPawn A;
	UVBWorldIndicatorLayerWidget* Layer = nullptr;
	if (!A.Spawn(*this, *Fixture.World, TEXT("A"))
		|| !(Layer = Fixture.MakeRow<UVBWorldIndicatorLayerWidget>(*this,
			TEXT("/Game/VB/UI/WBP_WorldIndicatorLayer.WBP_WorldIndicatorLayer_C"))))
	{
		Fixture.Shutdown();
		return false;
	}

	Fixture.Controller->Possess(A.Pawn);

	FSlateLife Life;
	for (int32 Round = 0; Round < 2; ++Round)
	{
		const FString When = Round == 0 ? TEXT("first construct") : TEXT("reconstruct");
		Life.Construct(*Layer);
		const TArray<TObjectPtr<UVBWorldIndicatorWidget>>& Indicators = Access::Indicators(*Layer);
		const UCanvasPanel* Canvas = Access::IndicatorCanvas(*Layer);
		// 여섯 갈래 - 등록 표시기(처형 키 표시 등)가 프롬프트 바로 아래, 프롬프트가 맨 위다.
		TestEqual(*(When + TEXT(": six indicators")), Indicators.Num(), 6);
		TestEqual(*(When + TEXT(": canvas holds only them")), Canvas ? Canvas->GetChildrenCount() : -1, 6);
		TestEqual(*(When + TEXT(": one prompt focus binding")),
			CountBound(A.Interactor->OnFocusChanged, Indicators.Num() > 5 ? Indicators[5].Get() : nullptr), 1);
		if (Indicators.Num() == 6)
		{
			TestTrue(*(When + TEXT(": objective layer first")), Indicators[0]->IsA<UVBObjectiveMarkerLayerWidget>());
			TestTrue(*(When + TEXT(": threat second")), Indicators[1]->IsA<UVBOffscreenThreatWidget>());
			TestTrue(*(When + TEXT(": nearby enemy bars third")), Indicators[2]->IsA<UVBNearbyEnemyBarsWidget>());
			TestTrue(*(When + TEXT(": lock-on health bar under the prompt")), Indicators[3]->IsA<UVBTargetHealthIndicatorWidget>());
			TestTrue(*(When + TEXT(": registered indicators under the prompt")), Indicators[4]->IsA<UVBRegisteredIndicatorsWidget>());
			TestTrue(*(When + TEXT(": prompt on top")), Indicators[5]->IsA<UVBInteractPromptWidget>());
			for (int32 Index = 0; Index < 6; ++Index)
			{
				TestTrue(*FString::Printf(TEXT("%s: indicator %d sits on the layer canvas"), *When, Index),
					Indicators[Index]->Slot && Indicators[Index]->Slot->Parent == Canvas);
			}

			if (const UCanvasPanelSlot* LayerSlot = Cast<UCanvasPanelSlot>(Indicators[0]->Slot))
			{
				const FAnchors Full = LayerSlot->GetAnchors();
				TestTrue(*(When + TEXT(": objective layer stretches over the layer")),
					Full.Minimum.IsZero() && Full.Maximum.Equals(FVector2D(1.0,
						1.0)) && LayerSlot->GetOffsets().Left == 0.0f && LayerSlot->GetOffsets().Right == 0.0f);
			}

			if (Round == 0)
			{
				// 첫 갱신 전 보임 - HUD 에 있을 때와 같다(화살표, 프롬프트 숨김, 마커 층은 깔림).
				TestFalse(TEXT("threat arrow starts hidden"), Indicators[1]->IsVisible());
				TestTrue(TEXT("nearby enemy bars layer starts laid down"),
					Indicators[2]->GetVisibility() == ESlateVisibility::HitTestInvisible);
				TestFalse(TEXT("lock-on health bar starts hidden"), Indicators[3]->IsVisible());
				TestTrue(TEXT("registered indicators layer starts laid down"),
					Indicators[4]->GetVisibility() == ESlateVisibility::HitTestInvisible);
				TestFalse(TEXT("prompt starts hidden"), Indicators[5]->IsVisible());
				TestTrue(TEXT("objective layer starts laid down"), Indicators[0]->GetVisibility() == ESlateVisibility::HitTestInvisible);
			}
		}

		if (Round == 0)
		{
			Life.Destruct(*Layer);
		}
	}

	// 갱신 연결: 처음 숨은 위협 화살표가 층의 틱 한 번으로 가장자리에 뜬다.
	const TArray<TObjectPtr<UVBWorldIndicatorWidget>>& Indicators = Access::Indicators(*Layer);
	UVBThreatTrackerSubsystem* Tracker = Fixture.World->GetSubsystem<UVBThreatTrackerSubsystem>();
	AActor* Enemy = Fixture.World->SpawnActor<AActor>();
	if (TestEqual(TEXT("six indicators before the tick check"), Indicators.Num(), 6)
		&& TestNotNull(TEXT("threat tracker"), Tracker) && TestNotNull(TEXT("enemy"), Enemy))
	{
		UVBWorldIndicatorWidget* Threat = Indicators[1];
		Threat->SetVisibility(ESlateVisibility::Collapsed);
		Tracker->Register(Enemy, FGameplayTag(), FLinearColor::Red);
		const FVector2D Size(1920.0, 1080.0);
		Access::Tick(*Layer, Size);
		TestTrue(TEXT("layer tick shows the collapsed threat arrow"), Threat->IsVisible());
		const UCanvasPanelSlot* ThreatSlot = Cast<UCanvasPanelSlot>(Threat->Slot);
		const FVector2D Position = ThreatSlot ? ThreatSlot->GetPosition() : FVector2D::ZeroVector;
		const bool bOnInset = FMath::IsNearlyEqual(Position.X, 80.0, 1e-2) || FMath::IsNearlyEqual(Position.X, 1920.0 - 80.0, 1e-2)
			|| FMath::IsNearlyEqual(Position.Y, 80.0, 1e-2) || FMath::IsNearlyEqual(Position.Y, 1080.0 - 80.0, 1e-2);
		TestTrue(TEXT("threat arrow sits on the inset edge"), bOnInset);
		Tracker->Unregister(Enemy);
		Access::Tick(*Layer, Size);
		TestFalse(TEXT("no threat hides the arrow again"), Threat->IsVisible());
	}

	Life.Destruct(*Layer);
	TestEqual(TEXT("destruct releases the prompt"),
		CountBound(A.Interactor->OnFocusChanged, Indicators.Num() > 5 ? Indicators[5].Get() : nullptr), 0);
	Fixture.Shutdown();
	return true;
}

#endif
