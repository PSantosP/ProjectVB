// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericWindow.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "Settings/VBOptionTypes.h"

class UGameUserSettings;
struct FVBMachineCommitPlan;

/**
 * 기기별 설정(화면, 그래픽)의 스냅샷. 원본은 엔진 UGameUserSettings 가 관리하고, 옵션 화면은 이 스냅샷을 고쳐서 돌려준다.
 */
struct VB_API FVBMachineSettings
{
	FIntPoint Resolution = FIntPoint::ZeroValue;
	EWindowMode::Type WindowMode = EWindowMode::Fullscreen;
	bool bVSync = false;
	float FrameRateLimit = 0.0f;
	TStaticArray<int32, static_cast<int32>(EVBQualityGroup::Count)> Quality{InPlace, 0};
	// 렌더 해상도 배율(%) = 업스케일링, 0 = 프로젝트 기본(자동). 창이 안 바뀌므로 ChangesVideoMode 에 넣지 않는다.
	float ResolutionQuality = 0.0f;

	static FVBMachineSettings Read(const UGameUserSettings& Settings);
	void Write(UGameUserSettings& Settings) const;

	int32 GetQuality(EVBQualityGroup Group) const { return Quality[static_cast<int32>(Group)]; }
	void SetQuality(EVBQualityGroup Group, int32 Level) { Quality[static_cast<int32>(Group)] = Level; }

	// 모든 그룹이 같은 단계면 그 단계, 섞였으면 INDEX_NONE(사용자 지정).
	int32 GetOverallQuality() const;
	void SetOverallQuality(int32 Level);

	// 해상도, 창 모드가 다르면 참 - 바꾸면 유지 여부를 물어야 한다.
	bool ChangesVideoMode(const FVBMachineSettings& Other) const;

	// 이 초안을 지금 값 Before 위에 확정할 때 할 일. 엔진을 건드리지 않는 값 계산이다.
	FVBMachineCommitPlan PlanCommit(const FVBMachineSettings& Before) const;

	bool operator==(const FVBMachineSettings& Other) const;
};

// 기기 설정 확정 한 번의 할 일.
struct FVBMachineCommitPlan
{
	// 바로 걸고 저장할 값. 해상도, 창 모드는 지금 값 그대로다.
	FVBMachineSettings Saved;
	// 해상도, 창 모드가 바뀌어 저장 없는 미리 보기로 걸어야 하면 true.
	bool bPreviewVideoMode = false;
	// 업스케일링 자동이라 이전 배율을 직접 되돌려야 하면 true.
	bool bResetScreenPercentage = false;
};

// 옵션 화면이 고치는 한 벌. 열 때 현재 값을 복사하고 적용 때 UVBOptionsSubsystem::Commit 에 통째로 전달한다.
struct VB_API FVBOptionsDraft
{
	FVBSettingsSaveData User;
	FVBMachineSettings Machine;
	// 게임 언어. 엔진 설정이 관리한다(VBCulture).
	FString Culture;
	// 이 게임의 난이도 단계(UVBDifficultyStateComponent 가 관리, 슬롯 공통 설정에 넣지 않는다). 빈 태그 = 다룰 게임 없음(메인 메뉴).
	FGameplayTag Difficulty;

	bool operator==(const FVBOptionsDraft& Other) const;
};
