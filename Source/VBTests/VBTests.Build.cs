// ProjectVB Copyright (c) 2025. All Rights Reserved.

using UnrealBuildTool;

public class VBTests : ModuleRules
{
	public VBTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("VBEditor"); // FVBRigVMCookFilter (VBRigVMCookFilterTests, FIND-160, 2026-10-24 user 승인)
			PrivateDependencyModuleNames.Add("RigVM"); // IRigVMRuntimeAssetInterface (VBRigVMCookFilterTests, FIND-160, 2026-10-24 user 승인)
		}
		PrivateDependencyModuleNames.Add("PhysicsCore"); // Isolated physical-material/contact fixtures (approved).
		PrivateDependencyModuleNames.Add("MotionWarping");
		PrivateDependencyModuleNames.Add("AnimationWarpingRuntime");
		PrivateDependencyModuleNames.Add("ProceduralMeshComponent"); // 궤적 검사에서 실제 메시 컴포넌트를 조회한다.
		// Play bot game audio: render the non-realtime mixer per frame and record its output (-DeterministicAudio, the same pair
		//   Movie Render Queue uses). User 2026-10-23 «1» via coordinator 00477472, .sw/combat-feel/capture-clock-audio.md 4.
		PrivateDependencyModuleNames.Add("AudioMixer");
		PrivateDependencyModuleNames.Add("NonRealtimeAudioRenderer");

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",

			// AFunctionalTest — Developer 모듈. 본 모듈이 UncookedOnly(cooked/Shipping 제외)라
			// Developer 모듈 링크 제약과 충돌하지 않음 (Design_2026-06-05_DEBT-004 E10/E16).
			"FunctionalTesting",

			// 진짜 키 경로 주입 — StartContinuousInputInjectionForAction (런타임 API, E15)
			"EnhancedInput",

			// FGameplayTag (VBGameplayTags 단위 테스트)
			"GameplayTags",

			// FGameplayAttribute (AVBPlayerState::SavedAttributes 세이브 키 계약 테스트)
			"GameplayAbilities",

			// 런타임 모듈에서 옮겨 온 시험이 직접 쓰는 모듈 (2026-10-02, 구조 정리 1a).
			// VB 가 공개 의존으로 이미 끌고 오지만, 시험이 직접 include·생성하는 모듈은 여기 적는다 -
			// 그래야 VB 의 의존이 바뀌어도 시험이 조용히 깨지지 않는다.
			"GameplayCameras", // UGameplayCameraComponent, UCameraAsset (VBEnvironmentCameraPolicyTests)
			"InputCore",       // EKeys (VBTargetLockPolicyTests)
			"AIModule",        // AAIController::UseBlackboard, UBlackboardComponent·키 타입 (VBCombatCoordinatorAuditTests, 2026-10-15 user 승인)
			"UMG",             // CreateWidget, UWidget::TakeWidget - HUD 줄 위젯 구성·해체 (VBHUDRowBindingTests, 묶음 3-1b)
			"SlateCore",       // FPointerEvent, FReply - 터치 시작을 실제 함수로 부른다 (VBTouchControlsTests, 묶음 3-1d2)
			"CommonInput",     // UCommonInputSubsystem::SetCurrentInputType - 입력 종류 출처 (VBTouchControlsTests)
			"CommonUI",        // UCommonUIActionRouterBase::SetActiveUIInputConfig - 입력 모드 출처 (VBTouchControlsTests)
			"Slate",           // FSlateApplication key/analog events - play bot pad input takes the hardware route (.sw/playbot, 2026-10-20 user approved)
			"Niagara",         // UNiagaraSystem - 피 이펙트가 끄기 스위치를 들고 있나 (VBBloodDisplayTests, .sw/blood-display)
			"DeveloperSettings", // UCommonInputPlatformSettings::Get() 은 인라인으로 UPlatformSettingsManager 를 부른다 - 키 그림 대응표 (VBKeyIconTests, L3d)
			"VBCinematicPlayback", // UVBCinematicDefinition - 시네마틱 임대 시험이 직접 만든다 (VBCinematicStartupSafetyTests)
			"LevelSequence",       // ULevelSequence 소프트 참조 (VBCinematicStartupSafetyTests)

			// 검증 대상 게임 모듈 (VB_API export 확인 완료, E20)
			"VB",
		});
	}
}
