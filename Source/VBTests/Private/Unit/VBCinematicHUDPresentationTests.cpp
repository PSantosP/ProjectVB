// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/VBGameplayTags.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Data/VBHUDLayoutData.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"
#include "UI/VBPrimaryLayout.h"
#include "UI/VBUIManagerSubsystem.h"
#include "UI/Slots/VBHUDLayoutRuntime.h"
#include "UI/Slots/VBUISlotWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "VBCinematicDefinition.h"
#include "VBUISlotTestWidgets.h"
#include "VBUITestFixture.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

class FVBCinematicHUDTestAccess
{
public:
	static void Install(UVBUIManagerSubsystem& UI, UVBPrimaryLayout* Layout, APlayerController* PC)
	{
		UI.Layout = Layout;
		UI.LayoutOwner = PC;
	}
	static void Destroy(UVBUIManagerSubsystem& UI)
	{
		UI.DestroyLayout();
	}
	static int32 InputHolds(const UVBUIManagerSubsystem& UI)
	{
		return UI.SuspendedInputReasons.Num();
	}
	static void Bind(UVBCinematicDirectorSubsystem& Director, UVBUIManagerSubsystem& UI, FGuid Token)
	{
		Director.RequestId = FGuid::NewGuid();
		Director.HUDPresentationOwner = &UI;
		Director.HUDPresentationToken = Token;
		Director.ActiveDefinition = NewObject<UVBCinematicDefinition>(&Director);
	}
	static bool Holds(const UVBCinematicDirectorSubsystem& Director, FGuid Token)
	{
		return Director.HUDPresentationToken == Token && Director.RequestId.IsValid() && Director.ActiveDefinition != nullptr;
	}
	static void Close(UVBCinematicDirectorSubsystem& Director, UVBUIManagerSubsystem& UI, FGuid Token, EVBCinematicEndType Type)
	{
		Director.RequestId = FGuid::NewGuid();
		Director.SessionId = FGuid::NewGuid();
		Director.HUDPresentationOwner = &UI;
		Director.HUDPresentationToken = Token;
		Director.EndResult = FVBCinematicEndResult{Director.SessionId, Type, EVBCinematicEndReason::None};
		Director.CloseRequest(false);
	}
};

namespace
{
	constexpr auto HUDFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	struct FHUDPresentationFixture
	{
		VBUITest::FRowFixture Row;
		VBUITest::FSlateLife Slate;
		TStrongObjectPtr<UGameInstance> GI;
		TStrongObjectPtr<UGameViewportClient> Viewport;
		UVBUIManagerSubsystem* UI = nullptr;
		UVBPrimaryLayout* Layout = nullptr;
		UCanvasPanel* Caption = nullptr;
		bool Init(FAutomationTestBase& Test)
		{
			if (!Row.Initialize(Test))
			{
				return false;
			}
			GI.Reset(NewObject<UGameInstance>(GetTransientPackage(), NAME_None, RF_Transient));
			Viewport.Reset(NewObject<UGameViewportClient>(GEngine, NAME_None, RF_Transient));
			FObjectProperty* GIProp = FindFProperty<FObjectProperty>(UGameViewportClient::StaticClass(), TEXT("GameInstance"));
			FObjectProperty* WorldProp = FindFProperty<FObjectProperty>(UGameViewportClient::StaticClass(), TEXT("World"));
			FArrayProperty* PlayersProp = FindFProperty<FArrayProperty>(UGameInstance::StaticClass(), TEXT("LocalPlayers"));
			if (!GIProp || !WorldProp || !PlayersProp)
			{
				Test.AddError(TEXT("CommonUI fixture identity schema is absent"));
				return false;
			}
			FObjectProperty* Inner = CastField<FObjectProperty>(PlayersProp->Inner);
			if (!Test.TestNotNull(TEXT("local player identity"), Inner))
			{
				return false;
			}
			GIProp->SetObjectPropertyValue_InContainer(Viewport.Get(), GI.Get());
			WorldProp->SetObjectPropertyValue_InContainer(Viewport.Get(), Row.World);
			FScriptArrayHelper Players(PlayersProp, PlayersProp->ContainerPtrToValuePtr<void>(GI.Get()));
			Inner->SetObjectPropertyValue(Players.GetRawPtr(Players.AddValue()), Row.LocalPlayer);
			Row.LocalPlayer->ViewportClient = Viewport.Get();
			UI = Row.LocalPlayer->GetSubsystem<UVBUIManagerSubsystem>();
			Layout = CreateWidget<UVBPrimaryLayout>(Row.Controller, UVBPrimaryLayout::StaticClass());
			if (!Test.TestNotNull(TEXT("UI owner"), UI) || !Test.TestNotNull(TEXT("native layer layout"), Layout))
			{
				return false;
			}
			FVBCinematicHUDTestAccess::Install(*UI, Layout, Row.Controller);
			Caption = NewObject<UCanvasPanel>(Layout);
			Caption->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (!Test.TestTrue(TEXT("caption outside the Game layer"), UI->AttachPresentation(*Caption)))
			{
				return false;
			}
			Slate.Construct(*Layout);
			return true;
		}
		~FHUDPresentationFixture()
		{
			if (UI)
			{
				FVBCinematicHUDTestAccess::Destroy(*UI);
			}
			if (Layout)
			{
				Slate.Destruct(*Layout);
			}
			Row.Shutdown();
			if (Row.LocalPlayer)
			{
				Row.LocalPlayer->ViewportClient = nullptr;
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicHUDCompositionTest, "VB.Unit.Cinematic.HUD.Composition", HUDFlags)
bool FVBCinematicHUDCompositionTest::RunTest(const FString& Parameters)
{
	FHUDPresentationFixture F;
	if (!F.Init(*this))
	{
		return false;
	}
	UCommonActivatableWidgetContainerBase* Game = F.Layout->GetLayer(VBGameplayTags::UI_Layer_Game);
	if (!TestNotNull(TEXT("Game layer"), Game))
	{
		return false;
	}
	Game->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	APlayerController* Foreign = F.Row.World->SpawnActor<APlayerController>();
	if (TestNotNull(TEXT("different controller"), Foreign))
	{
		AddExpectedError(TEXT("HUD 표시 요청 플레이어가"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("another player cannot hide this HUD"), F.UI->BeginHUDHide(*Foreign).IsValid());
	}
	const int32 InputBefore = FVBCinematicHUDTestAccess::InputHolds(*F.UI);
	const FGuid First = F.UI->BeginHUDHide(*F.Row.Controller);
	const FGuid Second = F.UI->BeginHUDHide(*F.Row.Controller);
	TestTrue(TEXT("distinct hide handles"), First.IsValid() && Second.IsValid() && First != Second);
	TestEqual(TEXT("actual Game layer is collapsed"), Game->GetVisibility(), ESlateVisibility::Collapsed);
	TestTrue(TEXT("caption parent survives outside the hidden layer"), F.Caption->GetParent() && static_cast<UWidget*>(F.Caption->GetParent()) != static_cast<UWidget*>(Game));
	TestEqual(TEXT("caption visibility is preserved"), F.Caption->GetVisibility(), ESlateVisibility::HitTestInvisible);
	F.UI->EndHUDHide(First);
	TestEqual(TEXT("second caller keeps HUD hidden"), Game->GetVisibility(), ESlateVisibility::Collapsed);
	F.UI->SetLayerHidden(VBGameplayTags::UI_Layer_Game, true);
	F.UI->EndHUDHide(Second);
	TestEqual(TEXT("death hide survives cinematic release"), Game->GetVisibility(), ESlateVisibility::Collapsed);
	F.UI->EndHUDHide(Second);
	F.UI->SetLayerHidden(VBGameplayTags::UI_Layer_Game, false);
	TestEqual(TEXT("last reason restores the actual prior visibility"), Game->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("presentation creates no input suspension"), FVBCinematicHUDTestAccess::InputHolds(*F.UI), InputBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicHUDCloseTest, "VB.Unit.Cinematic.HUD.Close", HUDFlags)
bool FVBCinematicHUDCloseTest::RunTest(const FString& Parameters)
{
	FHUDPresentationFixture F;
	if (!F.Init(*this))
	{
		return false;
	}
	UCommonActivatableWidgetContainerBase* Game = F.Layout->GetLayer(VBGameplayTags::UI_Layer_Game);
	if (!TestNotNull(TEXT("Game layer"), Game))
	{
		return false;
	}
	const ESlateVisibility Before = Game->GetVisibility();
	UVBCinematicDirectorSubsystem* Director = NewObject<UVBCinematicDirectorSubsystem>(F.Row.World);
	for (EVBCinematicEndType End : {EVBCinematicEndType::Completed, EVBCinematicEndType::Aborted})
	{
		const FGuid Token = F.UI->BeginHUDHide(*F.Row.Controller);
		TestTrue(TEXT("UI owns a real hide handle"), Token.IsValid());
		FVBCinematicHUDTestAccess::Close(*Director, *F.UI, Token, End);
		TestEqual(TEXT("shared Director cleanup restores the HUD"), Game->GetVisibility(), Before);
		F.UI->EndHUDHide(Token);
		TestEqual(TEXT("stale release is harmless"), Game->GetVisibility(), Before);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicHUDSlotsTest, "VB.Unit.Cinematic.HUD.Slots", HUDFlags)
bool FVBCinematicHUDSlotsTest::RunTest(const FString& Parameters)
{
	FHUDPresentationFixture F;
	if (!F.Init(*this))
	{
		return false;
	}
	UVBHUDLayoutData* Data = NewObject<UVBHUDLayoutData>(F.Layout);
	FVBUISlotPlacement Placement;
	Placement.Slot = VBGameplayTags::UI_Slot_HUD_Subtitle;
	Placement.Anchors = FAnchors(0.5f, 1.0f);
	Placement.Alignment = FVector2D(0.5, 1);
	Placement.Position = FVector2D(0, -190);
	Data->Slots.Add(Placement);
	FVBUISlotDefaultWidget Default;
	Default.Slot = Placement.Slot;
	Default.WidgetClass = UVBUISlotTestEntry::StaticClass();
	Data->Widgets.Add(Default);
	TArray<TObjectPtr<UVBUISlotWidget>> Slots;
	TArray<FVBUISlotHandle> Handles;
	VBHUDLayout::Build(*F.Layout, *F.Caption, *Data, Slots, Handles);
	TestEqual(TEXT("one actual caption slot constructed"), Slots.Num(), 1);
	TestEqual(TEXT("default widget registered once"), Handles.Num(), 1);
	if (Slots.Num() == 1)
	{
		UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Slots[0]->Slot);
		if (TestNotNull(TEXT("actual canvas placement"), Slot))
		{
			TestTrue(TEXT("caption geometry retained"), Slot->GetPosition().Equals(Placement.Position)
				&& Slot->GetAlignment().Equals(Placement.Alignment)
				&& Slot->GetAnchors().Minimum.Equals(Placement.Anchors.Minimum));
		}
		UVBUISlotSubsystem* Registry = UVBUISlotSubsystem::Get(F.Row.Controller);
		TestTrue(TEXT("same owner scope resolves the registered widget"), Registry && Registry->GetEntriesFor(*Slots[0]).Num() == 1);
	}
	for (FVBUISlotHandle& Handle : Handles)
	{
		Handle.Unregister();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicHUDReentryTest, "VB.Unit.Cinematic.HUD.ReentrantClose", HUDFlags)
bool FVBCinematicHUDReentryTest::RunTest(const FString& Parameters)
{
	FHUDPresentationFixture F;
	if (!F.Init(*this))
	{
		return false;
	}
	UVBCinematicDirectorSubsystem* Director = NewObject<UVBCinematicDirectorSubsystem>(F.Row.World);
	const FGuid First = F.UI->BeginHUDHide(*F.Row.Controller);
	FGuid Next;
	bool bMayReenter = true;
	const FDelegateHandle Handle = F.UI->OnInputAvailabilityChanged.AddLambda([&]()
	{
		if (bMayReenter)
		{
			bMayReenter = false;
			Next = F.UI->BeginHUDHide(*F.Row.Controller);
			FVBCinematicHUDTestAccess::Bind(*Director, *F.UI, Next);
		}
	});
	FVBCinematicHUDTestAccess::Close(*Director, *F.UI, First, EVBCinematicEndType::Completed);
	F.UI->OnInputAvailabilityChanged.Remove(Handle);
	TestTrue(TEXT("UI release can synchronously request a new presentation"), Next.IsValid());
	TestTrue(TEXT("old cleanup cannot erase new request data"), FVBCinematicHUDTestAccess::Holds(*Director, Next));
	FVBCinematicHUDTestAccess::Close(*Director, *F.UI, Next, EVBCinematicEndType::Aborted);
	return true;
}
#endif
