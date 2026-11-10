// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "UI/Indicator/VBIndicatorRegistrySubsystem.h"
#include "UI/Indicator/VBKeyPromptIndicatorWidget.h"
#include "UI/Indicator/VBRegisteredIndicatorsWidget.h"
#include "VBUITestFixture.h"

// 표시기 목록의 요청은 등록 표시기 갈래가 요청마다 위젯 하나로 그린다(전에는 아무도 안 그려 처형 표시가 안 보였다).
//   대조: 사라진 번호의 위젯을 떼지 않으면 "removed" 줄이, 있던 번호를 다시 만들면 "same widget" 줄이 실패한다.
namespace VBRegisteredIndicatorTest
{
	using VBUITest::FRowFixture;

	UCanvasPanel* CanvasOf(const UVBRegisteredIndicatorsWidget& Branch) { return Cast<UCanvasPanel>(Branch.GetRootWidget()); }

	TPair<uint64, FVBIndicatorRequest> MakeEntry(uint64 Id, const UClass* WidgetClass)
	{
		FVBIndicatorRequest Request;
		Request.Data = const_cast<UClass*>(WidgetClass);
		return TPair<uint64, FVBIndicatorRequest>(Id, Request);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBRegisteredIndicatorSyncTest, "VB.Unit.Indicator.Drawer.Sync",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBRegisteredIndicatorSyncTest::RunTest(const FString& Parameters)
{
	using namespace VBRegisteredIndicatorTest;
	FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		Fixture.Shutdown();
		return false;
	}

	UVBRegisteredIndicatorsWidget* Branch = CreateWidget<UVBRegisteredIndicatorsWidget>(Fixture.Controller,
		UVBRegisteredIndicatorsWidget::StaticClass());
	UCanvasPanel* Canvas = Branch ? CanvasOf(*Branch) : nullptr;
	if (!TestNotNull(TEXT("branch makes its own canvas"), Canvas))
	{
		Fixture.Shutdown();
		return false;
	}

	Branch->SyncRequests({ MakeEntry(7, UVBKeyPromptIndicatorWidget::StaticClass()) });
	TestEqual(TEXT("one request: one widget"), Canvas->GetChildrenCount(), 1);
	UWidget* First = Canvas->GetChildrenCount() > 0 ? Canvas->GetChildAt(0) : nullptr;
	TestTrue(TEXT("of the request's class"), First && First->IsA<UVBKeyPromptIndicatorWidget>());

	Branch->SyncRequests({ MakeEntry(7, UVBKeyPromptIndicatorWidget::StaticClass()) });
	TestEqual(TEXT("same request again: still one"), Canvas->GetChildrenCount(), 1);
	TestTrue(TEXT("same request again: same widget"), Canvas->GetChildrenCount() == 1 && Canvas->GetChildAt(0) == First);

	// 그릴 위젯 클래스가 아닌 Data(추상 부모, 위젯 아닌 클래스)는 그리지 않고 클래스마다 한 번 말한다.
	AddExpectedMessagePlain(TEXT("그릴 위젯 클래스를 안 들었다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		Branch->SyncRequests({ MakeEntry(7, UVBKeyPromptIndicatorWidget::StaticClass()),
			MakeEntry(8, UVBRegisteredIndicatorWidget::StaticClass()), MakeEntry(9, AActor::StaticClass()) });
	}

	TestEqual(TEXT("bad data: not drawn"), Canvas->GetChildrenCount(), 1);

	Branch->SyncRequests({});
	TestEqual(TEXT("removed: widget gone"), Canvas->GetChildrenCount(), 0);
	Fixture.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBRegisteredIndicatorRegistryTest, "VB.Unit.Indicator.Drawer.ReadsRegistry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBRegisteredIndicatorRegistryTest::RunTest(const FString& Parameters)
{
	// 갈래는 매 틱 이 보는 사람의 요청을 목록에서 읽는다 - 요청을 걸면 위젯이 생기고, 떼면 사라진다.
	using namespace VBRegisteredIndicatorTest;
	FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		Fixture.Shutdown();
		return false;
	}

	UVBIndicatorRegistrySubsystem* Registry = Fixture.World->GetSubsystem<UVBIndicatorRegistrySubsystem>();
	UVBRegisteredIndicatorsWidget* Branch = CreateWidget<UVBRegisteredIndicatorsWidget>(Fixture.Controller,
		UVBRegisteredIndicatorsWidget::StaticClass());
	UCanvasPanel* Layer = NewObject<UCanvasPanel>(Fixture.Controller);
	UCanvasPanelSlot* BranchSlot = Branch && Layer ? Layer->AddChildToCanvas(Branch) : nullptr;
	if (!TestNotNull(TEXT("registry"), Registry) || !TestNotNull(TEXT("branch slot"), BranchSlot))
	{
		Fixture.Shutdown();
		return false;
	}

	AActor* Target = Fixture.World->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(Target);
	Target->SetRootComponent(Root);
	Root->RegisterComponent();

	FVBIndicatorRequest Request;
	// 묶음이 없는 요청은 목록이 받지 않는다(UVBIndicatorRegistrySubsystem::IsLive).
	Request.Group = VBGameplayTags::Indicator_Group_Execute;
	Request.AnchorComponent = Root;
	Request.Subject = Target;
	Request.Data = UVBKeyPromptIndicatorWidget::StaticClass();
	Request.ViewerScope = EVBIndicatorViewerScope::OneViewer;
	Request.Viewer = Fixture.Controller;
	FVBIndicatorView View;
	View.PC = Fixture.Controller;
	{
		FVBIndicatorRegistration Registration(*Registry, Request);
		Branch->UpdateIndicator(View, *BranchSlot);
		TestEqual(TEXT("registered: one widget"), CanvasOf(*Branch)->GetChildrenCount(), 1);
		Registration.Reset();
		Branch->UpdateIndicator(View, *BranchSlot);
		TestEqual(TEXT("unregistered: widget gone"), CanvasOf(*Branch)->GetChildrenCount(), 0);
	}

	Fixture.Shutdown();
	return true;
}

#endif
