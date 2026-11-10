// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Settings/VBUserSettingsStore.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "SaveSystem/VBSaveGame.h"
#include "VB/VB.h"

namespace
{
	const FString GSettingsSlotName = TEXT("VBSettings");
	// 단일 로컬 플레이어 가정.
	constexpr int32 GSettingsUserIndex = 0;
}

UVBUserSettingsStore* UVBUserSettingsStore::Get(const UObject* WorldContextObject)
{
	if (!GEngine)
	{
		return nullptr;
	}

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UVBUserSettingsStore>() : nullptr;
}

void UVBUserSettingsStore::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 설정은 게임 시작 직후 어디서든 즉시 필요하므로 미리 보증해 둔다.
	LoadOrCreateSettings();
}

void UVBUserSettingsStore::Deinitialize()
{
	SettingsSave = nullptr;
	Super::Deinitialize();
}

UVBSaveGame* UVBUserSettingsStore::LoadOrCreateSettings()
{
	if (SettingsSave)
	{
		return SettingsSave;
	}

	if (Disk.Exists(GSettingsSlotName, GSettingsUserIndex))
	{
		SettingsSave = Cast<UVBSaveGame>(Disk.Read(GSettingsSlotName, GSettingsUserIndex));
		// 기본값으로 시작하면 다음 설정 저장이 이 파일을 덮으니 여기서 남긴다.
		VB_CLOG(!SettingsSave, Error, "설정 파일을 읽지 못했다 - 기본값으로 시작한다");
	}
	// 디스크에 없거나(첫 실행) 읽지 못했으면 기본값으로 새로 생성.
	if (!SettingsSave)
	{
		SettingsSave = Cast<UVBSaveGame>(UGameplayStatics::CreateSaveGameObject(UVBSaveGame::StaticClass()));
	}

	return SettingsSave;
}

const FVBSettingsSaveData& UVBUserSettingsStore::GetSettingsData(const UObject* WorldContextObject)
{
	// 저장소도 세이브도 없는 경로(에디터 위젯 프리뷰, GameInstance 초기화 전)의 기본값. 정적 상수라 수명이 참조보다 길다.
	static const FVBSettingsSaveData DefaultSettings;

	if (UVBUserSettingsStore* Store = Get(WorldContextObject))
	{
		if (const UVBSaveGame* Save = Store->LoadOrCreateSettings())
		{
			// SettingsSave 는 Deinitialize 전까지 교체되지 않는다 - 참조 안정.
			return Save->Settings;
		}
	}

	return DefaultSettings;
}

void UVBUserSettingsStore::UpdateSettings(const FVBSettingsSaveData& NewSettings)
{
	if (UVBSaveGame* Save = LoadOrCreateSettings())
	{
		Save->Settings = NewSettings;
		SaveSettings();
	}
}

void UVBUserSettingsStore::SaveSettings()
{
	UVBSaveGame* Save = LoadOrCreateSettings();
	Save->SaveVersion = UVBSaveGame::CurrentSaveVersion;
	Save->SaveTimestamp = FDateTime::UtcNow();

	// 설정은 작지만 저장 시점이 게임플레이 중일 수 있어 비동기로 히치를 피한다.
	UGameplayStatics::AsyncSaveGameToSlot(Save, GSettingsSlotName, GSettingsUserIndex, FAsyncSaveGameToSlotDelegate());
}
