// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Settings/VBOptionCatalog.h"

#include "Input/VBInputModifiers.h"
#include "Settings/VBOptionsDraft.h"
#include "Settings/VBOptionsSettings.h"

#define LOCTEXT_NAMESPACE "VBOptions"

namespace
{
	FVBOptionEntry MakeEntry(FName Id, const FText& Label, EVBOptionKind Kind)
	{
		FVBOptionEntry Entry;
		Entry.Id = Id;
		Entry.Label = Label;
		Entry.Kind = Kind;
		return Entry;
	}

	FVBOptionEntry UserToggle(FName Id, const FText& Label, bool FVBSettingsSaveData::* Field)
	{
		FVBOptionEntry Entry = MakeEntry(Id, Label, EVBOptionKind::Toggle);
		Entry.Get = [Field](const FVBOptionsDraft& Draft) { return Draft.User.*Field ? 1.0f : 0.0f; };
		Entry.Set = [Field](FVBOptionsDraft& Draft, float Value) { Draft.User.*Field = Value >= 0.5f; };
		return Entry;
	}

	FVBOptionEntry UserSlider(FName Id, const FText& Label, float FVBSettingsSaveData::* Field, const FFloatInterval& Range, bool bPercent)
	{
		FVBOptionEntry Entry = MakeEntry(Id, Label, EVBOptionKind::Slider);
		Entry.Range = Range;
		Entry.bPercent = bPercent;
		Entry.Get = [Field](const FVBOptionsDraft& Draft) { return Draft.User.*Field; };
		Entry.Set = [Field, Range](FVBOptionsDraft& Draft, float Value) { Draft.User.*Field = FMath::Clamp(Value, Range.Min, Range.Max); };
		return Entry;
	}

	// 선택지 행. 번호는 여기서 선택지 범위로 자른 뒤 전달한다 - 쓰는 쪽은 유효한 번호만 받는다.
	FVBOptionEntry Choice(FName Id, const FText& Label, TArray<FText> Choices,
		TFunction<int32(const FVBOptionsDraft&)> GetIndex, TFunction<void(FVBOptionsDraft&, int32)> SetIndex)
	{
		FVBOptionEntry Entry = MakeEntry(Id, Label, EVBOptionKind::Choice);
		const int32 Last = FMath::Max(0, Choices.Num() - 1);
		Entry.Choices = MoveTemp(Choices);
		Entry.Get = [GetIndex](const FVBOptionsDraft& Draft) { return static_cast<float>(FMath::Max(0, GetIndex(Draft))); };
		Entry.Set = [SetIndex, Last](FVBOptionsDraft& Draft, float Value)
		{
			SetIndex(Draft, FMath::Clamp(FMath::RoundToInt(Value), 0, Last));
		};
		return Entry;
	}

	TArray<FText> QualityLevels()
	{
		return { LOCTEXT("QualityLow", "낮음"), LOCTEXT("QualityMedium", "보통"), LOCTEXT("QualityHigh", "높음"),
			LOCTEXT("QualityEpic", "최고"), LOCTEXT("QualityCinematic", "시네마틱") };
	}

	FText QualityGroupLabel(EVBQualityGroup Group)
	{
		switch (Group)
		{
		case EVBQualityGroup::ViewDistance: return LOCTEXT("ViewDistance", "시야 거리");
		case EVBQualityGroup::AntiAliasing: return LOCTEXT("AntiAliasing", "안티에일리어싱");
		case EVBQualityGroup::Shadow: return LOCTEXT("Shadow", "그림자");
		case EVBQualityGroup::GlobalIllumination: return LOCTEXT("GlobalIllumination", "전역 조명");
		case EVBQualityGroup::Reflection: return LOCTEXT("Reflection", "반사");
		case EVBQualityGroup::PostProcess: return LOCTEXT("PostProcess", "후처리");
		case EVBQualityGroup::Texture: return LOCTEXT("Texture", "텍스처");
		case EVBQualityGroup::Effects: return LOCTEXT("Effects", "효과");
		case EVBQualityGroup::Foliage: return LOCTEXT("Foliage", "초목");
		case EVBQualityGroup::Shading: return LOCTEXT("Shading", "셰이딩");
		default: return FText::GetEmpty();
		}
	}

	void BuildDisplay(const FVBOptionCatalogContext& Context, TArray<FVBOptionEntry>& Out)
	{
		const TArray<FIntPoint> Resolutions = Context.Resolutions;
		TArray<FText> ResolutionNames;
		for (const FIntPoint& Resolution : Resolutions)
		{
			ResolutionNames.Add(FText::FromString(FString::Printf(TEXT("%d x %d"), Resolution.X, Resolution.Y)));
		}

		Out.Add(Choice(TEXT("Resolution"), LOCTEXT("Resolution", "해상도"), MoveTemp(ResolutionNames),
			[Resolutions](const FVBOptionsDraft& Draft) { return Resolutions.IndexOfByKey(Draft.Machine.Resolution); },
			[Resolutions](FVBOptionsDraft& Draft, int32 Index)
			{
				if (Resolutions.IsValidIndex(Index))
				{
					Draft.Machine.Resolution = Resolutions[Index];
				}
			}));

		// 선택지 번호 = EWindowMode 값.
		Out.Add(Choice(TEXT("WindowMode"), LOCTEXT("WindowMode", "화면 모드"),
			{ LOCTEXT("Fullscreen", "전체 화면"), LOCTEXT("WindowedFullscreen", "창 전체 화면"), LOCTEXT("Windowed", "창 모드") },
			[](const FVBOptionsDraft& Draft) { return static_cast<int32>(Draft.Machine.WindowMode); },
			[](FVBOptionsDraft& Draft, int32 Index) { Draft.Machine.WindowMode = static_cast<EWindowMode::Type>(Index); }));

		FVBOptionEntry VSync = MakeEntry(TEXT("VSync"), LOCTEXT("VSync", "수직 동기화"), EVBOptionKind::Toggle);
		VSync.Get = [](const FVBOptionsDraft& Draft) { return Draft.Machine.bVSync ? 1.0f : 0.0f; };
		VSync.Set = [](FVBOptionsDraft& Draft, float Value) { Draft.Machine.bVSync = Value >= 0.5f; };
		Out.Add(MoveTemp(VSync));

		TArray<float> Limits = Context.Settings->FrameRateLimits;
		const float Monitor = static_cast<float>(Context.MonitorRefreshRate);
		const bool bAddMonitor = Context.MonitorRefreshRate > 0
			&& !Limits.ContainsByPredicate([Monitor](float Limit) { return FMath::IsNearlyEqual(Limit, Monitor); });
		if (bAddMonitor)
		{
			// 숫자 순서 자리에 넣는다. 무제한(0)은 늘 끝이다.
			const int32 At = Limits.IndexOfByPredicate([Monitor](float Limit) { return Limit <= 0.0f || Limit > Monitor; });
			Limits.Insert(Monitor, At == INDEX_NONE ? Limits.Num() : At);
		}

		TArray<FText> LimitNames;
		for (const float Limit : Limits)
		{
			const FText Number = FText::AsNumber(FMath::RoundToInt(Limit));
			LimitNames.Add(Limit <= 0.0f ? LOCTEXT("Unlimited", "무제한")
				: bAddMonitor && FMath::IsNearlyEqual(Limit, Monitor) ? FText::Format(LOCTEXT("FrameRateMonitor", "{0} FPS (모니터)"), Number)
				: FText::Format(LOCTEXT("FrameRate", "{0} FPS"), Number));
		}

		Out.Add(Choice(TEXT("FrameRateLimit"), LOCTEXT("FrameRateLimit", "프레임 제한"), MoveTemp(LimitNames),
			[Limits](const FVBOptionsDraft& Draft)
			{
				return Limits.IndexOfByPredicate([&Draft](float Limit)
				{
					return FMath::IsNearlyEqual(Limit, Draft.Machine.FrameRateLimit);
				});
			},
			[Limits](FVBOptionsDraft& Draft, int32 Index)
			{
				if (Limits.IsValidIndex(Index))
				{
					Draft.Machine.FrameRateLimit = Limits[Index];
				}
			}));
	}

	FText ResolutionPresetLabel(FName Name, float ResolutionQuality)
	{
		const FText Percent = FText::AsNumber(FMath::RoundToInt(ResolutionQuality));
		if (ResolutionQuality <= 0.0f)
		{
			return LOCTEXT("UpscaleAuto", "자동 (기본 비율)");
		}

		if (Name == TEXT("Performance")) return FText::Format(LOCTEXT("UpscalePerformance", "성능 ({0}%)"), Percent);
		if (Name == TEXT("Balanced")) return FText::Format(LOCTEXT("UpscaleBalanced", "균형 ({0}%)"), Percent);
		if (Name == TEXT("Quality")) return FText::Format(LOCTEXT("UpscaleQuality", "품질 ({0}%)"), Percent);
		if (Name == TEXT("Native")) return FText::Format(LOCTEXT("UpscaleNative", "네이티브 ({0}%)"), Percent);
		// 프로젝트 Scalability ini 가 더한 프리셋 - 이름이 없어도 줄이 사라지지 않게 엔진 이름 그대로.
		return FText::Format(LOCTEXT("UpscaleNamed", "{0} ({1}%)"), FText::FromName(Name), Percent);
	}

	// 업스케일링 = 렌더 해상도 배율(엔진 해상도 프리셋). 안티에일리어싱 품질 바로 다음에 둔다.
	void AddUpscaling(const FVBOptionCatalogContext& Context, TArray<FVBOptionEntry>& Out)
	{
		TArray<float> Values;
		TArray<FText> Names;
		for (const FVBResolutionPresetChoice& Preset : Context.ResolutionPresets)
		{
			Values.Add(Preset.ResolutionQuality);
			Names.Add(ResolutionPresetLabel(Preset.Name, Preset.ResolutionQuality));
		}

		const float Current = Context.CurrentResolutionQuality;
		if (!Values.ContainsByPredicate([Current](float Value) { return FMath::IsNearlyEqual(Value, Current, 0.01f); }))
		{
			Values.Add(Current);
			// 0 은 프로젝트가 자동 프리셋을 지웠어도 자동이다(사용자 지정 0% 로 보이지 않게).
			Names.Add(Current <= 0.0f ? ResolutionPresetLabel(NAME_None, Current)
				: FText::Format(LOCTEXT("UpscaleCustom", "사용자 지정 ({0}%)"), FText::AsNumber(FMath::RoundToInt(Current))));
		}

		Out.Add(Choice(TEXT("Upscaling"), LOCTEXT("Upscaling", "업스케일링"), MoveTemp(Names),
			[Values](const FVBOptionsDraft& Draft)
			{
				return Values.IndexOfByPredicate([&Draft](float Value)
				{
					return FMath::IsNearlyEqual(Value, Draft.Machine.ResolutionQuality, 0.01f);
				});
			},
			[Values](FVBOptionsDraft& Draft, int32 Index)
			{
				if (Values.IsValidIndex(Index))
				{
					Draft.Machine.ResolutionQuality = Values[Index];
				}
			}));
	}

	void BuildGraphics(const FVBOptionCatalogContext& Context, TArray<FVBOptionEntry>& Out)
	{
		const TArray<FText> Levels = QualityLevels();
		const int32 CustomIndex = Levels.Num();
		TArray<FText> OverallChoices = Levels;
		OverallChoices.Add(LOCTEXT("QualityCustom", "사용자 지정"));
		Out.Add(Choice(TEXT("OverallQuality"), LOCTEXT("OverallQuality", "전체 품질"), MoveTemp(OverallChoices),
			[CustomIndex](const FVBOptionsDraft& Draft)
			{
				const int32 Overall = Draft.Machine.GetOverallQuality();
				return Overall == INDEX_NONE ? CustomIndex : Overall;
			},
			[CustomIndex](FVBOptionsDraft& Draft, int32 Index) { if (Index < CustomIndex) Draft.Machine.SetOverallQuality(Index); }));

		for (int32 GroupIndex = 0; GroupIndex < static_cast<int32>(EVBQualityGroup::Count); ++GroupIndex)
		{
			const EVBQualityGroup Group = static_cast<EVBQualityGroup>(GroupIndex);
			Out.Add(Choice(FName(TEXT("Quality"), GroupIndex + 1), QualityGroupLabel(Group), Levels,
				[Group](const FVBOptionsDraft& Draft) { return Draft.Machine.GetQuality(Group); },
				[Group](FVBOptionsDraft& Draft, int32 Index) { Draft.Machine.SetQuality(Group, Index); }));
			if (Group == EVBQualityGroup::AntiAliasing)
			{
				AddUpscaling(Context, Out);
			}
		}

		Out.Add(MakeEntry(VBOptionCatalog::AutoDetect, LOCTEXT("AutoDetect", "자동 감지"), EVBOptionKind::Action));
	}

	void BuildAudio(TArray<FVBOptionEntry>& Out)
	{
		const FFloatInterval Unit(0.0f, 1.0f);
		Out.Add(UserSlider(TEXT("MasterVolume"), LOCTEXT("MasterVolume", "전체 볼륨"), &FVBSettingsSaveData::MasterVolume, Unit, true));
		Out.Add(UserSlider(TEXT("MusicVolume"), LOCTEXT("MusicVolume", "음악"), &FVBSettingsSaveData::MusicVolume, Unit, true));
		Out.Add(UserSlider(TEXT("SfxVolume"), LOCTEXT("SfxVolume", "효과음"), &FVBSettingsSaveData::SfxVolume, Unit, true));
		Out.Add(UserSlider(TEXT("VoiceVolume"), LOCTEXT("VoiceVolume", "음성"), &FVBSettingsSaveData::VoiceVolume, Unit, true));
	}

	void BuildSubtitles(const FVBOptionCatalogContext& Context, TArray<FVBOptionEntry>& Out)
	{
		if (!Context.Cultures.IsEmpty())
		{
			TArray<FString> Names;
			TArray<FText> Labels;
			for (const FVBCultureChoice& Culture : Context.Cultures)
			{
				Names.Add(Culture.Name);
				Labels.Add(Culture.Label);
			}

			Out.Add(Choice(TEXT("Language"), LOCTEXT("Language", "언어"), MoveTemp(Labels),
				[Names](const FVBOptionsDraft& Draft) { return Names.IndexOfByKey(Draft.Culture); },
				[Names](FVBOptionsDraft& Draft, int32 Index) { if (Names.IsValidIndex(Index)) Draft.Culture = Names[Index]; }));
		}

		Out.Add(UserToggle(TEXT("Subtitles"), LOCTEXT("Subtitles", "자막"), &FVBSettingsSaveData::bSubtitlesEnabled));
		Out.Add(Choice(TEXT("SubtitleSize"), LOCTEXT("SubtitleSize", "자막 크기"),
			{ LOCTEXT("SizeSmall", "작게"), LOCTEXT("SizeMedium", "보통"), LOCTEXT("SizeLarge", "크게") },
			[](const FVBOptionsDraft& Draft) { return static_cast<int32>(Draft.User.SubtitleSize); },
			[](FVBOptionsDraft& Draft, int32 Index) { Draft.User.SubtitleSize = static_cast<EVBSubtitleSize>(Index); }));
		Out.Add(UserToggle(TEXT("SpeakerName"), LOCTEXT("SpeakerName", "화자 이름"), &FVBSettingsSaveData::bShowSpeakerName));
	}

	void BuildAccessibility(const FVBOptionCatalogContext& Context, TArray<FVBOptionEntry>& Out)
	{
		const FFloatInterval Unit(0.0f, 1.0f);
		Out.Add(UserSlider(TEXT("UIScale"), LOCTEXT("UIScale", "UI 크기"), &FVBSettingsSaveData::UIScale,
			Context.Settings->UIScaleRange, true));
		Out.Add(UserSlider(TEXT("CameraShake"), LOCTEXT("CameraShake", "화면 흔들림"), &FVBSettingsSaveData::CameraShakeScale, Unit, true));
		Out.Add(UserSlider(TEXT("ScreenFlash"), LOCTEXT("ScreenFlash", "화면 가장자리 섬광"), &FVBSettingsSaveData::ScreenFlashScale, Unit, true));
	}

	void BuildControls(const FVBOptionCatalogContext& Context, TArray<FVBOptionEntry>& Out)
	{
		Out.Add(UserSlider(TEXT("LookSensitivity"), LOCTEXT("LookSensitivity", "시점 감도"), &FVBSettingsSaveData::LookSensitivity,
			Context.Settings->LookSensitivityRange, false));
		Out.Add(UserToggle(TEXT("InvertY"), LOCTEXT("InvertY", "상하 반전"), &FVBSettingsSaveData::bInvertYAxis));
		Out.Add(UserToggle(TEXT("Vibration"), LOCTEXT("Vibration", "진동"), &FVBSettingsSaveData::bVibrationEnabled));
		{
			// 패드 시점 감도: 안 고른 동안(-1)은 마우스 값을 보여 주고, 다른 값으로 움직이면 실제 값을 넣는다.
			// 마우스와 같은 값은 따르기(-1)로 둔다 - Set(Get) 이 초안을 바꾸지 않아야 한다.
			FVBOptionEntry PadLook = MakeEntry(TEXT("GamepadLookSensitivity"), LOCTEXT("GamepadLookSensitivity", "패드 시점 감도"),
				EVBOptionKind::Slider);
			const FFloatInterval Range = Context.Settings->LookSensitivityRange;
			PadLook.Range = Range;
			PadLook.bPercent = false;
			PadLook.Get = [](const FVBOptionsDraft& Draft) { return VBInputCalibration::GamepadLookSensitivity(Draft.User); };
			PadLook.Set = [Range](FVBOptionsDraft& Draft, float Value)
			{
				const float Chosen = FMath::Clamp(Value, Range.Min, Range.Max);
				Draft.User.GamepadLookSensitivity = FMath::IsNearlyEqual(Chosen, Draft.User.LookSensitivity) ? -1.0f : Chosen;
			};
			Out.Add(MoveTemp(PadLook));
		}

		Out.Add(UserSlider(TEXT("GamepadLookDeadZone"), LOCTEXT("GamepadLookDeadZone", "패드 시점 데드존"),
			&FVBSettingsSaveData::GamepadLookDeadZone,
			Context.Settings->StickDeadZoneRange, false));
		Out.Add(UserSlider(TEXT("GamepadMoveDeadZone"), LOCTEXT("GamepadMoveDeadZone", "패드 이동 데드존"),
			&FVBSettingsSaveData::GamepadMoveDeadZone,
			Context.Settings->StickDeadZoneRange, false));
		for (const FVBToggleableInput& Input : Context.Settings->ToggleableInputs)
		{
			const FGameplayTag Tag = Input.InputTag;
			Out.Add(Choice(Tag.GetTagName(), Input.Label, { LOCTEXT("Hold", "누르고 있기"), LOCTEXT("Toggle", "토글") },
				[Tag](const FVBOptionsDraft& Draft) { return Draft.User.ToggleInputs.HasTagExact(Tag) ? 1 : 0; },
				[Tag](FVBOptionsDraft& Draft, int32 Index)
				{
					if (Index == 1)
					{
						Draft.User.ToggleInputs.AddTag(Tag);
					}
					else
					{
						Draft.User.ToggleInputs.RemoveTag(Tag);
					}
				}));
		}

		Out.Add(MakeEntry(VBOptionCatalog::KeyBindings, LOCTEXT("KeyBindings", "키 다시 지정"), EVBOptionKind::Action));
	}

	// 난이도. 선택지와 초안 모두 태그를 쓴다(순서 번호 저장 안 함). 원격 클라는 지금 단계 하나뿐이고 이름에 이유를 단다.
	void BuildGameplay(const FVBOptionCatalogContext& Context, TArray<FVBOptionEntry>& Out)
	{
		// 플레이어 설정이라 게임이 없어도(메인 메뉴) 보인다 - 그래서 게임플레이 탭이 메인 메뉴에도 생긴다.
		Out.Add(UserToggle(TEXT("AutoTargetNext"), LOCTEXT("AutoTargetNext", "적 처치 후 다음 대상 자동 락온"),
			&FVBSettingsSaveData::bAutoTargetNextOnKill));
		Out.Add(UserToggle(TEXT("ShowMinimap"), LOCTEXT("ShowMinimap", "미니맵"), &FVBSettingsSaveData::bShowMinimap));
		Out.Add(Choice(TEXT("BloodDisplay"), LOCTEXT("BloodDisplay", "피 표현"),
			{ LOCTEXT("BloodDisplayNormal", "보통"), LOCTEXT("BloodDisplayAlternate", "다른 색"), LOCTEXT("BloodDisplayOff", "끄기") },
			[](const FVBOptionsDraft& Draft) { return static_cast<int32>(Draft.User.BloodDisplay); },
			[](FVBOptionsDraft& Draft, int32 Index) { Draft.User.BloodDisplay = static_cast<EVBBloodDisplay>(Index); }));
		if (Context.Difficulties.IsEmpty())
		{
			return;
		}

		TArray<FText> Labels;
		TArray<FGameplayTag> Tags;
		for (const FVBDifficultyChoice& Choice : Context.Difficulties)
		{
			Labels.Add(Choice.Label);
			Tags.Add(Choice.Tag);
		}

		const FText Label = Context.bDifficultyHostOnly ? LOCTEXT("DifficultyHostOnly",
			"난이도 (호스트만 바꿀 수 있음)") : LOCTEXT("Difficulty", "난이도");
		Out.Add(Choice(TEXT("Difficulty"), Label, MoveTemp(Labels),
			[Tags](const FVBOptionsDraft& Draft) { return Tags.IndexOfByKey(Draft.Difficulty); },
			[Tags](FVBOptionsDraft& Draft, int32 Index) { if (Tags.IsValidIndex(Index)) { Draft.Difficulty = Tags[Index]; } }));
	}
}

namespace VBOptionCatalog
{
	FText TabLabel(EVBOptionTab Tab)
	{
		switch (Tab)
		{
		case EVBOptionTab::Display: return LOCTEXT("TabDisplay", "화면");
		case EVBOptionTab::Graphics: return LOCTEXT("TabGraphics", "그래픽");
		case EVBOptionTab::Audio: return LOCTEXT("TabAudio", "오디오");
		case EVBOptionTab::Subtitles: return LOCTEXT("TabLanguageSubtitles", "언어·자막");
		case EVBOptionTab::Accessibility: return LOCTEXT("TabAccessibility", "접근성");
		case EVBOptionTab::Controls: return LOCTEXT("TabControls", "조작");
		case EVBOptionTab::Gameplay: return LOCTEXT("TabGameplay", "게임플레이");
		default: return FText::GetEmpty();
		}
	}

	TArray<FVBOptionEntry> BuildTab(EVBOptionTab Tab, const FVBOptionCatalogContext& Context)
	{
		TArray<FVBOptionEntry> Entries;
		if (!Context.Settings)
		{
			return Entries;
		}

		switch (Tab)
		{
		case EVBOptionTab::Display: BuildDisplay(Context, Entries); break;
		case EVBOptionTab::Graphics: BuildGraphics(Context, Entries); break;
		case EVBOptionTab::Audio: BuildAudio(Entries); break;
		case EVBOptionTab::Subtitles: BuildSubtitles(Context, Entries); break;
		case EVBOptionTab::Accessibility: BuildAccessibility(Context, Entries); break;
		case EVBOptionTab::Controls: BuildControls(Context, Entries); break;
		case EVBOptionTab::Gameplay: BuildGameplay(Context, Entries); break;
		default: break;
		}

		return Entries;
	}

	int32 StepVisibleTab(int32 Current, int32 Step, const TArray<bool>& Visible)
	{
		const int32 Count = Visible.Num();
		if (Count == 0 || Step == 0)
		{
			return INDEX_NONE;
		}

		const int32 Direction = Step > 0 ? 1 : -1;
		int32 Index = FMath::Clamp(Current, 0, Count - 1);
		for (int32 Tried = 0; Tried < Count; ++Tried)
		{
			Index = (Index + Direction + Count) % Count;
			if (Visible[Index])
			{
				return Index;
			}
		}

		return INDEX_NONE;
	}
}

#undef LOCTEXT_NAMESPACE
