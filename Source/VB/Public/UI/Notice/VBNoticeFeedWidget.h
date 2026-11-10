// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Text/TextLayout.h" // ETextJustify - 줄 정렬 칸
#include "Styling/SlateColor.h"
#include "UI/Notice/VBNoticeFeed.h"
#include "VBNoticeFeedWidget.generated.h"

class UPanelWidget;
class UTextBlock;
class UVBNoticeSubsystem;

/**
 * 알림 줄을 그린다(읽기만). 목록은 UVBNoticeSubsystem 이 관리한다.
 * 층들 위에 붙어 메뉴 위에도 보인다. 끝나 가는 줄은 FadeSeconds 동안 흐려진다.
 */
UCLASS()
class VB_API UVBNoticeFeedWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	// 구독은 Construct, Destruct 짝으로 한다(다시 붙일 때 OnInitialized 는 안 불린다).
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 줄이 쌓이는 상자(세로). 오래된 줄이 위다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UPanelWidget> EntryBox;

	// 끝나기 전 흐려지는 시간(실시간 초).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(ClampMin="0", Units="s"))
	float FadeSeconds = 0.4f;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(ClampMin="6"))
	int32 FontSize = 20;

	// 종류별 글 색. 없는 종류는 DefaultColor.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TMap<EVBNoticeCategory, FSlateColor> CategoryColors;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FSlateColor DefaultColor = FSlateColor(FLinearColor::White);

	// 줄 정렬, 그림자.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TEnumAsByte<ETextJustify::Type> LineJustification = ETextJustify::Center;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FVector2D ShadowOffset = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FLinearColor ShadowColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.8f);

private:
	void Rebuild(const TArray<FVBNoticeEntry>& Entries);

	TWeakObjectPtr<UVBNoticeSubsystem> Notices;
	FDelegateHandle ChangedHandle;
	bool bDirty = true;

	// 만든 줄. 줄 수가 줄면 감추고 다시 쓴다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> Lines;
};
