// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBOptionTypes.generated.h"

// 옵션 화면의 탭. 선언 순서 = 화면 순서.
UENUM()
enum class EVBOptionTab : uint8
{
	Display,
	Graphics,
	Audio,
	Subtitles,
	Accessibility,
	Controls,
	// 이 게임의 설정(난이도). 게임 월드에 난이도 담당 객체가 있을 때만 줄이 있다 - 메인 메뉴에서는 탭이 숨는다.
	Gameplay,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EVBSubtitleSize : uint8
{
	Small,
	Medium,
	Large
};

UENUM(BlueprintType)
enum class EVBBloodDisplay : uint8
{
	Normal,
	Alternate,
	Off
};

// 엔진 확장성 그룹(UGameUserSettings 의 Get/Set*Quality 열 쌍). 선언 순서 = 그래픽 탭의 줄 순서.
enum class EVBQualityGroup : uint8
{
	ViewDistance,
	AntiAliasing,
	Shadow,
	GlobalIllumination,
	Reflection,
	PostProcess,
	Texture,
	Effects,
	Foliage,
	Shading,
	Count
};

// 업스케일링 선택지 하나 = 엔진 해상도 프리셋(Scalability::FResolutionPreset 의 이름, 배율). 0 = 자동(프로젝트 기본 비율).
struct FVBResolutionPresetChoice
{
	FName Name;
	float ResolutionQuality = 0.0f;
};
