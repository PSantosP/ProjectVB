// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// 고를 수 있는 언어 하나. Name = 문화권 이름("ko", "en"), Label = 그 언어로 쓴 이름.
struct FVBCultureChoice
{
	FString Name;
	FText Label;
};

/** 게임 언어는 엔진 설정이 관리한다(게임 = GameUserSettings.ini, 에디터 = PIE 현지화 미리 보기). 그 분기는 여기에만 둔다. */
namespace VBCulture
{
	// 지금 게임 글의 언어(고를 수 있는 이름으로 맞춘 것).
	VB_API FString GetActive();

	// 기본값 - OS 언어에 맞는 것, 없으면 원문 언어.
	VB_API FString GetDefault();

	// 컴파일된 번역이 있는 언어 + 원문 언어. 원문이 맨 앞이다.
	VB_API TArray<FVBCultureChoice> GetSelectable();

	// 바꾸고 다음 시작 때도 쓰이게 남긴다.
	VB_API void Apply(const FString& Culture);
}
