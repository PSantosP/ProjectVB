// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/VBKeyBindingRules.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "VBInputUserSettings.generated.h"

/**
 * 키 다시 지정 사용자 설정. 줄마다 어느 장치의 키인지 정해, 키보드, 패드 IMC 의 같은 줄 이름이 서로 덮어쓰지 않게 한다.
 * 설정 화면이 부르는 줄 읽기, 키 바꾸기, 장치별 기본값도 여기 둔다. 저장은 부르는 쪽이 한다.
 */
UCLASS()
class VB_API UVBInputUserSettings : public UEnhancedInputUserSettings
{
	GENERATED_BODY()

public:
	virtual FHardwareDeviceIdentifier DetermineHardwareDeviceForActionMapping(const FEnhancedActionKeyMapping& ActionMapping,
		const UInputMappingContext* IMC = nullptr) const override;

	// 지금 키 프로필에서 이 장치의 줄(등록 순서). 이름 있는 줄만 들어오고, 첫째 칸만 본다.
	TArray<FVBKeyBindingRow> GetRows(EVBKeyDevice Device) const;

	// 그 줄에 InNewKey 를 준다. 같은 장치에서 그 키를 쓰던 줄은 키 없음이 되고 OutCleared 로 돌려준다.
	// 받을 수 없는 키, 없는 줄, 칸 못 찾음, 결과 불일치면 아무것도 바꾸지 않고(되돌리고) false.
	bool RebindKey(FName MappingName, EVBKeyDevice Device, const FKey& InNewKey, TArray<FName>& OutCleared);

	// 이 장치의 줄을 전부 IMC 기본 키로 되돌린다.
	void ResetDeviceToDefault(EVBKeyDevice Device);

private:
	// 엔진은 줄을 "칸 + 장치 이름" 으로 찾으므로 첫째 칸을 꼭 적는다.
	static void FillArgs(FMapPlayerKeyArgs& Args, FName MappingName, EVBKeyDevice Device, const FKey& NewKey);
};

/**
 * 키 프로필. 매핑을 다시 지을 때 줄의 키를 기본 키 종류가 아니라 줄의 장치로 고른다.
 * 엔진 기본은 패드 줄의 키 없음을 걸러 기본 버튼이 그대로 먹는다.
 */
UCLASS()
class VB_API UVBPlayerMappableKeyProfile : public UEnhancedPlayerMappableKeyProfile
{
	GENERATED_BODY()

public:
	virtual int32 GetPlayerMappedKeysForRebuildControlMappings(const FEnhancedActionKeyMapping& DefaultMapping,
		TArray<FKey>& OutKeys) const override;
};
