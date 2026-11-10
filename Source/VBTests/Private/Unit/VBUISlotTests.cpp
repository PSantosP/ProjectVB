// ProjectVB Copyright (c) 2025. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Data/VBHUDLayoutData.h"
#include "GameFramework/Actor.h"
#include "UI/Progress/VBTitleCardWidget.h"
#include "UI/Slots/VBUISlotSubsystem.h"
#include "UI/Slots/VBUISlotWidget.h"
#include "UObject/Package.h"
#include "VBUISlotTestWidgets.h"
#include "VBUITestFixture.h"
#include "Widgets/Layout/SConstraintCanvas.h"

// UI 슬롯. 시험 월드는 VBUITestFixture(실제 로컬 플레이어 문맥).
// 못 보는 것: HUD 가 배치 데이터로 자리를 만드는 실제 길(HUD 초기화는 AVBCharacter 폰이 필요하다 - 자산 값은 아래 HUDLayout 시험이 본다),
//   화면 겹침과 입력(PIE 몫).
class FVBUISlotTestAccess
{
public:
	static int32 NextId(const UVBUISlotSubsystem& Registry) { return Registry.NextId; }
	static const TMap<int32, TObjectPtr<UUserWidget>>& EntryWidgets(const UVBUISlotWidget& SlotWidget) { return SlotWidget.EntryWidgets; }
	static UCanvasPanel* Canvas(const UVBUISlotWidget& SlotWidget) { return SlotWidget.SlotCanvas; }
	static int32 EntryCount(const UVBUISlotSubsystem& Registry, const FGameplayTag& Tag)
	{
		return Registry.Entries.FilterByPredicate([&Tag](const FVBUISlotEntry& Entry) { return Entry.Slot == Tag; }).Num();
	}
};

namespace
{
	using VBUITest::FRowFixture;
	using VBUITest::FSlateLife;

	UVBUISlotWidget* MakeSlot(FRowFixture& Fixture, const FGameplayTag& Tag, const UObject* Owner)
	{
		UVBUISlotWidget* SlotWidget = CreateWidget<UVBUISlotWidget>(Fixture.Controller, UVBUISlotWidget::StaticClass());
		if (SlotWidget)
		{
			SlotWidget->SetSlot(Tag, Owner);
		}

		return SlotWidget;
	}

	FVBUISlotRequest MakeRequest(const FGameplayTag& Tag, TSubclassOf<UUserWidget> Class, int32 Order, const UObject* Registrant,
		const UObject* Scope = nullptr)
	{
		FVBUISlotRequest Request;
		Request.Slot = Tag;
		Request.WidgetClass = Class;
		Request.Order = Order;
		Request.Registrant = Registrant;
		Request.Scope = Scope;
		return Request;
	}

	UVBUISlotTestEntry* EntryWidget(const UVBUISlotWidget& SlotWidget, int32 Id)
	{
		return Cast<UVBUISlotTestEntry>(FVBUISlotTestAccess::EntryWidgets(SlotWidget).FindRef(Id));
	}

	// 살아 있는 Slate 캔버스 슬롯의 ZOrder - UMG 배열이 아니라 실제로 그리는 쪽을 본다.
	int32 LiveZOrder(const UVBUISlotWidget& SlotWidget, const UUserWidget& Entry)
	{
		const UCanvasPanel* Canvas = FVBUISlotTestAccess::Canvas(SlotWidget);
		const TSharedPtr<SConstraintCanvas> Live = Canvas ? Canvas->GetCanvasWidget() : nullptr;
		const TSharedPtr<SWidget> EntrySlate = Entry.GetCachedWidget();
		if (!Live.IsValid() || !EntrySlate.IsValid())
		{
			return MIN_int32;
		}

		FChildren* Children = Live->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (Children->GetChildAt(Index) == EntrySlate)
			{
				return static_cast<const SConstraintCanvas::FSlot&>(Children->GetSlotAt(Index)).GetZOrder();
			}
		}

		return MIN_int32;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUISlotRegistryTest, "VB.Unit.UISlot.Registry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBUISlotRegistryTest::RunTest(const FString& Parameters)
{
	FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		Fixture.Shutdown();
		return false;
	}

	UVBUISlotSubsystem* Registry = UVBUISlotSubsystem::Get(Fixture.Controller);
	const FGameplayTag TagX = VBGameplayTags::UI_Slot_HUD_QuestTracker;
	const FGameplayTag TagY = VBGameplayTags::UI_Slot_HUD_Subtitle;
	UObject* OwnerA = Fixture.World->SpawnActor<AActor>();
	UObject* OwnerB = Fixture.World->SpawnActor<AActor>();
	UObject* Registrant = Fixture.World->SpawnActor<AActor>();
	UVBUISlotWidget* SlotA = MakeSlot(Fixture, TagX, OwnerA);
	if (!TestNotNull(TEXT("slot registry on the local player"), Registry) || !TestNotNull(TEXT("slot A"), SlotA))
	{
		Fixture.Shutdown();
		return false;
	}

	FSlateLife LifeA;
	LifeA.Construct(*SlotA);

	// 등록 -> 자리에 하나.
	FVBUISlotHandle High = Registry->RegisterWidget(MakeRequest(TagX, UVBUISlotTestEntry::StaticClass(), 10, Registrant));
	TestTrue(TEXT("registration is valid"), High.IsValid());
	UVBUISlotTestEntry* HighWidget = EntryWidget(*SlotA, High.GetId());
	if (!TestNotNull(TEXT("slot created the entry"), HighWidget))
	{
		Fixture.Shutdown();
		return false;
	}

	TestEqual(TEXT("entry constructed once"), HighWidget->Constructs, 1);

	// 낮은 Order 를 나중에 등록 -> 살아 있는 Slate 에서 아래, 먼저 있던 항목은 떼었다 붙이지 않는다.
	FVBUISlotHandle Low = Registry->RegisterWidget(MakeRequest(TagX, UVBUISlotTestOther::StaticClass(), 5, Registrant));
	UVBUISlotTestEntry* LowWidget = EntryWidget(*SlotA, Low.GetId());
	TestEqual(TEXT("two entries"), FVBUISlotTestAccess::EntryWidgets(*SlotA).Num(), 2);
	TestEqual(TEXT("live z of the high entry"), LiveZOrder(*SlotA, *HighWidget), 10);
	TestEqual(TEXT("live z of the low entry"), LowWidget ? LiveZOrder(*SlotA, *LowWidget) : MIN_int32, 5);
	TestEqual(TEXT("an unrelated registration does not rebuild the existing entry"), HighWidget->Constructs, 1);
	TestEqual(TEXT("and does not destruct it"), HighWidget->Destructs, 0);

	// 같은 등록자, 태그, 클래스 두 번째 = 거절, 빈 태그 = 거절.
	AddExpectedMessagePlain(TEXT("두 번째 등록을 거절한다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("비어 등록하지 않는다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("duplicate registration is refused"),
		Registry->RegisterWidget(MakeRequest(TagX, UVBUISlotTestEntry::StaticClass(), 0, Registrant)).IsValid());
	TestFalse(TEXT("empty tag is refused"),
		Registry->RegisterWidget(MakeRequest(FGameplayTag(), UVBUISlotTestEntry::StaticClass(), 0, Registrant)).IsValid());

	// 해제 -> 그 항목만 떼고 해체.
	High.Unregister();
	TestEqual(TEXT("one entry left"), FVBUISlotTestAccess::EntryWidgets(*SlotA).Num(), 1);
	TestEqual(TEXT("unregistered entry destructed"), HighWidget->Destructs, 1);
	TestFalse(TEXT("handle is cleared"), High.IsValid());

	// 등록 먼저, 자리 나중 -> 자리가 구성될 때 받는다.
	Registry->RegisterWidget(MakeRequest(TagY, UVBUISlotTestEntry::StaticClass(), 0, Registrant));
	UVBUISlotWidget* SlotY = MakeSlot(Fixture, TagY, OwnerA);
	FSlateLife LifeY;
	LifeY.Construct(*SlotY);
	TestEqual(TEXT("slot built after the registration still receives it"), FVBUISlotTestAccess::EntryWidgets(*SlotY).Num(), 1);

	// 범위: OwnerA 범위 항목은 OwnerA 의 자리만 받는다(옛 HUD 가 살아 있어도 새 HUD 의 자리는 안 받는다).
	UVBUISlotWidget* SlotB = MakeSlot(Fixture, TagX, OwnerB);
	FSlateLife LifeB;
	LifeB.Construct(*SlotB);
	TestEqual(TEXT("unscoped entry reaches every slot of the tag"), FVBUISlotTestAccess::EntryWidgets(*SlotB).Num(), 1);
	FVBUISlotHandle Scoped = Registry->RegisterWidget(MakeRequest(TagX, UVBUISlotTestEntry::StaticClass(), 0, OwnerA, OwnerA));
	TestEqual(TEXT("scoped entry reaches its owner's slot"), FVBUISlotTestAccess::EntryWidgets(*SlotA).Num(), 2);
	TestEqual(TEXT("scoped entry skips another owner's slot"), FVBUISlotTestAccess::EntryWidgets(*SlotB).Num(), 1);

	// 같은 자리의 Slate 재구성 -> 같은 항목 UObject 를 다시 붙인다.
	UUserWidget* Before = FVBUISlotTestAccess::EntryWidgets(*SlotA).FindRef(Scoped.GetId());
	LifeA.Destruct(*SlotA);
	TestFalse(TEXT("destructed slot leaves the registry"), Registry->HasSlot(*SlotA));
	LifeA.Construct(*SlotA);
	TestEqual(TEXT("reconstructed slot keeps its entries"), FVBUISlotTestAccess::EntryWidgets(*SlotA).Num(), 2);
	TestTrue(TEXT("reconstructed slot reuses the same entry object"),
		FVBUISlotTestAccess::EntryWidgets(*SlotA).FindRef(Scoped.GetId()) == Before);

	// 등록자가 사라지면(손잡이를 못 푼 채) 다음 변경에서 걷힌다.
	AActor* Doomed = Fixture.World->SpawnActor<AActor>();
	Registry->RegisterWidget(MakeRequest(TagY, UVBUISlotTestOther::StaticClass(), 0, Doomed));
	TestEqual(TEXT("orphan entry shown"), FVBUISlotTestAccess::EntryWidgets(*SlotY).Num(), 2);
	Doomed->Destroy();
	Registry->RegisterWidget(MakeRequest(TagX, UVBUISlotTestOther::StaticClass(), 0, OwnerB));
	TestEqual(TEXT("orphan entry pruned on the next change"), FVBUISlotTestAccess::EntryWidgets(*SlotY).Num(), 1);

	LifeA.Destruct(*SlotA);
	LifeB.Destruct(*SlotB);
	LifeY.Destruct(*SlotY);
	Fixture.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUISlotVisibilityAndScopeTest, "VB.Unit.UISlot.VisibilityAndScope",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBUISlotVisibilityAndScopeTest::RunTest(const FString& Parameters)
{
	FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		Fixture.Shutdown();
		return false;
	}

	UVBUISlotSubsystem* Registry = UVBUISlotSubsystem::Get(Fixture.Controller);
	const FGameplayTag Tag = VBGameplayTags::UI_Slot_HUD_ChapterCard;
	AActor* Owner = Fixture.World->SpawnActor<AActor>();
	UVBUISlotWidget* SlotWidget = MakeSlot(Fixture, Tag, Owner);
	if (!TestNotNull(TEXT("registry"), Registry) || !TestNotNull(TEXT("slot"), SlotWidget))
	{
		Fixture.Shutdown();
		return false;
	}

	FSlateLife Life;
	Life.Construct(*SlotWidget);

	// 처음 보임: 새로 만든 항목에 붙인 뒤 한 번. 그 뒤 항목이 스스로 바꾼 보임을 무관한 변경이 덮지 않는다.
	FVBUISlotRequest Request = MakeRequest(Tag, UVBUISlotTestEntry::StaticClass(), 0, Owner, Owner);
	Request.InitialVisibility = ESlateVisibility::HitTestInvisible;
	const FVBUISlotHandle Shown = Registry->RegisterWidget(Request);
	UVBUISlotTestEntry* Entry = EntryWidget(*SlotWidget, Shown.GetId());
	if (!TestNotNull(TEXT("entry"), Entry))
	{
		Fixture.Shutdown();
		return false;
	}

	TestTrue(TEXT("initial visibility applied on creation"), Entry->GetVisibility() == ESlateVisibility::HitTestInvisible);
	Entry->SetVisibility(ESlateVisibility::Collapsed);
	Registry->RegisterWidget(MakeRequest(Tag, UVBUISlotTestOther::StaticClass(), 1, Owner, Owner));
	TestTrue(TEXT("an unrelated registration does not reapply the initial visibility"),
		Entry->GetVisibility() == ESlateVisibility::Collapsed);
	// 재구성도 다시 쓰지 않는다.
	Life.Destruct(*SlotWidget);
	Life.Construct(*SlotWidget);
	TestTrue(TEXT("reconstruct does not reapply it either"), Entry->GetVisibility() == ESlateVisibility::Collapsed);

	// 범위 주인이 사라지면 그 범위의 항목은 다음 변경에서 걷힌다.
	AActor* ScopeOwner = Fixture.World->SpawnActor<AActor>();
	UVBUISlotWidget* Unscoped = MakeSlot(Fixture, VBGameplayTags::UI_Slot_HUD_RegionBanner, nullptr);
	FSlateLife LifeU;
	LifeU.Construct(*Unscoped);
	Registry->RegisterWidget(MakeRequest(VBGameplayTags::UI_Slot_HUD_RegionBanner, UVBUISlotTestEntry::StaticClass(), 0, nullptr,
		ScopeOwner));
	TestEqual(TEXT("scoped entry is not shown to a slot with another owner"), FVBUISlotTestAccess::EntryWidgets(*Unscoped).Num(), 0);
	const int32 Before = Registry->GetEntriesFor(*SlotWidget).Num();
	ScopeOwner->Destroy();
	Registry->RegisterWidget(MakeRequest(VBGameplayTags::UI_Slot_HUD_RegionBanner, UVBUISlotTestOther::StaticClass(), 0, Owner));
	TestEqual(TEXT("dead scope entry is pruned from the registry"),
		FVBUISlotTestAccess::EntryCount(*Registry, VBGameplayTags::UI_Slot_HUD_RegionBanner), 1);
	TestEqual(TEXT("other slots are untouched"), Registry->GetEntriesFor(*SlotWidget).Num(), Before);

	Life.Destruct(*SlotWidget);
	LifeU.Destruct(*Unscoped);
	Fixture.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUISlotReentrancyTest, "VB.Unit.UISlot.Reentrancy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBUISlotReentrancyTest::RunTest(const FString& Parameters)
{
	FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		Fixture.Shutdown();
		return false;
	}

	UVBUISlotSubsystem* Registry = UVBUISlotSubsystem::Get(Fixture.Controller);
	const FGameplayTag Tag = VBGameplayTags::UI_Slot_HUD_TutorialHint;
	UObject* Registrant = Fixture.World->SpawnActor<AActor>();
	UVBUISlotWidget* SlotWidget = MakeSlot(Fixture, Tag, Registrant);
	if (!TestNotNull(TEXT("registry"), Registry) || !TestNotNull(TEXT("slot"), SlotWidget))
	{
		Fixture.Shutdown();
		return false;
	}

	FSlateLife Life;
	Life.Construct(*SlotWidget);
	// 시험용 정적 칸은 다른 시험이 남긴 값이 있을 수 있다 - 처음에 비우고 끝에 다시 비운다.
	UVBUISlotTestEntry::PendingAction = EVBUISlotTestAction::None;

	// 항목의 구성이 같은 슬롯에 다른 항목을 등록한다 -> 둘 다, 각각 한 번씩만 만든다.
	UVBUISlotTestEntry::PendingRegistry = Registry;
	UVBUISlotTestEntry::PendingSlot = Tag;
	UVBUISlotTestEntry::PendingAction = EVBUISlotTestAction::RegisterOther;
	Registry->RegisterWidget(MakeRequest(Tag, UVBUISlotTestEntry::StaticClass(), 0, Registrant));
	const TMap<int32, TObjectPtr<UUserWidget>>& Widgets = FVBUISlotTestAccess::EntryWidgets(*SlotWidget);
	TestEqual(TEXT("nested registration ends with both entries"), Widgets.Num(), 2);
	int32 TotalConstructs = 0;
	for (const TPair<int32, TObjectPtr<UUserWidget>>& Pair : Widgets)
	{
		if (const UVBUISlotTestEntry* Entry = Cast<UVBUISlotTestEntry>(Pair.Value))
		{
			TotalConstructs += Entry->Constructs;
		}
	}

	TestEqual(TEXT("each entry constructed exactly once"), TotalConstructs, 2);

	// 항목의 구성이 자기 등록을 푼다 -> 끝에는 그 항목이 없다(오래된 목록이 다시 붙이지 않는다).
	UVBUISlotTestEntry::PendingAction = EVBUISlotTestAction::UnregisterId;
	UVBUISlotTestEntry::PendingId = FVBUISlotTestAccess::NextId(*Registry);
	const FVBUISlotHandle SelfRemoving = Registry->RegisterWidget(MakeRequest(Tag, UVBUISlotTestOther::StaticClass(), 3,
		Fixture.World->SpawnActor<AActor>()));
	TestFalse(TEXT("self-removing entry is gone from the slot"), Widgets.Contains(SelfRemoving.GetId()));
	TestEqual(TEXT("slot still matches the registry"), Widgets.Num(), Registry->GetEntriesFor(*SlotWidget).Num());

	UVBUISlotTestEntry::PendingRegistry.Reset();
	Life.Destruct(*SlotWidget);
	Fixture.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUISlotDesiredSizeTest, "VB.Unit.UISlot.DesiredSize",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBUISlotDesiredSizeTest::RunTest(const FString& Parameters)
{
	FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		Fixture.Shutdown();
		return false;
	}

	UVBUISlotSubsystem* Registry = UVBUISlotSubsystem::Get(Fixture.Controller);
	UObject* Registrant = Fixture.World->SpawnActor<AActor>();
	UVBUISlotWidget* SlotWidget = MakeSlot(Fixture, VBGameplayTags::UI_Slot_HUD_QuickSlots, Registrant);
	if (!TestNotNull(TEXT("registry"), Registry) || !TestNotNull(TEXT("slot"), SlotWidget))
	{
		Fixture.Shutdown();
		return false;
	}

	FSlateLife Life;
	Life.Construct(*SlotWidget);
	Registry->RegisterWidget(MakeRequest(VBGameplayTags::UI_Slot_HUD_QuickSlots, UVBUISlotTestEntry::StaticClass(), 0, Registrant));
	// 자리는 AutoSize 점 앵커로 놓이므로 "자리의 원하는 크기 = 항목의 원하는 크기" 가 "캔버스 직접 자식" 과 같은 자리를 만든다.
	// 시험 모듈은 SlateCore 에 직접 묶이지 않는다 - UMG 의 같은 길(UWidget::ForceLayoutPrepass / GetDesiredSize)로 잰다.
	SlotWidget->ForceLayoutPrepass();
	TestTrue(TEXT("slot desires exactly its single entry's size"), SlotWidget->GetDesiredSize().Equals(FVector2D(200.0, 50.0), 0.5));
	TestTrue(TEXT("slot itself does not eat clicks"), SlotWidget->GetVisibility() == ESlateVisibility::SelfHitTestInvisible);
	Life.Destruct(*SlotWidget);
	Fixture.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUISlotEntryOwnerTest, "VB.Unit.UISlot.EntryOwner",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBUISlotEntryOwnerTest::RunTest(const FString& Parameters)
{
	// 항목은 PC 를 Outer 로 만들어진다 - 자리 주인은 부모 패널(SlotCanvas)을 따라가야 보인다(설계 3-1d2 3.3).
	FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		Fixture.Shutdown();
		return false;
	}

	UVBUISlotSubsystem* Registry = UVBUISlotSubsystem::Get(Fixture.Controller);
	UObject* Owner = Fixture.World->SpawnActor<AActor>();
	UVBUISlotWidget* SlotWidget = MakeSlot(Fixture, VBGameplayTags::UI_Slot_HUD_QuickSlots, Owner);
	UVBUISlotTestEntry* Loose = CreateWidget<UVBUISlotTestEntry>(Fixture.Controller, UVBUISlotTestEntry::StaticClass());
	if (!TestNotNull(TEXT("registry"), Registry) || !TestNotNull(TEXT("slot"), SlotWidget) || !TestNotNull(TEXT("loose widget"), Loose))
	{
		Fixture.Shutdown();
		return false;
	}

	FSlateLife Life;
	Life.Construct(*SlotWidget);
	const FVBUISlotHandle Handle = Registry->RegisterWidget(MakeRequest(VBGameplayTags::UI_Slot_HUD_QuickSlots,
		UVBUISlotTestEntry::StaticClass(), 0, Owner));
	UVBUISlotTestEntry* Entry = EntryWidget(*SlotWidget, Handle.GetId());
	if (TestNotNull(TEXT("slot created the entry"), Entry))
	{
		TestTrue(TEXT("entry finds the owner of its slot"), UVBUISlotWidget::FindSlotOwnerOf(*Entry) == Owner);
		TestTrue(TEXT("entry outer is the controller, so a typed outer would not find the slot"),
			Entry->GetTypedOuter<UVBUISlotWidget>() == nullptr);
	}

	TestTrue(TEXT("a widget outside any slot has no slot owner"), UVBUISlotWidget::FindSlotOwnerOf(*Loose) == nullptr);
	Life.Destruct(*SlotWidget);
	Fixture.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHUDLayoutDataTest, "VB.Unit.UISlot.HUDLayout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBHUDLayoutDataTest::RunTest(const FString& Parameters)
{
	// 저작 검사: 중복 태그, 자리 없는 위젯, 필수 빈 클래스.
	UVBHUDLayoutData* Bad = NewObject<UVBHUDLayoutData>(GetTransientPackage());
	FVBUISlotPlacement Placement;
	Placement.Slot = VBGameplayTags::UI_Slot_HUD_Subtitle;
	Bad->Slots.Add(Placement);
	Bad->Slots.Add(Placement);
	FVBUISlotDefaultWidget Row;
	Row.Slot = VBGameplayTags::UI_Slot_HUD_QuestTracker;
	Bad->Widgets.Add(Row);
	TArray<FString> Problems;
	Bad->CollectProblems(Problems);
	TestEqual(TEXT("duplicate slot, missing slot and empty required class are all reported"), Problems.Num(), 3);

	// 실제 자산: 옛 HUD 의 자리, 순서, 구간과 같고, 제목 카드 두 WBP 의 역할이 맞다.
	const UVBHUDLayoutData* Layout = LoadObject<UVBHUDLayoutData>(nullptr, TEXT("/Game/VB/UI/DA_HUDLayout.DA_HUDLayout"));
	if (!TestNotNull(TEXT("DA_HUDLayout"), Layout))
	{
		return false;
	}

	TArray<FString> AssetProblems;
	Layout->CollectProblems(AssetProblems);
	TestEqual(TEXT("asset has no authoring problems"), AssetProblems.Num(), 0);
	// 터치 조작이 맨 앞: 옛 HUD 가 다른 줄보다 먼저 붙였다(같은 Z0 구간에서 가장 아래, 설계 3-1d2 3.4).
	const FGameplayTag Expected[] = {
		VBGameplayTags::UI_Slot_HUD_TouchControls,
		VBGameplayTags::UI_Slot_HUD_WeaponWheel, VBGameplayTags::UI_Slot_HUD_QuestTracker, VBGameplayTags::UI_Slot_HUD_Progression,
		VBGameplayTags::UI_Slot_HUD_QuickSlots, VBGameplayTags::UI_Slot_HUD_ChapterCard, VBGameplayTags::UI_Slot_HUD_RegionBanner,
		VBGameplayTags::UI_Slot_HUD_TutorialHint, VBGameplayTags::UI_Slot_HUD_WorldIndicators, VBGameplayTags::UI_Slot_HUD_Subtitle,
		// 보스 바 - 맨 끝에 붙였다.
		VBGameplayTags::UI_Slot_HUD_BossBar,
		// 미니맵 - 맨 끝에 붙였다.
		VBGameplayTags::UI_Slot_HUD_Minimap,
	};
	constexpr int32 ExpectedCount = UE_ARRAY_COUNT(Expected);
	if (TestEqual(TEXT("twelve slots"), Layout->Slots.Num(), ExpectedCount))
	{
		for (int32 Index = 0; Index < ExpectedCount; ++Index)
		{
			TestTrue(*FString::Printf(TEXT("slot %d is %s (old attach order)"), Index, *Expected[Index].ToString()),
				Layout->Slots[Index].Slot == Expected[Index]);
		}

		TestTrue(TEXT("touch controls fill the screen"), Layout->Slots[0].IsFullScreen());
		TestEqual(TEXT("touch controls share the row band"), Layout->Slots[0].ZOrder, 0);
		TestEqual(TEXT("weapon wheel stays on top"), Layout->Slots[1].ZOrder, 100);
		TestTrue(TEXT("weapon wheel is 480 fixed"), !Layout->Slots[1].bAutoSize && Layout->Slots[1].Size.Equals(FVector2D(480.0, 480.0)));
		TestEqual(TEXT("world indicators stay at the bottom (user decision)"), Layout->Slots[8].ZOrder, -1);
		TestTrue(TEXT("world indicators fill the screen"), Layout->Slots[8].IsFullScreen());
		TestTrue(TEXT("quest tracker top right -40,40"), Layout->Slots[2].Position.Equals(FVector2D(-40.0, 40.0))
			&& Layout->Slots[2].Anchors.Minimum.Equals(FVector2D(1.0, 0.0)));
		// 퀵슬롯은 왼쪽 아래, 미니맵은 오른쪽 아래 220 고정.
		TestTrue(TEXT("quick slots bottom left 40,-40"), Layout->Slots[4].Position.Equals(FVector2D(40.0, -40.0))
			&& Layout->Slots[4].Anchors.Minimum.Equals(FVector2D(0.0, 1.0)) && Layout->Slots[4].Alignment.Equals(FVector2D(0.0, 1.0)));
		TestTrue(TEXT("minimap bottom right -40,-40"), Layout->Slots[11].Position.Equals(FVector2D(-40.0, -40.0))
			&& Layout->Slots[11].Anchors.Minimum.Equals(FVector2D(1.0, 1.0)) && Layout->Slots[11].Alignment.Equals(FVector2D(1.0, 1.0)));
		TestTrue(TEXT("minimap is 220 fixed"), !Layout->Slots[11].bAutoSize && Layout->Slots[11].Size.Equals(FVector2D(220.0, 220.0)));
		// 아래 가운데 쌓임: 보스 바(-140, 높이 35) -> 자막(-190).
		//   퀵슬롯(-40, 칸 80)이 비키기 전에 고른 자리다 - 사용자가 따로 안 물어 그대로 둔다.
		TestTrue(TEXT("subtitle bottom centre -190 (above the boss bar)"), Layout->Slots[9].Position.Equals(FVector2D(0.0, -190.0)));
		TestTrue(TEXT("boss bar bottom centre -140 (between quick slots and subtitles)"),
			Layout->Slots[10].Position.Equals(FVector2D(0.0, -140.0))
			&& Layout->Slots[10].Anchors.Minimum.Equals(FVector2D(0.5, 1.0)) && Layout->Slots[10].Alignment.Equals(FVector2D(0.5, 1.0)));
	}

	const FVBUISlotDefaultWidget* TouchRow = Layout->Widgets.FindByPredicate(
		[](const FVBUISlotDefaultWidget& Row) { return Row.Slot == VBGameplayTags::UI_Slot_HUD_TouchControls; });
	if (TestNotNull(TEXT("touch controls row"), TouchRow))
	{
		TestTrue(TEXT("touch controls row uses WBP_TouchControls"),
			TouchRow->WidgetClass && TouchRow->WidgetClass->GetPathName() == TEXT("/Game/VB/UI/Touch/WBP_TouchControls.WBP_TouchControls_C"));
		TestTrue(TEXT("touch controls are optional (test switch)"), TouchRow->bOptional);
		// 처음 보임을 자산이 정하면 자리가 구성 뒤 덮어써서 위젯이 정한 Menu 중 숨김을 뒤집는다.
		TestFalse(TEXT("touch controls decide their own first visibility"), TouchRow->bSetInitialVisibility);
	}

	const FVBUISlotDefaultWidget* MinimapRow = Layout->Widgets.FindByPredicate(
		[](const FVBUISlotDefaultWidget& Row) { return Row.Slot == VBGameplayTags::UI_Slot_HUD_Minimap; });
	// 미니맵은 숨는 조건(옵션, 지도 정의, 폰)을 스스로 정한다 - 자산이 처음 보임을 정하면 그 판단을 덮어쓴다.
	TestTrue(TEXT("minimap row uses WBP_Minimap and decides its own visibility"), MinimapRow && MinimapRow->WidgetClass
		&& MinimapRow->WidgetClass->GetPathName() == TEXT("/Game/VB/UI/Map/WBP_Minimap.WBP_Minimap_C") && !MinimapRow->bSetInitialVisibility);
	for (const FVBUISlotDefaultWidget& Default : Layout->Widgets)
	{
		const UVBTitleCardWidget* Card = Default.WidgetClass ? Cast<UVBTitleCardWidget>(Default.WidgetClass->GetDefaultObject()) : nullptr;
		if (Default.Slot == VBGameplayTags::UI_Slot_HUD_ChapterCard)
		{
			TestTrue(TEXT("chapter card row listens to chapters"), Card && Card->GetSource() == EVBTitleCardSource::Chapter);
		}
		else if (Default.Slot == VBGameplayTags::UI_Slot_HUD_RegionBanner)
		{
			TestTrue(TEXT("region banner row listens to regions"), Card && Card->GetSource() == EVBTitleCardSource::Region);
		}
	}

	return true;
}
#endif
