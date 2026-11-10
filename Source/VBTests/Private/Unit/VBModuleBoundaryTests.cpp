// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/VBGameplayTags.h"
#include "Blueprint/UserWidget.h"
#include "UI/Notice/VBNoticeFeedWidget.h"
#include "UI/VBUISettings.h"

// 설정 타입이 UUserWidget 이 되어 컴파일러가 못 잡는 것을 대신 잡는다. 다른 위젯을 꽂으면 알림 줄이 조용히 사라진다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBModuleBoundaryNoticeFeedTest, "VB.Unit.ModuleBoundary.NoticeFeedClass",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVBModuleBoundaryNoticeFeedTest::RunTest(const FString& Parameters)
{
	const TSoftClassPtr<UUserWidget>& ClassRef = GetDefault<UVBUISettings>()->NoticeFeedClass;
	if (!TestFalse(TEXT("notice feed class is configured"), ClassRef.IsNull()))
	{
		return false;
	}

	UClass* FeedClass = ClassRef.LoadSynchronous();
	if (!TestNotNull(TEXT("notice feed class loads"), FeedClass))
	{
		return false;
	}

	TestTrue(TEXT("configured class is a notice feed"), FeedClass->IsChildOf(UVBNoticeFeedWidget::StaticClass()));
	// 반대 대조: 알림 줄이 아닌 위젯은 이 판정을 통과하지 못한다.
	TestFalse(TEXT("a plain widget is not a notice feed"), UUserWidget::StaticClass()->IsChildOf(UVBNoticeFeedWidget::StaticClass()));
	return true;
}

// 층 태그는 UI 기반 코드로 옮겼다. 기존 호출부가 쓰는 이름 그대로 등록되어 있어야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBModuleBoundaryLayerTagsTest, "VB.Unit.ModuleBoundary.LayerTags",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVBModuleBoundaryLayerTagsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("game layer"), FGameplayTag(VBGameplayTags::UI_Layer_Game).GetTagName(), FName(TEXT("UI.Layer.Game")));
	TestEqual(TEXT("game menu layer"), FGameplayTag(VBGameplayTags::UI_Layer_GameMenu).GetTagName(), FName(TEXT("UI.Layer.GameMenu")));
	TestEqual(TEXT("menu layer"), FGameplayTag(VBGameplayTags::UI_Layer_Menu).GetTagName(), FName(TEXT("UI.Layer.Menu")));
	TestEqual(TEXT("modal layer"), FGameplayTag(VBGameplayTags::UI_Layer_Modal).GetTagName(), FName(TEXT("UI.Layer.Modal")));
	TestEqual(TEXT("dialog layer"), FGameplayTag(VBGameplayTags::UI_Layer_Dialog).GetTagName(), FName(TEXT("UI.Layer.Dialog")));
	// 반대 대조: 등록되지 않은 이름은 유효한 태그로 풀리지 않는다.
	TestFalse(TEXT("an unknown layer does not resolve"), FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.Unknown"), false).IsValid());
	return true;
}

#endif
