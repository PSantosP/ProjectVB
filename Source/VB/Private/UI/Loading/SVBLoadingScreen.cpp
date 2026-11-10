// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Loading/SVBLoadingScreen.h"

#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void SVBLoadingScreen::Construct(const FArguments& InArgs)
{
	FadeInSeconds = FMath::Max(0.0f, InArgs._FadeInSeconds);
	BackgroundColor = InArgs._BackgroundColor;
	StartSeconds = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCurrentTime() : 0.0;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(this, &SVBLoadingScreen::GetBackgroundColor)
		.ColorAndOpacity(this, &SVBLoadingScreen::GetContentColor)
		.Padding(0.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			.Padding(InArgs._ContentPadding)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(FMargin(0.0f, 0.0f, 0.0f, InArgs._ThrobberSpacing))
				[
					SNew(SCircularThrobber)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(InArgs._Text)
					.Font(InArgs._Font)
					.ColorAndOpacity(InArgs._TextColor)
				]
			]
		]
	];
}

float SVBLoadingScreen::GetFade() const
{
	if (FadeInSeconds <= 0.0f || !FSlateApplication::IsInitialized())
	{
		return 1.0f;
	}

	const double Elapsed = FSlateApplication::Get().GetCurrentTime() - StartSeconds;
	return FMath::Clamp(static_cast<float>(Elapsed / FadeInSeconds), 0.0f, 1.0f);
}

FLinearColor SVBLoadingScreen::GetContentColor() const
{
	return FLinearColor(1.0f, 1.0f, 1.0f, GetFade());
}

FSlateColor SVBLoadingScreen::GetBackgroundColor() const
{
	return FLinearColor(BackgroundColor.R, BackgroundColor.G, BackgroundColor.B, BackgroundColor.A * GetFade());
}
