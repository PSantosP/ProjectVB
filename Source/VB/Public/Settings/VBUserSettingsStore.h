// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SaveSystem/VBSaveDisk.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "VBUserSettingsStore.generated.h"

class UVBSaveGame;
struct FVBSettingsSaveData;

/**
 * 게임 슬롯과 무관한 사용자 설정의 저장 담당. 슬롯 "VBSettings"(UVBSaveGame 형식) 하나를 GameInstance 수명 동안 캐시한다.
 * 값의 범위 판정과 엔진 적용은 UVBOptionsSubsystem 이 맡는다.
 */
UCLASS()
class VB_API UVBUserSettingsStore : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UVBUserSettingsStore* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// 설정 세이브를 캐시에서 반환하거나, 디스크에서 로드하거나, 없으면 새로 만든다. 항상 유효 포인터.
	UVBSaveGame* LoadOrCreateSettings();

	// 설정 값을 읽는 유일한 경로. 저장소가 없어도 기본값 참조를 돌려준다(null 없음).
	//   참조라서 UpdateSettings 직후의 입력이 새 값을 본다.
	static const FVBSettingsSaveData& GetSettingsData(const UObject* WorldContextObject);

	// 설정 값을 바꾸고 영속화한다. 소비처가 매번 이 값을 읽으므로 갱신이 곧 적용이다.
	void UpdateSettings(const FVBSettingsSaveData& NewSettings);

	// 현재 설정을 슬롯에 비동기로 저장한다.
	void SaveSettings();

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBUserSettingsStoreTestAccess;
#endif
	FVBSaveDisk Disk = FVBSaveDisk::Platform();

	// LoadOrCreateSettings 가 보증하는 캐시.
	UPROPERTY()
	TObjectPtr<UVBSaveGame> SettingsSave;
};
