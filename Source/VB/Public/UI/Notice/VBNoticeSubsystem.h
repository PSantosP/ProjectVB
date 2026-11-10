// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UI/Notice/VBNoticeFeed.h"
#include "VBNoticeSubsystem.generated.h"

/**
 * 화면 알림 줄을 관리한다(로컬 플레이어마다 하나). 생산자는 Push 만 부르고 UVBNoticeFeedWidget 이 그린다.
 * 시각은 실시간이라 일시정지 중에도 줄이 걷힌다.
 */
UCLASS()
class VB_API UVBNoticeSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	// 위젯, PC, 폰, 로컬 플레이어 무엇이든 받는다.
	static UVBNoticeSubsystem* Get(const UObject* WorldContextObject);

	void Push(FVBNoticeRequest Request);
	void PushText(EVBNoticeCategory Category, const FText& Text, FName MergeKey = NAME_None);

	// 시간이 다 된 줄을 걷어 낸 뒤의 목록. 걷힌 줄이 있으면 OnNoticesChanged 가 먼저 울린다.
	const TArray<FVBNoticeEntry>& GetEntries();

	double GetNow() const;

	// 줄이 생기거나 합쳐지거나 걷혔다.
	FSimpleMulticastDelegate OnNoticesChanged;

private:
	FVBNoticeFeed Feed;
};
