// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Narrative/Progress/VBChapterProgress.h"
#include "Narrative/Story/VBStoryStateBinding.h"
#include "VBTitleCardWidget.generated.h"

class AVBPlayerController;
class UTextBlock;
class UVBStoryStateComponent;

// 무엇을 알리는 카드인가. WBP 클래스 기본값(Source)이 정한다.
UENUM()
enum class EVBTitleCardSource : uint8
{
	None, // 스스로 듣지 않는다 - ShowCard 를 직접 부르는 쓰임
	Chapter, // 세계 사실의 챕터가 바뀌면
	Region, // 소유 PC 가 지역 진입을 알리면
};

/**
 * 잠깐 떴다 사라지는 제목 한 장. 챕터 카드와 지역 배너가 같이 쓴다.
 * Source 대로 구성될 때 스스로 신호를 듣는다. 시간은 실초다.
 */
UCLASS(Abstract)
class VB_API UVBTitleCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// bLarge = 처음 들어온 지역, 챕터처럼 크게 오래. 아니면 SmallScale 로 줄여 SmallSeconds 동안.
	void ShowCard(const FText& Title, const FText& Subtitle, bool bLarge);

	EVBTitleCardSource GetSource() const { return Source; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtTitle;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtSubtitle;

	UPROPERTY(EditDefaultsOnly, Category="VB|Progress", meta=(ClampMin="0.1", Units="s"))
	float LargeSeconds = 4.0f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Progress", meta=(ClampMin="0.1", Units="s"))
	float SmallSeconds = 2.0f;

	// 나타나고 사라지는 데 걸리는 시간(각각). 보이는 시간 안에 든다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Progress", meta=(ClampMin="0", Units="s"))
	float FadeSeconds = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Progress", meta=(ClampMin="0.1", ClampMax="1"))
	float SmallScale = 0.7f;

private:
	friend class FVBHUDRowTestAccess;

	void BindStory();
	void HandleStoryChanged();
	void HandleRegionEntered(FGameplayTag RegionId, bool bFirstVisit);

	double ShownAt = -1.0;
	float ShowSeconds = 0.0f;

	// 무엇을 듣는가. None = 스스로 듣지 않음. 실행 중에 바꾸지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	EVBTitleCardSource Source = EVBTitleCardSource::None;

	FVBStoryStateBinding StoryBinding;
	// 기준선을 잡은 세계 사실. 재구성 때 지우지 않아야 끊긴 동안 바뀐 챕터 카드가 뜬다.
	TWeakObjectPtr<UVBStoryStateComponent> BaselineStory;
	VBChapterProgress::FBaseline ChapterBaseline;
	FDelegateHandle RegionEnteredHandle;
	// 지역 신호를 건 PC. 풀 때 여기서 푼다.
	TWeakObjectPtr<AVBPlayerController> RegionSource;
	bool bWarnedNoStory = false;
};
