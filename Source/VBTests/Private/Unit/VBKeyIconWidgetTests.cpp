// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Misc/ScopeExit.h"
#include "UI/VBInputKeyText.h"
#include "Unit/VBUITestFixture.h"
#include "UObject/UnrealType.h"

// 키 칸 한 쌍(글자, 그림)의 보이기 규칙 - 네 화면이 같이 쓰는 한 곳.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBKeyIconShowTest, "VB.Unit.Input.KeyIconShow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBKeyIconShowTest::RunTest(const FString& Parameters)
{
	UTextBlock* Label = NewObject<UTextBlock>(GetTransientPackage());
	UImage* Icon = NewObject<UImage>(GetTransientPackage());
	UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage());
	FSlateBrush Brush;
	Brush.SetResourceObject(Texture);
	const FText KeyText = FText::FromString(TEXT("E"));
	constexpr ESlateVisibility Shown = ESlateVisibility::HitTestInvisible;
	constexpr ESlateVisibility Folded = ESlateVisibility::Collapsed;

	VBInputKeyText::ShowKey(Label, Icon, KeyText, Brush);
	TestEqual(TEXT("picture: icon shown"), Icon->GetVisibility(), Shown);
	TestTrue(TEXT("picture: icon draws the given texture"), Icon->GetBrush().GetResourceObject() == Texture);
	TestEqual(TEXT("picture: text folded"), Label->GetVisibility(), Folded);

	VBInputKeyText::ShowKey(Label, Icon, KeyText, TOptional<FSlateBrush>());
	TestEqual(TEXT("no picture: icon folded"), Icon->GetVisibility(), Folded);
	TestEqual(TEXT("no picture: text shown"), Label->GetVisibility(), Shown);
	TestTrue(TEXT("no picture: text is the key name"), Label->GetText().EqualTo(KeyText));

	VBInputKeyText::ShowKey(Label, nullptr, KeyText, Brush);
	TestEqual(TEXT("no icon slot: text shown although a picture exists"), Label->GetVisibility(), Shown);

	VBInputKeyText::ShowKey(Label, Icon, FText::GetEmpty(), TOptional<FSlateBrush>());
	TestEqual(TEXT("nothing: icon folded"), Icon->GetVisibility(), Folded);
	TestEqual(TEXT("nothing: text folded"), Label->GetVisibility(), Folded);

	VBInputKeyText::ShowKey(nullptr, nullptr, KeyText, Brush); // 칸이 둘 다 없는 WBP 변형도 멈추지 않는다
	return true;
}

// 네 WBP 에 그림 칸 ImgKey 가 실제로 묶였나. 선택 칸(BindWidgetOptional)은 이름이 틀려도 조용히 비므로 이 시험이 유일한 막이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBKeyIconWidgetsTest, "VB.Unit.Input.KeyIconWidgets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBKeyIconWidgetsTest::RunTest(const FString& Parameters)
{
	VBUITest::FRowFixture Fixture;
	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	const TCHAR* Classes[] = {
		TEXT("/Game/VB/UI/WBP_InteractPrompt.WBP_InteractPrompt_C"),
		TEXT("/Game/VB/UI/Inventory/WBP_QuickSlot.WBP_QuickSlot_C"),
		TEXT("/Game/VB/UI/Tutorial/WBP_TutorialHint.WBP_TutorialHint_C"),
		TEXT("/Game/VB/UI/Tutorial/WBP_Tutorial.WBP_Tutorial_C"),
	};
	for (const TCHAR* ClassPath : Classes)
	{
		UUserWidget* Widget = Fixture.MakeRow<UUserWidget>(*this, ClassPath);
		if (!Widget)
		{
			continue;
		}
		// C++ 부모의 칸을 본다 - WBP 의 변수 칸은 같은 이름의 자기 변수를 만들어서, WBP 클래스에서 찾으면 C++ 묶음이 끊겨도 통과한다
		UClass* Native = Widget->GetClass();
		while (Native && !Native->HasAnyClassFlags(CLASS_Native))
		{
			Native = Native->GetSuperClass();
		}

		const FObjectProperty* Slot = Native ? FindFProperty<FObjectProperty>(Native, TEXT("ImgKey")) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s: C++ parent declares ImgKey"), ClassPath), Slot))
		{
			continue;
		}

		TestTrue(*FString::Printf(TEXT("%s: WBP has an Image named ImgKey"), ClassPath),
			Cast<UImage>(Slot->GetObjectPropertyValue_InContainer(Widget)) != nullptr);
	}

	return true;
}
#endif
