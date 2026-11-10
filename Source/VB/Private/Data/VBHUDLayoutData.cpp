// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBHUDLayoutData.h"

#include "Blueprint/UserWidget.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool FVBUISlotPlacement::IsFullScreen() const
{
	return Anchors.Minimum.IsNearlyZero() && Anchors.Maximum.Equals(FVector2D(1.0, 1.0));
}

void UVBHUDLayoutData::CollectProblems(TArray<FString>& OutProblems) const
{
	TSet<FGameplayTag> SlotTags;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FGameplayTag& Tag = Slots[Index].Slot;
		if (!Tag.IsValid())
		{
			OutProblems.Add(FString::Printf(TEXT("Slots[%d] 의 태그가 비었다"), Index));
		}
		else if (SlotTags.Contains(Tag))
		{
			OutProblems.Add(FString::Printf(TEXT("Slots[%d] 의 태그 %s 가 겹친다 - 같은 태그의 자리 둘이 같은 위젯을 두 장 띄운다"), Index, *Tag.ToString()));
		}

		SlotTags.Add(Tag);
		// 한 축만 늘인 앵커는 이 데이터가 다루지 않는다 - 점 앵커처럼 Position, Size 가 적용돼 엉뚱한 자리가 된다.
		const FAnchors& Anchors = Slots[Index].Anchors;
		const bool bPointX = FMath::IsNearlyEqual(Anchors.Minimum.X, Anchors.Maximum.X);
		const bool bPointY = FMath::IsNearlyEqual(Anchors.Minimum.Y, Anchors.Maximum.Y);
		if (!Slots[Index].IsFullScreen() && !(bPointX && bPointY))
		{
			OutProblems.Add(FString::Printf(TEXT("Slots[%d](%s) 의 앵커는 점이거나 (0,0)-(1,1) 전체 화면이어야 한다"), Index, *Tag.ToString()));
		}
	}

	for (int32 Index = 0; Index < Widgets.Num(); ++Index)
	{
		const FVBUISlotDefaultWidget& Row = Widgets[Index];
		if (!SlotTags.Contains(Row.Slot))
		{
			OutProblems.Add(FString::Printf(TEXT("Widgets[%d] 의 슬롯 %s 에 자리가 없다 - 아무 데도 안 뜬다"), Index, *Row.Slot.ToString()));
		}

		if (!Row.WidgetClass && !Row.bOptional)
		{
			OutProblems.Add(FString::Printf(TEXT("Widgets[%d](%s) 의 클래스가 비었다"), Index, *Row.Slot.ToString()));
		}
	}
}

#if WITH_EDITOR
EDataValidationResult UVBHUDLayoutData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TArray<FString> Problems;
	CollectProblems(Problems);
	for (const FString& Problem : Problems)
	{
		Context.AddError(FText::FromString(Problem));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif
