// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/DataValidation.h"
#include "UI/Inventory/VBInventoryScreenWidget.h"

// 장비 칸 종류마다 인벤토리 화면 이름표가 있어야 한다 - 없으면 tag 원문이 보인다.
//   이름표 없는 C++ 기본값이 대조군이라 검증이 거절해야 하고, 실제 WBP 는 통과해야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInventorySlotLabelTest, "VB.Unit.UI.Inventory.SlotLabels",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInventorySlotLabelTest::RunTest(const FString& Parameters)
{
	FDataValidationContext Empty;
	TestTrue(TEXT("a screen without slot labels is invalid"),
		GetDefault<UVBInventoryScreenWidget>()->IsDataValid(Empty) == EDataValidationResult::Invalid);

	UClass* Shipped = LoadClass<UVBInventoryScreenWidget>(nullptr, TEXT("/Game/VB/UI/Inventory/WBP_Inventory.WBP_Inventory_C"));
	if (!TestNotNull(TEXT("shipped WBP_Inventory"), Shipped))
	{
		return false;
	}

	FDataValidationContext Context;
	const EDataValidationResult Result = Shipped->GetDefaultObject<UVBInventoryScreenWidget>()->IsDataValid(Context);
	TArray<FText> Warnings, Errors;
	Context.SplitIssues(Warnings, Errors);
	for (const FText& Error : Errors)
	{
		AddError(Error.ToString());
	}

	TestTrue(TEXT("shipped WBP_Inventory labels every slot kind"), Result != EDataValidationResult::Invalid);
	return true;
}
#endif
