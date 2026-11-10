#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/UnrealType.h"
#include "Settings/VBOptionCatalog.h"
#include "Settings/VBOptionRules.h"
#include "Settings/VBOptionsDraft.h"
#include "Settings/VBOptionsSettings.h"
#include "Settings/VBGameUserSettings.h"
#include "Engine/Engine.h"

// 옵션 항목 목록, 판정. 화면, 엔진 없이 순수 함수만 본다.

#if WITH_AUTOMATION_TESTS
namespace
{
	UVBOptionsSettings* MakeLimits()
	{
		UVBOptionsSettings* Limits = NewObject<UVBOptionsSettings>();
		Limits->UIScaleRange = FFloatInterval(0.8f, 1.5f);
		Limits->LookSensitivityRange = FFloatInterval(0.1f, 3.0f);
		Limits->FrameRateLimits = { 30.0f, 60.0f, 0.0f };
		FVBToggleableInput Sprint;
		Sprint.InputTag = VBGameplayTags::Input_Sprint;
		Limits->ToggleableInputs = { Sprint };
		return Limits;
	}

	FVBOptionCatalogContext MakeContext(const UVBOptionsSettings* Limits)
	{
		FVBOptionCatalogContext Context;
		Context.Settings = Limits;
		Context.Resolutions = { FIntPoint(1280, 720), FIntPoint(1920, 1080) };
		Context.Cultures = { { TEXT("ko"), FText::AsCultureInvariant(TEXT("한국어")) }, { TEXT("en"), FText::AsCultureInvariant(TEXT("English")) } };
		// 게임 중인 화면처럼 난이도 셋(메인 메뉴 = 빈 목록은 Options.Difficulty 가 따로 본다).
		for (const TCHAR* Name : { TEXT("Difficulty.Easy"), TEXT("Difficulty.Normal"), TEXT("Difficulty.Hard") })
		{
			Context.Difficulties.Add({ FGameplayTag::RequestGameplayTag(FName(Name), false), FText::AsCultureInvariant(Name) });
		}

		return Context;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBOptionCatalogTest, "VB.Unit.Options.Catalog",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBOptionCatalogTest::RunTest(const FString& Parameters)
{
	const UVBOptionsSettings* Limits = MakeLimits();
	const FVBOptionCatalogContext Context = MakeContext(Limits);

	FVBOptionsDraft Draft;
	Draft.Machine.Resolution = FIntPoint(1920, 1080);
	Draft.Machine.FrameRateLimit = 60.0f;
	Draft.Culture = TEXT("ko");
	// 줄이 있는 화면의 초안에는 늘 정규화된 단계가 있다(UVBDifficultyStateComponent 가 빈, 모르는 태그를 기본으로 바꿔 든다).
	Draft.Difficulty = Context.Difficulties[1].Tag;
	for (int32 TabIndex = 0; TabIndex < static_cast<int32>(EVBOptionTab::Count); ++TabIndex)
	{
		const EVBOptionTab Tab = static_cast<EVBOptionTab>(TabIndex);
		const TArray<FVBOptionEntry> Entries = VBOptionCatalog::BuildTab(Tab, Context);
		const FString TabName = VBOptionCatalog::TabLabel(Tab).ToString();
		TestTrue(FString::Printf(TEXT("tab %s has rows"), *TabName), Entries.Num() > 0);
		for (const FVBOptionEntry& Entry : Entries)
		{
			if (Entry.Kind == EVBOptionKind::Action)
			{
				continue;
			}

			const FString What = FString::Printf(TEXT("%s/%s"), *TabName, *Entry.Id.ToString());
			if (!TestTrue(What + TEXT(" has get and set"), Entry.Get && Entry.Set))
			{
				continue;
			}

			TestTrue(What + TEXT(" choice row has choices"), Entry.Kind != EVBOptionKind::Choice || Entry.Choices.Num() > 0);
			// 읽은 값을 그대로 쓰면 초안이 안 바뀐다.
			const FVBOptionsDraft Before = Draft;
			Entry.Set(Draft, Entry.Get(Draft));
			TestTrue(What + TEXT(" set(get) keeps the draft"), Draft == Before);
		}
	}

	const TArray<FVBOptionEntry> Controls = VBOptionCatalog::BuildTab(EVBOptionTab::Controls, Context);
	TestTrue(TEXT("controls list each toggleable input and the key bindings action"),
		Controls.ContainsByPredicate([](const FVBOptionEntry& E) { return E.Id == VBGameplayTags::Input_Sprint.GetTag().GetTagName(); })
		&& Controls.ContainsByPredicate([](const FVBOptionEntry& E) { return E.Id == VBOptionCatalog::KeyBindings; }));
	TestEqual(TEXT("resolution choices come from the context"),
		VBOptionCatalog::BuildTab(EVBOptionTab::Display, Context)[0].Choices.Num(), Context.Resolutions.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBOptionLanguageTest, "VB.Unit.Options.Language",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBOptionLanguageTest::RunTest(const FString& Parameters)
{
	const UVBOptionsSettings* Limits = MakeLimits();
	FVBOptionCatalogContext Context = MakeContext(Limits);
	const FVBOptionEntry Language = VBOptionCatalog::BuildTab(EVBOptionTab::Subtitles, Context)[0];
	TestEqual(TEXT("language is the first row of its tab"), Language.Id, FName(TEXT("Language")));
	TestEqual(TEXT("one choice per selectable culture"), Language.Choices.Num(), Context.Cultures.Num());

	FVBOptionsDraft Draft;
	Draft.Culture = TEXT("ko");
	Language.Set(Draft, 1.0f);
	TestEqual(TEXT("choosing the second row picks its culture"), Draft.Culture, FString(TEXT("en")));
	TestEqual(TEXT("reads back its index"), Language.Get(Draft), 1.0f);
	TestFalse(TEXT("a culture change makes the draft differ"), Draft == FVBOptionsDraft());

	Context.Cultures.Reset();
	const TArray<FVBOptionEntry> NoCultures = VBOptionCatalog::BuildTab(EVBOptionTab::Subtitles, Context);
	TestFalse(TEXT("no row without selectable cultures"),
		NoCultures.ContainsByPredicate([](const FVBOptionEntry& E) { return E.Id == TEXT("Language"); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBOptionQualityTest, "VB.Unit.Options.OverallQuality",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBOptionQualityTest::RunTest(const FString& Parameters)
{
	const UVBOptionsSettings* Limits = MakeLimits();
	const TArray<FVBOptionEntry> Graphics = VBOptionCatalog::BuildTab(EVBOptionTab::Graphics, MakeContext(Limits));
	const FVBOptionEntry& Overall = Graphics[0];
	// 줄 번호가 아니라 Id 로 찾는다 - 그룹 사이에 다른 줄(업스케일링)이 끼어도 시험이 엉뚱한 줄을 잡지 않게.
	const FName ShadowId(TEXT("Quality"), 1 + static_cast<int32>(EVBQualityGroup::Shadow));
	const FVBOptionEntry* ShadowRow = Graphics.FindByPredicate([&ShadowId](const FVBOptionEntry& Entry) { return Entry.Id == ShadowId; });
	if (!TestNotNull(TEXT("shadow row"), ShadowRow))
	{
		return false;
	}

	const FVBOptionEntry& Shadow = *ShadowRow;

	FVBOptionsDraft Draft;
	Overall.Set(Draft, 2.0f);
	TestEqual(TEXT("overall sets every group"), Draft.Machine.GetQuality(EVBQualityGroup::Texture), 2);
	TestEqual(TEXT("overall reads back"), Overall.Get(Draft), 2.0f);

	Shadow.Set(Draft, 4.0f);
	const float CustomIndex = static_cast<float>(Overall.Choices.Num() - 1);
	TestEqual(TEXT("one different group reads as custom"), Overall.Get(Draft), CustomIndex);
	Overall.Set(Draft, CustomIndex);
	TestEqual(TEXT("choosing custom changes nothing"), Draft.Machine.GetQuality(EVBQualityGroup::Shadow), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBOptionUpscalingTest, "VB.Unit.Options.Upscaling",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBOptionUpscalingTest::RunTest(const FString& Parameters)
{
	// 엔진 BaseScalability 의 프리셋 다섯과 같은 모양.
	const UVBOptionsSettings* Limits = MakeLimits();
	FVBOptionCatalogContext Context = MakeContext(Limits);
	Context.ResolutionPresets = { { TEXT("Default"), 0.0f }, { TEXT("Performance"), 50.0f }, { TEXT("Balanced"), 58.328f },
		{ TEXT("Quality"), 66.662f }, { TEXT("Native"), 100.0f } };
	auto FindUpscaling = [](const TArray<FVBOptionEntry>& Rows) { return Rows.FindByPredicate([](const FVBOptionEntry& Entry) { return Entry.Id == TEXT("Upscaling"); }); };

	// 지금 값이 프리셋이면 선택지 = 프리셋 다섯, 고르면 그 배율이 초안에 들어간다.
	Context.CurrentResolutionQuality = 100.0f;
	const TArray<FVBOptionEntry> Graphics = VBOptionCatalog::BuildTab(EVBOptionTab::Graphics, Context);
	const FVBOptionEntry* Upscaling = FindUpscaling(Graphics);
	if (!TestNotNull(TEXT("upscaling row"), Upscaling))
	{
		return false;
	}

	const FName AntiAliasingId(TEXT("Quality"), 1 + static_cast<int32>(EVBQualityGroup::AntiAliasing));
	const int32 AntiAliasingIndex = Graphics.IndexOfByPredicate([&AntiAliasingId](const FVBOptionEntry& Entry) { return Entry.Id == AntiAliasingId; });
	TestTrue(TEXT("upscaling sits right after anti-aliasing quality"), Graphics.IsValidIndex(AntiAliasingIndex + 1)
		&& &Graphics[AntiAliasingIndex + 1] == Upscaling);
	TestEqual(TEXT("one choice per preset"), Upscaling->Choices.Num(), 5);

	FVBOptionsDraft Draft;
	Draft.Machine.ResolutionQuality = 100.0f;
	TestEqual(TEXT("native reads as its index"), Upscaling->Get(Draft), 4.0f);
	Upscaling->Set(Draft, 1.0f);
	TestEqual(TEXT("performance writes 50%"), Draft.Machine.ResolutionQuality, 50.0f);
	Upscaling->Set(Draft, 0.0f);
	TestEqual(TEXT("auto writes 0 (project default ratio)"), Draft.Machine.ResolutionQuality, 0.0f);

	// 프리셋이 아닌 값(자동 감지의 71%)은 사용자 지정 줄로 보이고, 고르면 그 값으로 돌아간다.
	Context.CurrentResolutionQuality = 71.0f;
	const TArray<FVBOptionEntry> Custom = VBOptionCatalog::BuildTab(EVBOptionTab::Graphics, Context);
	const FVBOptionEntry* CustomRow = FindUpscaling(Custom);
	if (!TestNotNull(TEXT("upscaling row (custom)"), CustomRow))
	{
		return false;
	}

	TestEqual(TEXT("custom adds one choice"), CustomRow->Choices.Num(), 6);
	Draft.Machine.ResolutionQuality = 71.0f;
	TestEqual(TEXT("custom value reads as the last choice"), CustomRow->Get(Draft), 5.0f);
	CustomRow->Set(Draft, 2.0f);
	CustomRow->Set(Draft, 5.0f);
	TestEqual(TEXT("choosing custom restores the original value"), Draft.Machine.ResolutionQuality, 71.0f);

	// 초안 비교: 배율만 달라도 적용 안 한 변경이다(버리고 나갈까를 묻는 근거).
	FVBOptionsDraft Other = Draft;
	Other.Machine.ResolutionQuality = 50.0f;
	TestFalse(TEXT("resolution scale change makes the draft dirty"), Draft == Other);
	TestFalse(TEXT("resolution scale alone does not ask for video-mode confirm"), Draft.Machine.ChangesVideoMode(Other.Machine));
	return true;
}

// 기기 설정 확정 계획: 화면 모드는 저장 값에서 빠지고 미리 보기로만 바뀐다. 나머지는 초안 그대로 저장한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBOptionCommitPlanTest, "VB.Unit.Options.CommitPlan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBOptionCommitPlanTest::RunTest(const FString& Parameters)
{
	FVBMachineSettings Before;
	Before.Resolution = FIntPoint(1920, 1080);
	Before.WindowMode = EWindowMode::Fullscreen;
	Before.ResolutionQuality = 100.0f;

	FVBMachineSettings Draft = Before;
	Draft.bVSync = !Before.bVSync;
	const FVBMachineCommitPlan SameMode = Draft.PlanCommit(Before);
	TestFalse(TEXT("same video mode needs no preview"), SameMode.bPreviewVideoMode);
	TestTrue(TEXT("other settings are saved as drafted"), SameMode.Saved == Draft);
	TestFalse(TEXT("a fixed scale keeps the screen percentage"), SameMode.bResetScreenPercentage);

	Draft.Resolution = FIntPoint(1280, 720);
	Draft.WindowMode = EWindowMode::Windowed;
	Draft.ResolutionQuality = 0.0f;
	const FVBMachineCommitPlan NewMode = Draft.PlanCommit(Before);
	TestTrue(TEXT("a new video mode goes through preview"), NewMode.bPreviewVideoMode);
	TestTrue(TEXT("saved resolution stays the current one"), NewMode.Saved.Resolution == Before.Resolution);
	TestTrue(TEXT("saved window mode stays the current one"), NewMode.Saved.WindowMode == Before.WindowMode);
	TestEqual(TEXT("scale is saved as drafted"), NewMode.Saved.ResolutionQuality, 0.0f);
	TestTrue(TEXT("auto scale resets the old percentage"), NewMode.bResetScreenPercentage);
	return true;
}

// 난이도 줄: 게임이 없으면 줄이 없고, 고르면 초안의 태그가 바뀌어 적용이 켜지며,
//   원격 클라는 지금 단계 하나만 보여 바꿀 수 없다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBOptionDifficultyTest, "VB.Unit.Options.Difficulty",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBOptionDifficultyTest::RunTest(const FString& Parameters)
{
	const UVBOptionsSettings* Limits = MakeLimits();
	// 처치 후 다음 대상 줄은 플레이어 설정이라 늘 첫 줄이다 - 메인 메뉴에도 있고, 난이도는 게임이 있을 때 그 뒤에 온다.
	FVBOptionCatalogContext Menu = MakeContext(Limits);
	Menu.Difficulties.Reset();
	const TArray<FVBOptionEntry> MenuRows = VBOptionCatalog::BuildTab(EVBOptionTab::Gameplay, Menu);
	// 미니맵 줄도 플레이어 설정이라 메인 메뉴에 있다. 난이도 줄은 위치가 아니라 ID 로 찾는다 - 플레이어 줄이 늘어도 시험이 안 흔들린다.
	if (TestEqual(TEXT("main menu: auto-target, minimap and blood display"), MenuRows.Num(), 3))
	{
		TestTrue(TEXT("main menu first row is the auto-target toggle"), MenuRows[0].Id == FName(TEXT("AutoTargetNext")));
		FVBOptionsDraft ToggleDraft;
		TestTrue(TEXT("auto-target defaults on"), MenuRows[0].Get(ToggleDraft) > 0.5f);
		MenuRows[0].Set(ToggleDraft, 0.0f);
		TestFalse(TEXT("turning it off writes the player setting"), ToggleDraft.User.bAutoTargetNextOnKill);

		TestTrue(TEXT("main menu second row is the minimap toggle"), MenuRows[1].Id == FName(TEXT("ShowMinimap")));
		TestTrue(TEXT("minimap defaults on"), MenuRows[1].Get(ToggleDraft) > 0.5f);
		MenuRows[1].Set(ToggleDraft, 0.0f);
		TestFalse(TEXT("turning the minimap off writes the player setting"), ToggleDraft.User.bShowMinimap);
	}

	const auto FindRow = [](const TArray<FVBOptionEntry>& InRows, const TCHAR* Id)
	{
		return InRows.FindByPredicate([Id](const FVBOptionEntry& Row) { return Row.Id == FName(Id); });
	};
	const FVBOptionCatalogContext InGame = MakeContext(Limits);
	const TArray<FVBOptionEntry> Rows = VBOptionCatalog::BuildTab(EVBOptionTab::Gameplay, InGame);
	if (!TestEqual(TEXT("in game: three player rows + difficulty row"), Rows.Num(), 4))
	{
		return false;
	}

	const FVBOptionEntry* DifficultyRowPtr = FindRow(Rows, TEXT("Difficulty"));
	if (!TestNotNull(TEXT("difficulty row"), DifficultyRowPtr))
	{
		return false;
	}

	const FVBOptionEntry& DifficultyRow = *DifficultyRowPtr;
	TestEqual(TEXT("three choices"), DifficultyRow.Choices.Num(), 3);

	FVBOptionsDraft Draft;
	Draft.Difficulty = InGame.Difficulties[1].Tag;
	const FVBOptionsDraft Committed = Draft;
	TestEqual(TEXT("row reads the draft's level"), FMath::RoundToInt(DifficultyRow.Get(Draft)), 1);
	DifficultyRow.Set(Draft, 2.0f);
	TestTrue(TEXT("choosing Hard writes the tag"), Draft.Difficulty == InGame.Difficulties[2].Tag);
	TestFalse(TEXT("a changed level makes the draft dirty (Apply enabled)"), Draft == Committed);

	FVBOptionCatalogContext Remote = MakeContext(Limits);
	Remote.Difficulties = { InGame.Difficulties[1] };
	Remote.bDifficultyHostOnly = true;
	const TArray<FVBOptionEntry> RemoteRows = VBOptionCatalog::BuildTab(EVBOptionTab::Gameplay, Remote);
	const FVBOptionEntry* RemoteDifficulty = FindRow(RemoteRows, TEXT("Difficulty"));
	if (TestEqual(TEXT("remote client: four rows"), RemoteRows.Num(), 4) && TestNotNull(TEXT("remote difficulty row"), RemoteDifficulty))
	{
		TestEqual(TEXT("remote client: only the current level to pick"), RemoteDifficulty->Choices.Num(), 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBOptionRulesTest, "VB.Unit.Options.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBOptionRulesTest::RunTest(const FString& Parameters)
{
	const UVBOptionsSettings* Limits = MakeLimits();

	FVBSettingsSaveData Wild;
	Wild.MasterVolume = 3.0f;
	Wild.CameraShakeScale = -1.0f;
	Wild.UIScale = 9.0f;
	Wild.LookSensitivity = 0.0f;
	Wild.ToggleInputs.AddTag(VBGameplayTags::Input_Sprint);
	Wild.ToggleInputs.AddTag(VBGameplayTags::Input_Crouch);
	const FVBSettingsSaveData Clean = VBOptionRules::Sanitize(Wild, *Limits);
	TestEqual(TEXT("volume clamps to 1"), Clean.MasterVolume, 1.0f);
	TestEqual(TEXT("shake clamps to 0"), Clean.CameraShakeScale, 0.0f);
	TestEqual(TEXT("ui scale clamps to range"), Clean.UIScale, 1.5f);
	TestEqual(TEXT("sensitivity clamps to range"), Clean.LookSensitivity, 0.1f);
	TestTrue(TEXT("toggleable input kept"), Clean.ToggleInputs.HasTagExact(VBGameplayTags::Input_Sprint));
	TestFalse(TEXT("input that is not offered is dropped"), Clean.ToggleInputs.HasTagExact(VBGameplayTags::Input_Crouch));

	TestTrue(TEXT("ability toggle stays toggle"), VBOptionRules::IsToggleInput(VBGameplayTags::Input_Crouch, true, FVBSettingsSaveData()));
	TestFalse(TEXT("hold by default"), VBOptionRules::IsToggleInput(VBGameplayTags::Input_Sprint, false, FVBSettingsSaveData()));
	TestTrue(TEXT("user toggle"), VBOptionRules::IsToggleInput(VBGameplayTags::Input_Sprint, false, Clean));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBOptionBloodDisplayTest, "VB.Unit.Options.BloodDisplay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBOptionBloodDisplayTest::RunTest(const FString& Parameters)
{
	const UVBOptionsSettings* Limits = MakeLimits();
	FVBOptionCatalogContext Context = MakeContext(Limits);
	Context.Difficulties.Reset();
	const TArray<FVBOptionEntry> Rows = VBOptionCatalog::BuildTab(EVBOptionTab::Gameplay, Context);
	const FVBOptionEntry* Row = Rows.FindByPredicate([](const FVBOptionEntry& Entry) { return Entry.Id == TEXT("BloodDisplay"); });
	if (!TestNotNull(TEXT("blood display is offered on Gameplay without a game world"), Row))
	{
		return false;
	}

	TestTrue(TEXT("blood display is a choice row"), Row->Kind == EVBOptionKind::Choice);
	TestEqual(TEXT("three blood display choices"), Row->Choices.Num(), 3);

	FVBOptionsDraft Draft;
	const FVBOptionsDraft Defaults = Draft;
	TestTrue(TEXT("blood display defaults to Normal"), Draft.User.BloodDisplay == EVBBloodDisplay::Normal);
	TestEqual(TEXT("Normal reads as index zero"), Row->Get(Draft), 0.0f);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Row->Set(Draft, static_cast<float>(Index));
		TestTrue(TEXT("choice index writes the saved enum"), Draft.User.BloodDisplay == static_cast<EVBBloodDisplay>(Index));
		TestEqual(TEXT("choice reads the written index"), Row->Get(Draft), static_cast<float>(Index));
		TestEqual(TEXT("blood display participates in draft dirty detection"), Draft == Defaults, Index == 0);
		TestTrue(TEXT("sanitize preserves each offered mode"),
			VBOptionRules::Sanitize(Draft.User, *Limits).BloodDisplay == Draft.User.BloodDisplay);
	}

	Row->Set(Draft, -1.0f);
	TestTrue(TEXT("negative choice clamps to Normal"), Draft.User.BloodDisplay == EVBBloodDisplay::Normal);
	Row->Set(Draft, 99.0f);
	TestTrue(TEXT("large choice clamps to Off"), Draft.User.BloodDisplay == EVBBloodDisplay::Off);
	for (const int32 Invalid : { 3, 127, 255 })
	{
		Draft.User.BloodDisplay = static_cast<EVBBloodDisplay>(Invalid);
		TestTrue(TEXT("invalid saved enum sanitizes to Normal"),
			VBOptionRules::Sanitize(Draft.User, *Limits).BloodDisplay == EVBBloodDisplay::Normal);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBOptionBloodDisplaySaveTest, "VB.Unit.Options.BloodDisplaySave",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBOptionBloodDisplaySaveTest::RunTest(const FString& Parameters)
{
	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	for (const EVBBloodDisplay Mode : { EVBBloodDisplay::Normal, EVBBloodDisplay::Alternate, EVBBloodDisplay::Off })
	{
		Save->Settings.BloodDisplay = Mode;
		TArray<uint8> Bytes;
		if (!TestTrue(TEXT("settings save serializes to memory"), UGameplayStatics::SaveGameToMemory(Save, Bytes)))
		{
			return false;
		}

		const UVBSaveGame* Loaded = Cast<UVBSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
		if (!TestNotNull(TEXT("settings save reloads"), Loaded))
		{
			return false;
		}

		TestTrue(TEXT("save round trip preserves blood display"), Loaded->Settings.BloodDisplay == Mode);
	}

	// 저장 API 와 같은 태그 직렬화로 쓰되 새 칸만 빼서 옛 저장 파일을 흉내 낸다.
	struct FLegacySettingsWriter : FObjectAndNameAsStringProxyArchive
	{
		explicit FLegacySettingsWriter(FArchive& Inner) : FObjectAndNameAsStringProxyArchive(Inner, false) { ArNoDelta = true; }
		mutable bool bOmittedBloodDisplay = false;
		virtual bool ShouldSkipProperty(const FProperty* Property) const override
		{
			if (Property->GetOwnerStruct() == FVBSettingsSaveData::StaticStruct() && Property->GetFName() == TEXT("BloodDisplay"))
			{
				bOmittedBloodDisplay = true;
				return true;
			}

			return FObjectAndNameAsStringProxyArchive::ShouldSkipProperty(Property);
		}
	};
	Save->Settings.BloodDisplay = EVBBloodDisplay::Off;
	Save->Settings.VoiceVolume = 0.37f;
	TArray<uint8> LegacyBytes;
	FMemoryWriter Writer(LegacyBytes, true);
	FLegacySettingsWriter LegacyWriter(Writer);
	Save->Serialize(LegacyWriter);
	TestTrue(TEXT("legacy payload actually excludes BloodDisplay"), LegacyWriter.bOmittedBloodDisplay);
	TestFalse(TEXT("legacy payload writes without errors"), LegacyWriter.IsError());

	UVBSaveGame* LegacyLoaded = NewObject<UVBSaveGame>();
	FMemoryReader Reader(LegacyBytes, true);
	FObjectAndNameAsStringProxyArchive LegacyReader(Reader, true);
	LegacyLoaded->Serialize(LegacyReader);
	TestFalse(TEXT("legacy payload reads without errors"), LegacyReader.IsError());
	TestEqual(TEXT("legacy sibling property survives"), LegacyLoaded->Settings.VoiceVolume, Save->Settings.VoiceVolume);
	TestTrue(TEXT("missing blood display field keeps Normal"), LegacyLoaded->Settings.BloodDisplay == EVBBloodDisplay::Normal);
	TestEqual(TEXT("legacy load needs no SaveVersion bump"), LegacyLoaded->SaveVersion, UVBSaveGame::CurrentSaveVersion);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBOptionBloodPaletteTest, "VB.Unit.Options.BloodPalette",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBOptionBloodPaletteTest::RunTest(const FString& Parameters)
{
	const UVBOptionsSettings* Settings = GetDefault<UVBOptionsSettings>();
	const TMap<FName, FLinearColor> Expected = {
		{ TEXT("User.Spray Color"), FLinearColor(0.35f, 0.55f, 0.9f, 1.0f) },
		{ TEXT("User.Mist Color"), FLinearColor(0.4f, 0.55f, 0.8f, 1.0f) },
		{ TEXT("User.Streak Color"), FLinearColor(1.4f, 1.7f, 2.2f, 1.0f) },
		{ TEXT("User.Wound Color"), FLinearColor(0.35f, 0.55f, 0.9f, 1.0f) }
	};
	TestEqual(TEXT("ini supplies four alternate blood parameter keys"), Settings->AlternateBloodColors.Num(), Expected.Num());
	for (const TPair<FName, FLinearColor>& Pair : Expected)
	{
		const FLinearColor* Actual = Settings->AlternateBloodColors.Find(Pair.Key);
		if (TestNotNull(FString::Printf(TEXT("ini palette contains %s"), *Pair.Key.ToString()), Actual))
		{
			TestTrue(TEXT("ini uses the selected cool-light color including HDR streak"), Actual->Equals(Pair.Value));
		}
	}

	TestTrue(TEXT("alternate decal color matches the selected spray tint"),
		Settings->AlternateBloodDecalColor.Equals(FLinearColor(0.35f, 0.55f, 0.9f, 1.0f)));
	return true;
}

// 프레임 제한: 메뉴 제한, 첫 값 규칙, 모니터 주사율 줄, 엔진이 우리 기계 설정 클래스를 만드는지.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBOptionFrameRateTest, "VB.Unit.Options.FrameRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBOptionFrameRateTest::RunTest(const FString& Parameters)
{
	using namespace VBOptionRules;
	TestEqual(TEXT("outside a menu the user limit stands"), CombineFrameRateLimits(144.0f, {}), 144.0f);
	TestEqual(TEXT("menu caps an unlimited user"), CombineFrameRateLimits(0.0f, { 60.0f }), 60.0f);
	TestEqual(TEXT("menu caps a higher user limit"), CombineFrameRateLimits(144.0f, { 60.0f }), 60.0f);
	TestEqual(TEXT("a lower user limit survives the menu"), CombineFrameRateLimits(30.0f, { 60.0f }), 30.0f);
	TestEqual(TEXT("menu limit 0 = off"), CombineFrameRateLimits(0.0f, { 0.0f }), 0.0f);
	TestEqual(TEXT("default = monitor refresh"), DefaultFrameRateLimit(144), 144.0f);
	TestEqual(TEXT("unknown refresh falls back to 60, never unlimited"), DefaultFrameRateLimit(0), 60.0f);
	TestEqual(TEXT("negative refresh falls back to 60"), DefaultFrameRateLimit(-1), 60.0f);

	// 목록(30/60/무제한)에 없는 주사율은 숫자 자리에 끼고, 있는 주사율은 줄을 더하지 않는다.
	const UVBOptionsSettings* Limits = MakeLimits();
	auto FindLimitRow = [](const TArray<FVBOptionEntry>& Rows) { return Rows.FindByPredicate([](const FVBOptionEntry& Entry) { return Entry.Id == FName(TEXT("FrameRateLimit")); }); };
	FVBOptionCatalogContext Context = MakeContext(Limits);
	Context.MonitorRefreshRate = 165;
	const TArray<FVBOptionEntry> Display = VBOptionCatalog::BuildTab(EVBOptionTab::Display, Context);
	const FVBOptionEntry* Row = FindLimitRow(Display);
	if (!TestNotNull(TEXT("frame limit row"), Row))
	{
		return false;
	}

	TestEqual(TEXT("monitor choice added"), Row->Choices.Num(), 4);
	TestTrue(TEXT("monitor choice sits before unlimited"), Row->Choices.IsValidIndex(2)
		&& Row->Choices[2].ToString().Contains(TEXT("165")));
	FVBOptionsDraft Draft;
	Draft.Machine.FrameRateLimit = 165.0f;
	TestEqual(TEXT("a monitor-rate draft points at the monitor row"), Row->Get(Draft), 2.0f);
	Row->Set(Draft, 3.0f);
	TestEqual(TEXT("last row is unlimited"), Draft.Machine.FrameRateLimit, 0.0f);

	Context.MonitorRefreshRate = 60;
	const TArray<FVBOptionEntry> ListedDisplay = VBOptionCatalog::BuildTab(EVBOptionTab::Display, Context);
	const FVBOptionEntry* Listed = FindLimitRow(ListedDisplay);
	TestTrue(TEXT("a listed refresh adds no row"), Listed && Listed->Choices.Num() == 3);

	// 기계 설정 클래스가 우리 것이 아니면 메뉴 제한, 첫 값이 통째로 없다(DefaultEngine.ini 한 줄).
	TestEqual(TEXT("engine builds UVBGameUserSettings"), GEngine ? GEngine->GameUserSettingsClassName.ToString() : FString(),
		FString(TEXT("/Script/VB.VBGameUserSettings")));
	TestNotNull(TEXT("live machine settings are ours"), UVBGameUserSettings::Get());
	return true;
}

// 패드의 이전/다음 탭(LB/RB): 숨은 탭을 건너뛰고 끝에서 돈다 - 패드로도 탭을 고를 수 있어야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBOptionTabStepTest, "VB.Unit.Options.TabStep",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBOptionTabStepTest::RunTest(const FString& Parameters)
{
	// 메인 메뉴처럼 마지막 탭(게임플레이)이 숨은 7칸.
	const TArray<bool> Visible = { true, true, true, true, true, true, false };
	TestEqual(TEXT("next moves one tab"), VBOptionCatalog::StepVisibleTab(0, 1, Visible), 1);
	TestEqual(TEXT("previous from the first wraps to the last visible"), VBOptionCatalog::StepVisibleTab(0, -1, Visible), 5);
	TestEqual(TEXT("next from the last visible skips the hidden tab and wraps"), VBOptionCatalog::StepVisibleTab(5, 1, Visible), 0);
	const TArray<bool> Gap = { true, false, true };
	TestEqual(TEXT("a hidden tab in the middle is skipped"), VBOptionCatalog::StepVisibleTab(0, 1, Gap), 2);
	TestEqual(TEXT("one visible tab stays put"), VBOptionCatalog::StepVisibleTab(0, 1, { true, false }), 0);
	TestEqual(TEXT("no visible tab = none"), VBOptionCatalog::StepVisibleTab(0, 1, { false, false }), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("zero step = none"), VBOptionCatalog::StepVisibleTab(0, 0, Visible), static_cast<int32>(INDEX_NONE));
	return true;
}

#endif
