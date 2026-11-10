// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Widgets/SCompoundWidget.h"

/**
 * 로딩 화면의 생김새. 로딩 스레드에서 그려지므로 UObject, 설정, 월드를 읽지 않고 값만 받는다.
 * 흐려짐은 로딩 스레드도 돌리는 Slate 시간으로 계산한다.
 */
class SVBLoadingScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVBLoadingScreen)
		: _BackgroundColor(FLinearColor::Black)
		, _TextColor(FLinearColor::White)
		, _FadeInSeconds(0.0f)
		, _ThrobberSpacing(0.0f)
	{}
		SLATE_ARGUMENT(FText, Text)
		SLATE_ARGUMENT(FSlateFontInfo, Font)
		SLATE_ARGUMENT(FLinearColor, BackgroundColor)
		SLATE_ARGUMENT(FLinearColor, TextColor)
		// 0 이면 처음부터 불투명(다른 화면을 이어받을 때).
		SLATE_ARGUMENT(float, FadeInSeconds)
		// 글, 도는 표시 묶음의 화면 가장자리 여백과, 도는 표시와 글 사이 간격.
		SLATE_ARGUMENT(FMargin, ContentPadding)
		SLATE_ARGUMENT(float, ThrobberSpacing)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// 흐려짐 0~1. SBorder 는 자기 바탕을 안 흐리므로 바탕과 글에 따로 곱한다.
	float GetFade() const;
	FLinearColor GetContentColor() const;
	FSlateColor GetBackgroundColor() const;

	FLinearColor BackgroundColor = FLinearColor::Black;
	double StartSeconds = 0.0;
	float FadeInSeconds = 0.0f;
};
