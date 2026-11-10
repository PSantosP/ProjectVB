// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBNoticeFeed.generated.h"

// 알림의 종류. 화면이 줄의 색을 고르는 데만 쓴다.
UENUM(BlueprintType)
enum class EVBNoticeCategory : uint8
{
	System,
	Item,
	Quest,
	Progression,
	Refusal,
};

// 알림 한 줄을 띄워 달라는 요청.
struct VB_API FVBNoticeRequest
{
	EVBNoticeCategory Category = EVBNoticeCategory::System;

	// 같은 키의 줄이 떠 있으면 그 줄에 합친다. None = 합치지 않는다.
	FName MergeKey;

	// 이 요청이 더하는 수(아이템 개수 등). 합치지 않는 줄은 1 이면 된다.
	int32 Count = 1;

	// 합친 수 -> 글. 합칠 때마다 다시 부른다. 빈 글이면 줄을 만들지 않는다.
	TFunction<FText(int32 Total)> MakeText;

	// 수를 쓰지 않는 고정 글 요청.
	static FVBNoticeRequest FromText(EVBNoticeCategory InCategory, const FText& Text, FName InMergeKey = NAME_None);
};

// 떠 있는 알림 한 줄.
struct FVBNoticeEntry
{
	uint32 Id = 0;
	EVBNoticeCategory Category = EVBNoticeCategory::System;
	FName MergeKey;
	int32 Count = 0;
	FText Text;
	double ExpiresAt = 0.0;
	TFunction<FText(int32 Total)> MakeText;
};

/** 알림 줄 목록의 순수 논리(합치기, 개수 상한, 만료). UVBNoticeSubsystem 이 관리하고, 화면은 읽기만 한다. */
struct VB_API FVBNoticeFeed
{
	// 줄을 더하거나 합친다. 목록이 바뀌었으면 참.
	bool Push(FVBNoticeRequest Request, double Now, double Lifetime, int32 MaxEntries);

	// 시간이 다 된 줄을 뺀다. 목록이 바뀌었으면 참.
	bool Prune(double Now);

	const TArray<FVBNoticeEntry>& GetEntries() const { return Entries; }

private:
	// 오래된 것이 앞이다.
	TArray<FVBNoticeEntry> Entries;
	uint32 NextId = 1;
};
