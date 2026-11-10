#include "UI/VBWeaponWheelWidget.h"
#include "Player/VBWeaponWheelComponent.h"
#include "Player/VBPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Fonts/FontMeasure.h"
#include "Styling/CoreStyle.h"

void UVBWeaponWheelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		WidgetTree->RootWidget = WidgetTree->ConstructWidget<UCanvasPanel>();
	}

	SetVisibility(ESlateVisibility::Collapsed);
}

void UVBWeaponWheelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 바퀴는 PC 생성자 하위 객체라 PC 가 있으면 있다 - 기다릴 신호가 없다.
	if (const AVBPlayerController* PC = GetOwningPlayer<AVBPlayerController>())
	{
		BindWheel(PC->GetWeaponWheel());
	}
}

void UVBWeaponWheelWidget::BindWheel(UVBWeaponWheelComponent* Component)
{
	if (Wheel.IsValid())
	{
		Wheel->OnChanged.Remove(ChangedHandle);
	}

	Wheel = Component;
	if (Component)
	{
		Component->SetPresentation(this);
		ChangedHandle = Component->OnChanged.AddUObject(this, &ThisClass::Refresh);
	}

	Refresh();
}

void UVBWeaponWheelWidget::NativeDestruct()
{
	if (Wheel.IsValid())
	{
		Wheel->OnChanged.Remove(ChangedHandle);
		Wheel->SetPresentation(nullptr);
	}

	Wheel.Reset();
	Super::NativeDestruct();
}

void UVBWeaponWheelWidget::Refresh()
{
	SetVisibility(Wheel.IsValid() && Wheel->IsOpen() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	InvalidateLayoutAndVolatility();
}

int32 UVBWeaponWheelWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	const int32 BaseLayer = Super::NativePaint(Args, Geometry, CullingRect, Elements, LayerId, Style, bParentEnabled);
	if (!Wheel.IsValid() || !Wheel->IsOpen())
	{
		return BaseLayer;
	}

	const TArray<FVBWeaponWheelEntry>& Entries = Wheel->GetVisibleEntries();
	if (Entries.IsEmpty())
	{
		return BaseLayer;
	}

	// 호는 부채꼴의 96% 만 채워 칸 사이에 틈을 둔다.
	constexpr int32 ArcSegments = 48;
	constexpr double ArcFill = 0.96;
	const FVector2D Center = Geometry.GetLocalSize() * 0.5;
	const double Sector = 2.0 * PI / Entries.Num();
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 18);
	const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		TArray<FVector2D> Arc;
		for (int32 Segment = 0; Segment <= ArcSegments; ++Segment)
		{
			const double Angle = Sector * (Index - ArcFill * 0.5 + ArcFill * Segment / ArcSegments);
			Arc.Add(Center + FVector2D(FMath::Sin(Angle), -FMath::Cos(Angle)) * RingRadius);
		}

		const bool bSelected = Index == Wheel->GetSelectedSlot();
		FSlateDrawElement::MakeLines(Elements, BaseLayer + 1, Geometry.ToPaintGeometry(), Arc,
			ESlateDrawEffect::None, bSelected ? SelectionColor : RingColor, true, RingThickness);
		const FVector2D LabelPosition = Center + FVector2D(FMath::Sin(Sector * Index), -FMath::Cos(Sector * Index)) * RingRadius;
		const FString Label = Entries[Index].Label.ToString();
		const FVector2D Size = Measure->Measure(Label, Font);
		FSlateDrawElement::MakeText(Elements, BaseLayer + 2,
			Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(LabelPosition - Size * 0.5)),
			Label, Font, ESlateDrawEffect::None, FLinearColor::White);
	}

	const FString Hint = Wheel->GetSelectedSlot() == INDEX_NONE
		? NSLOCTEXT("VBWeaponWheel", "Cancel", "\uB193\uC73C\uBA74 \uCDE8\uC18C").ToString()
		: NSLOCTEXT("VBWeaponWheel", "Confirm", "\uB193\uC73C\uBA74 \uC120\uD0DD").ToString();
	const FSlateFontInfo HintFont = FCoreStyle::GetDefaultFontStyle("Regular", 13);
	const FVector2D HintSize = Measure->Measure(Hint, HintFont);
	FSlateDrawElement::MakeText(Elements, BaseLayer + 2,
		Geometry.ToPaintGeometry(HintSize, FSlateLayoutTransform(Center - HintSize * 0.5)),
		Hint, HintFont, ESlateDrawEffect::None, FLinearColor::White);
	const FVector2D CursorPosition = Center + Wheel->GetCursor();
	const TArray<FVector2D> Cross = {CursorPosition + FVector2D(-4, 0), CursorPosition + FVector2D(4, 0), CursorPosition,
		CursorPosition + FVector2D(0, -4), CursorPosition + FVector2D(0, 4)};
	FSlateDrawElement::MakeLines(Elements, BaseLayer + 3, Geometry.ToPaintGeometry(), Cross,
		ESlateDrawEffect::None, FLinearColor::White, true, 2.f);
	return BaseLayer + 3;
}
