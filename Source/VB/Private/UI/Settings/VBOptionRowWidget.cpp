// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Settings/VBOptionRowWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

#define LOCTEXT_NAMESPACE "VBOptions"

void UVBOptionRowWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BtnPrev->OnClicked.AddDynamic(this, &UVBOptionRowWidget::HandlePrev);
	BtnNext->OnClicked.AddDynamic(this, &UVBOptionRowWidget::HandleNext);
	BtnAction->OnClicked.AddDynamic(this, &UVBOptionRowWidget::HandleAction);
}

void UVBOptionRowWidget::Setup(int32 InRowIndex, const FVBOptionEntry& Entry)
{
	RowIndex = InRowIndex;
	Kind = Entry.Kind;
	Choices = Entry.Choices;
	Range = Entry.Range;
	StepSize = Entry.Step;
	bPercent = Entry.bPercent;
	TxtLabel->SetText(Entry.Label);

	const bool bAction = Kind == EVBOptionKind::Action;
	const ESlateVisibility Stepper = bAction ? ESlateVisibility::Collapsed : ESlateVisibility::Visible;
	BtnPrev->SetVisibility(Stepper);
	BtnNext->SetVisibility(Stepper);
	TxtValue->SetVisibility(bAction ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	BtnAction->SetVisibility(bAction ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UVBOptionRowWidget::SetValue(float InValue)
{
	Value = InValue;
	RefreshValueText();
}

UWidget* UVBOptionRowWidget::GetFocusTarget() const
{
	return Kind == EVBOptionKind::Action ? static_cast<UWidget*>(BtnAction) : static_cast<UWidget*>(BtnNext);
}

void UVBOptionRowWidget::HandlePrev()
{
	Step(-1);
}

void UVBOptionRowWidget::HandleNext()
{
	Step(1);
}

void UVBOptionRowWidget::HandleAction()
{
	OnAction.ExecuteIfBound(RowIndex);
}

void UVBOptionRowWidget::Step(int32 Direction)
{
	switch (Kind)
	{
	case EVBOptionKind::Slider:
		Value = FMath::Clamp(Value + Direction * StepSize, Range.Min, Range.Max);
		break;
	case EVBOptionKind::Toggle:
		Value = Value >= 0.5f ? 0.0f : 1.0f;
		break;
	case EVBOptionKind::Choice:
		// 끝에서 반대쪽 끝으로 돈다 - 한 방향만 눌러도 전부 지난다.
		if (Choices.IsEmpty())
		{
			return;
		}

		Value = static_cast<float>((FMath::RoundToInt(Value) + Direction + Choices.Num()) % Choices.Num());
		break;
	default:
		return;
	}

	RefreshValueText();
	OnValueChanged.ExecuteIfBound(RowIndex, Value);
}

void UVBOptionRowWidget::RefreshValueText()
{
	switch (Kind)
	{
	case EVBOptionKind::Slider:
	{
		FNumberFormattingOptions Format;
		Format.SetMaximumFractionalDigits(bPercent ? 0 : 2);
		TxtValue->SetText(bPercent ? FText::Format(LOCTEXT("Percent", "{0}%"), FText::AsNumber(Value * 100.0f, &Format))
			: FText::AsNumber(Value, &Format));
		break;
	}
	case EVBOptionKind::Toggle:
		TxtValue->SetText(Value >= 0.5f ? LOCTEXT("On", "켜짐") : LOCTEXT("Off", "꺼짐"));
		break;
	case EVBOptionKind::Choice:
	{
		const int32 Index = FMath::RoundToInt(Value);
		TxtValue->SetText(Choices.IsValidIndex(Index) ? Choices[Index] : FText::GetEmpty());
		break;
	}
	default:
		break;
	}
}

#undef LOCTEXT_NAMESPACE
