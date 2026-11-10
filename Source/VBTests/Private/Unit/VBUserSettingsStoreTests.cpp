// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/GameInstance.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Settings/VBUserSettingsStore.h"
#include "UObject/StrongObjectPtr.h"

class FVBUserSettingsStoreTestAccess
{
public:
	static void SetReadDisk(UVBUserSettingsStore& Store, TFunction<bool(const FString&, int32)> Exists,
		TFunction<USaveGame*(const FString&, int32)> Read)
	{
		Store.Disk.Exists = MoveTemp(Exists);
		Store.Disk.Read = MoveTemp(Read);
	}
};

// 설정 저장소는 슬롯 하나를 캐시하고, 옛 API 는 저장소로 전달만 한다(자기 사본이 없다). 읽기만 하고 디스크에 쓰지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUserSettingsStoreCacheTest, "VB.Unit.Settings.UserStoreCache",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBUserSettingsStoreCacheTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
	const TStrongObjectPtr<UVBUserSettingsStore> Store(NewObject<UVBUserSettingsStore>(Instance.Get()));
	const TStrongObjectPtr<UVBSaveGameSubsystem> Save(NewObject<UVBSaveGameSubsystem>(Instance.Get()));

	const UVBSaveGame* First = Store->LoadOrCreateSettings();
	TestNotNull(TEXT("the store always gives a settings save"), First);
	TestTrue(TEXT("the store keeps one object"), Store->LoadOrCreateSettings() == First);
	// 초기화 안 된 GameInstance 에는 등록된 저장소가 없다 - 옛 API 가 자기 사본을 만들면 여기서 값이 나온다.
	TestNull(TEXT("the old API holds no settings of its own"), Save->LoadOrCreateSettings());
	return true;
}

// 설정 파일을 읽지 못하면 기본값으로 시작하되 그 사실을 남긴다. 실제 슬롯에는 닿지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUserSettingsStoreUnreadableTest, "VB.Unit.Settings.UserStoreUnreadable",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBUserSettingsStoreUnreadableTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
	const TStrongObjectPtr<UVBUserSettingsStore> Store(NewObject<UVBUserSettingsStore>(Instance.Get()));
	FVBUserSettingsStoreTestAccess::SetReadDisk(*Store.Get(), [](const FString&, int32) { return true; },
		[](const FString&, int32) -> USaveGame* { return nullptr; });
	AddExpectedError(TEXT("설정 파일을 읽지 못했다"), EAutomationExpectedErrorFlags::Contains, 1);

	TestNotNull(TEXT("the store still gives settings"), Store->LoadOrCreateSettings());
	return true;
}
#endif
