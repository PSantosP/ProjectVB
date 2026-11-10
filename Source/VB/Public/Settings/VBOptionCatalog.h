// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Math/Interval.h"
#include "Settings/VBCulture.h"
#include "Settings/VBOptionTypes.h"

struct FVBOptionsDraft;
class UVBOptionsSettings;

enum class EVBOptionKind : uint8
{
	Slider,
	Toggle,
	Choice,
	Action
};

// 옵션 화면의 한 줄. 화면은 종류만 보고 그리고, 값은 Get/Set 으로 초안에서 읽고 쓴다(켜기 = 0/1, 고르기 = 선택지 번호).
struct VB_API FVBOptionEntry
{
	FName Id;
	FText Label;
	EVBOptionKind Kind = EVBOptionKind::Toggle;
	TArray<FText> Choices;
	FFloatInterval Range = FFloatInterval(0.0f, 1.0f);
	float Step = 0.05f;
	bool bPercent = false;
	TFunction<float(const FVBOptionsDraft&)> Get;
	TFunction<void(FVBOptionsDraft&, float)> Set;
};

// 난이도 줄의 선택지 하나.
struct FVBDifficultyChoice
{
	FGameplayTag Tag;
	FText Label;
};

// 목록을 만들 때 쓰는 바깥 사실. 목록 함수가 엔진에 묻지 않도록 호출자가 채운다.
struct FVBOptionCatalogContext
{
	// 게임 쪽 난이도 담당 객체가 살아 있을 때만 채운다(메인 메뉴 = 빔 = 줄 없음). 원격 클라이언트는 현재 단계 하나만 넣고 bDifficultyHostOnly.
	TArray<FVBDifficultyChoice> Difficulties;
	bool bDifficultyHostOnly = false;
	const UVBOptionsSettings* Settings = nullptr;
	TArray<FIntPoint> Resolutions;
	TArray<FVBCultureChoice> Cultures;
	TArray<FVBResolutionPresetChoice> ResolutionPresets;
	// 화면을 열 때의 렌더 해상도 배율. 어느 프리셋과도 다르면(ini 를 손으로 고친 값, 또는 자동 감지가 정한 71, 87% 같은 값) "사용자 지정" 줄로 보이고 그것을 고르면 이 값으로 돌아간다.
	float CurrentResolutionQuality = 0.0f;
	// 모니터 주사율(FPlatformMisc::GetMaxRefreshRate). 프레임 제한 목록에 없으면 "N FPS (모니터)" 줄이 끼어든다 - 기본값이 그 값이라
	//   목록에 없으면 처음 연 화면이 아무 줄도 가리키지 못한다. 0 = 모름, 줄 없음.
	int32 MonitorRefreshRate = 0;
};

// 어떤 탭에 어떤 줄이 있는지의 유일한 홈. 순수 함수라 화면 없이 시험한다.
namespace VBOptionCatalog
{
	// Action 줄의 Id. 화면이 받아 처리한다.
	inline const FName KeyBindings(TEXT("KeyBindings"));
	inline const FName AutoDetect(TEXT("AutoDetect"));

	VB_API FText TabLabel(EVBOptionTab Tab);
	VB_API TArray<FVBOptionEntry> BuildTab(EVBOptionTab Tab, const FVBOptionCatalogContext& Context);

	// 패드의 이전/다음 탭. 숨은 탭은 건너뛰고 끝에서 처음으로 돈다. 보이는 탭이 없으면 INDEX_NONE.
	VB_API int32 StepVisibleTab(int32 Current, int32 Step, const TArray<bool>& Visible);
}
