// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Inventory/Crafting/VBCraftingComponent.h"
#include "Inventory/Shop/VBShopComponent.h"
#include "Inventory/Shop/VBShopRules.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/VBStationSessionComponent.h"
#include "UI/Crafting/VBCraftingScreenWidget.h"
#include "UI/Inventory/VBInventoryScreenWidget.h"
#include "UI/Map/VBFastTravelScreenWidget.h"
#include "UI/Quest/VBQuestJournalScreenWidget.h"
#include "UI/Shop/VBShopScreenWidget.h"
#include "UI/VBComponentSubscription.h"
#include "UI/VBListDetailScreenWidget.h"
#include "UI/VBScreenText.h"
#include "VBSubscriptionTestTypes.h"

// 목록 화면이 같이 쓰는 값 함수, 구독, 저장 이름의 단위 시험. 위젯 동작(초점, 줄 짓기)은 PIE 가 본다.

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBListDetailKeepSelectionTest, "VB.Unit.UI.ListDetail.KeepSelectionOrFirst",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBListDetailKeepSelectionTest::RunTest(const FString& Parameters)
{
	{
		const TArray<int32> Rows = {4, 7};
		int32 Selected = 7;
		UVBListDetailScreenWidget::KeepSelectionOrFirst(Rows, Selected, INDEX_NONE);
		TestEqual(TEXT("a listed selection stays"), Selected, 7);
		Selected = 9;
		UVBListDetailScreenWidget::KeepSelectionOrFirst(Rows, Selected, INDEX_NONE);
		TestEqual(TEXT("a gone selection moves to the first row"), Selected, 4);
		UVBListDetailScreenWidget::KeepSelectionOrFirst(TArray<int32>(), Selected, INDEX_NONE);
		TestTrue(TEXT("no rows clears to None, not to 0"), Selected == INDEX_NONE);
	}

	{
		const UVBItemDefinition* Listed = NewObject<UVBItemDefinition>();
		const UVBItemDefinition* Gone = NewObject<UVBItemDefinition>();
		const TArray<TWeakObjectPtr<const UVBItemDefinition>> Rows = {Listed};
		TWeakObjectPtr<const UVBItemDefinition> Selected = Gone;
		UVBListDetailScreenWidget::KeepSelectionOrFirst(Rows, Selected, nullptr);
		TestTrue(TEXT("weak key: a gone selection moves to the first row"), Selected.Get() == Listed);
		const TArray<TWeakObjectPtr<const UVBItemDefinition>> NoRows;
		UVBListDetailScreenWidget::KeepSelectionOrFirst(NoRows, Selected, nullptr);
		TestTrue(TEXT("weak key: no rows clears the selection"), Selected.Get() == nullptr);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBScreenTextResultReasonTest, "VB.Unit.UI.ScreenText.ResultReason",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBScreenTextResultReasonTest::RunTest(const FString& Parameters)
{
	TMap<EVBPurchaseResult, FText> Texts;
	Texts.Add(EVBPurchaseResult::NoRoom, INVTEXT("full"));
	TestEqual(TEXT("a listed result reads its text"),
		VBScreenText::ResultReason(Texts, EVBPurchaseResult::NoRoom, /*bHasSelection=*/true).ToString(), FString(TEXT("full")));
	TestTrue(TEXT("no selection clears the reason"), VBScreenText::ResultReason(Texts, EVBPurchaseResult::NoRoom, /*bHasSelection=*/false).IsEmpty());
	TestTrue(TEXT("a result missing from the table clears the reason"), VBScreenText::ResultReason(Texts, EVBPurchaseResult::Ok, /*bHasSelection=*/true).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBScreenTextCurrencyTest, "VB.Unit.UI.ScreenText.HeldCurrencyLine",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBScreenTextCurrencyTest::RunTest(const FString& Parameters)
{
	UVBItemDefinition* Gold = NewObject<UVBItemDefinition>();
	Gold->DisplayName = INVTEXT("Gold");
	const FText Format = INVTEXT("{Name} {Count}");
	TestEqual(TEXT("name comes from the currency item"), VBScreenText::HeldCurrencyLine(Format, Gold, 12).ToString(), FString(TEXT("Gold 12")));
	TestEqual(TEXT("no currency item leaves the name empty"), VBScreenText::HeldCurrencyLine(Format, nullptr, 0).ToString(), FString(TEXT(" 0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBComponentSubscriptionTest, "VB.Unit.UI.ComponentSubscription",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBComponentSubscriptionTest::RunTest(const FString& Parameters)
{
	UVBShopComponent* First = NewObject<UVBShopComponent>();
	UVBCraftingComponent* Second = NewObject<UVBCraftingComponent>();
	UVBSubscriptionTestListener* Listener = NewObject<UVBSubscriptionTestListener>();
	TVBComponentSubscription<UVBStationSessionComponent> Subscription;

	Subscription.Bind(First, &UVBStationSessionComponent::OnViewChanged, Listener, &UVBSubscriptionTestListener::Hear);
	TestTrue(TEXT("remembers the bound component"), Subscription.Get() == First);
	First->OnViewChanged.Broadcast();
	TestEqual(TEXT("the bound component reaches the listener"), Listener->Heard, 1);

	Subscription.Bind(Second, &UVBStationSessionComponent::OnViewChanged, Listener, &UVBSubscriptionTestListener::Hear);
	First->OnViewChanged.Broadcast();
	TestEqual(TEXT("rebinding leaves the old component"), Listener->Heard, 1);
	Second->OnViewChanged.Broadcast();
	TestEqual(TEXT("rebinding reaches the new component"), Listener->Heard, 2);

	Subscription.Bind(nullptr, &UVBStationSessionComponent::OnViewChanged, Listener, &UVBSubscriptionTestListener::Hear);
	TestTrue(TEXT("binding nothing forgets the component"), Subscription.Get() == nullptr);
	Second->OnViewChanged.Broadcast();
	TestEqual(TEXT("binding nothing only unbinds"), Listener->Heard, 2);

	Subscription.Bind(First, &UVBStationSessionComponent::OnViewChanged, Listener, &UVBSubscriptionTestListener::Hear);
	Subscription.Reset();
	First->OnViewChanged.Broadcast();
	TestEqual(TEXT("reset unbinds"), Listener->Heard, 2);
	return true;
}

// WBP 값은 이름으로 불러온다 - 부모로 옮긴 속성의 이름이 바뀌면 저장된 값이 조용히 사라진다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBListDetailSavedNamesTest, "VB.Unit.UI.ListDetail.SavedNames",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBListDetailSavedNamesTest::RunTest(const FString& Parameters)
{
	const TArray<UClass*> ListScreens = {UVBCraftingScreenWidget::StaticClass(), UVBShopScreenWidget::StaticClass(),
		UVBFastTravelScreenWidget::StaticClass(), UVBInventoryScreenWidget::StaticClass(), UVBQuestJournalScreenWidget::StaticClass()};
	for (const UClass* Screen : ListScreens)
	{
		for (const TCHAR* Name : {TEXT("EntryWidgetClass"), TEXT("EntryBox"), TEXT("TxtEmpty")})
		{
			TestNotNull(*FString::Printf(TEXT("%s keeps %s"), *Screen->GetName(), Name), FindFProperty<FProperty>(Screen, Name));
		}
	}

	for (const UClass* Station : {UVBCraftingScreenWidget::StaticClass(), UVBShopScreenWidget::StaticClass()})
	{
		for (const TCHAR* Name : {TEXT("CurrencyFormat"), TEXT("TxtCurrency")})
		{
			TestNotNull(*FString::Printf(TEXT("%s keeps %s"), *Station->GetName(), Name), FindFProperty<FProperty>(Station, Name));
		}

		// 뒤로 가기를 안 받으면 세션을 벗어날 길이 없다.
		const FBoolProperty* BackHandler = FindFProperty<FBoolProperty>(Station, TEXT("bIsBackHandler"));
		const FBoolProperty* RequireClose = FindFProperty<FBoolProperty>(Station, TEXT("bRequireCloseButton"));
		if (TestNotNull(TEXT("bIsBackHandler exists"), BackHandler) && TestNotNull(TEXT("bRequireCloseButton exists"), RequireClose))
		{
			TestTrue(*FString::Printf(TEXT("%s handles back"), *Station->GetName()), BackHandler->GetPropertyValue_InContainer(Station->GetDefaultObject()));
			TestTrue(*FString::Printf(TEXT("%s requires a close button"), *Station->GetName()), RequireClose->GetPropertyValue_InContainer(Station->GetDefaultObject()));
		}
	}

	return true;
}
#endif
