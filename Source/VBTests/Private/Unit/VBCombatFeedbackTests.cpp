#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/VBCombatFeedbackModifier.h"
#include "Camera/VBFinisherCamera.h"
#include "Camera/VBFinisherDirectorSubsystem.h"
#include "AbilitySystem/GameplayCues/VBGC_CombatFeedback.h"
#include "AbilitySystem/GameplayCues/VBGC_SwingImpactShake.h"
#include "GameplayEffectTypes.h"
#include "Data/VBCombatFeedbackProfile.h"
#include "Data/VBSwingCombatProfile.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Curves/CurveFloat.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "TimeControl/VBTimeDilationSubsystem.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFeedbackRecipientTest, "VB.Unit.Camera.CombatFeedback.RecipientRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBFeedbackRecipientTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
	UWorld* Foreign = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
	ON_SCOPE_EXIT
	{
		if (Foreign) { Foreign->DestroyWorld(false); }
		if (World) { World->DestroyWorld(false); }
	};
	if (!TestNotNull(TEXT("recipient world"), World) || !TestNotNull(TEXT("foreign world"), Foreign)) { return false; }
	World->GetOutermost()->SetFlags(RF_Transient);
	Foreign->GetOutermost()->SetFlags(RF_Transient);
	APawn* Victim = World->SpawnActor<APawn>();
	APawn* Attacker = World->SpawnActor<APawn>();
	APawn* OtherWorldPawn = Foreign->SpawnActor<APawn>();
	AActor* NonPawn = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("victim"), Victim) || !TestNotNull(TEXT("attacker"), Attacker)
		|| !TestNotNull(TEXT("foreign pawn"), OtherWorldPawn) || !TestNotNull(TEXT("non-pawn"), NonPawn))
	{
		return false;
	}
	FGameplayCueParameters Cue;
	Cue.Instigator = Attacker;
	TestEqual(TEXT("target policy preserves victim routing"),
		UVBGC_CombatFeedback::ResolveScreenRecipient(Victim, Cue, EVBFeedbackRecipient::Target), Victim);
	TestEqual(TEXT("landing policy selects attacker"),
		UVBGC_CombatFeedback::ResolveScreenRecipient(Victim, Cue, EVBFeedbackRecipient::Instigator), Attacker);
	Cue.Instigator = nullptr;
	Cue.EffectCauser = Attacker;
	TestNull(TEXT("missing instigator does not substitute victim or effect causer"),
		UVBGC_CombatFeedback::ResolveScreenRecipient(Victim, Cue, EVBFeedbackRecipient::Instigator));
	Cue.Instigator = NonPawn;
	TestNull(TEXT("non-pawn instigator rejected"),
		UVBGC_CombatFeedback::ResolveScreenRecipient(Victim, Cue, EVBFeedbackRecipient::Instigator));
	Cue.Instigator = OtherWorldPawn;
	TestNull(TEXT("foreign-world instigator rejected"),
		UVBGC_CombatFeedback::ResolveScreenRecipient(Victim, Cue, EVBFeedbackRecipient::Instigator));
	TestNull(TEXT("absent target rejected"),
		UVBGC_CombatFeedback::ResolveScreenRecipient(nullptr, Cue, EVBFeedbackRecipient::Target));
	TestTrue(TEXT("existing profiles default to target"),
		NewObject<UVBCombatFeedbackProfile>()->ScreenRecipient == EVBFeedbackRecipient::Target);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAirSlamProfileTest, "VB.Unit.Camera.CombatFeedback.AuthoredAirSlam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBAirSlamProfileTest::RunTest(const FString& Parameters)
{
	const UVBCombatFeedbackProfile* Profile = LoadObject<UVBCombatFeedbackProfile>(nullptr,
		TEXT("/Game/VB/Data/DA_CombatFeedback_AirSlam.DA_CombatFeedback_AirSlam"));
	if (!TestNotNull(TEXT("authored AirSlam profile"), Profile)) { return false; }
	TestTrue(TEXT("landing enables attacker screen feedback"), Profile->bScreenImpact);
	TestTrue(TEXT("landing screen recipient is original attacker"), Profile->ScreenRecipient == EVBFeedbackRecipient::Instigator);
	TestTrue(TEXT("landing camera envelope is finite and brief"), FMath::IsFinite(Profile->Screen.CameraSeconds)
		&& Profile->Screen.CameraSeconds > 0.0f && Profile->Screen.CameraSeconds <= 1.0f);
	TestTrue(TEXT("landing has a visible camera channel"), FMath::IsFinite(Profile->Screen.FOVAmplitude)
		&& !FMath::IsNearlyZero(Profile->Screen.FOVAmplitude));
	TestTrue(TEXT("ground world effect is preserved"), !Profile->WorldEffects.IsEmpty() && Profile->WorldEffects[0].System);
	return true;
}

namespace
{
	struct FFeedbackCameraFixture
	{
		UWorld* World = nullptr;
		APlayerController* Controller = nullptr;
		APawn* Pawn = nullptr;
		APlayerCameraManager* Camera = nullptr;
		UVBCombatFeedbackModifier* Modifier = nullptr;

		bool Initialize(FAutomationTestBase& Test)
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
			if (!Test.TestNotNull(TEXT("isolated world"), World))
			{
				return false;
			}
			// 직접 만든 패키지만 transient로 한정한다. Editor/사용자 맵과 저장 슬롯은 사용하지 않는다.
			World->GetOutermost()->SetFlags(RF_Transient);
			if (GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}

			Controller = World->SpawnActor<APlayerController>();
			Pawn = World->SpawnActor<APawn>();
			if (!Test.TestNotNull(TEXT("controller"), Controller) || !Test.TestNotNull(TEXT("pawn"), Pawn))
			{
				return false;
			}
			// 실제 로컬 플레이어 문맥만 붙인다. 입력/온라인 초기화가 필요한 SetPlayer는 호출하지 않는다.
			if (!Test.TestNotNull(TEXT("engine owns local player"), GEngine))
			{
				return false;
			}

			ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient);
			Controller->Player = LocalPlayer;
			LocalPlayer->PlayerController = Controller;
			Controller->Possess(Pawn);
			Camera = Controller->PlayerCameraManager;
			if (!Camera)
			{
				Camera = World->SpawnActor<APlayerCameraManager>();
				if (!Test.TestNotNull(TEXT("camera"), Camera))
				{
					return false;
				}

				Camera->InitializeFor(Controller);
				Controller->PlayerCameraManager = Camera;
			}

			Camera->SetViewTarget(Pawn);
			Modifier = Cast<UVBCombatFeedbackModifier>(Camera->AddNewCameraModifier(UVBCombatFeedbackModifier::StaticClass()));
			return Test.TestNotNull(TEXT("modifier"), Modifier) && Test.TestTrue(TEXT("local owned pawn"), Pawn->IsLocallyControlled());
		}

		~FFeedbackCameraFixture()
		{
			if (IsValid(Modifier))
			{
				Modifier->StopFeedback();
			}

			if (World)
			{
				const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
				if (World->HasBegunPlay())
				{
					World->EndPlay(EEndPlayReason::Quit);
				}

				World->DestroyWorld(false);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}

				OwnedPackage->SetDirtyFlag(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatFeedbackRealClockTest, "VB.Unit.Camera.CombatFeedback.RealClock",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatFeedbackRealClockTest::RunTest(const FString& Parameters)
{
	FFeedbackCameraFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	FVBScreenImpactSettings Settings;
	Settings.CameraSeconds = 0.2f;
	Settings.EffectSeconds = 0.2f;
	Settings.FOVAmplitude = 2.0f;
	UGameplayStatics::SetGlobalTimeDilation(Fixture.World, 0.05f);
	TestTrue(TEXT("feedback admitted"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	const FRotator ControlBefore = Fixture.Controller->GetControlRotation();
	FMinimalViewInfo Initial;
	Initial.FOV = 90.0f;
	Fixture.Modifier->ModifyCamera(0.001f, Initial);
	TestTrue(TEXT("immediate camera impulse"), Initial.FOV > 90.0f);
	TestTrue(TEXT("view impulse leaves aim input intact"), Fixture.Controller->GetControlRotation().Equals(ControlBefore));

	const float RealBefore = Fixture.World->GetRealTimeSeconds();
	const float GameBefore = Fixture.World->GetTimeSeconds();
	Fixture.World->Tick(LEVELTICK_TimeOnly, 0.12f);
	Fixture.World->Tick(LEVELTICK_TimeOnly, 0.12f);
	TestTrue(TEXT("real clock advances under slow motion"), Fixture.World->GetRealTimeSeconds() - RealBefore >= 0.2f);
	TestTrue(TEXT("game clock remains slowed"), Fixture.World->GetTimeSeconds() - GameBefore < 0.03f);
	FMinimalViewInfo After;
	After.FOV = 90.0f;
	Fixture.Modifier->ModifyCamera(0.001f, After);
	TestEqual(TEXT("impulse expires by real time, not dilated callback delta"), After.FOV, 90.0f);
	TestTrue(TEXT("expired modifier disabled"), Fixture.Modifier->IsDisabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatFeedbackOwnerLifetimeTest, "VB.Unit.Camera.CombatFeedback.OwnerLifetime",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatFeedbackOwnerLifetimeTest::RunTest(const FString& Parameters)
{
	FFeedbackCameraFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	FVBScreenImpactSettings Settings;
	Settings.CameraSeconds = 1.0f;
	Settings.EffectSeconds = 1.0f;
	TestTrue(TEXT("first feedback"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	TestTrue(TEXT("same modifier retrigger"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	TestTrue(TEXT("camera retains same modifier"),
		Fixture.Camera->FindCameraModifierByClass(UVBCombatFeedbackModifier::StaticClass()) == Fixture.Modifier);
	APawn* Other = Fixture.World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("other view target"), Other))
	{
		return false;
	}

	Fixture.Camera->SetViewTarget(Other);
	Fixture.Modifier->Tick(0.0f);
	TestTrue(TEXT("view change cancels without camera callback"), Fixture.Modifier->IsDisabled());

	Fixture.Camera->SetViewTarget(Fixture.Pawn);
	TestTrue(TEXT("owned view can restart"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	Fixture.Camera->CameraStyle = FName(TEXT("Fixed"));
	Fixture.Modifier->Tick(0.0f);
	TestTrue(TEXT("style bypass cancels cached effect"), Fixture.Modifier->IsDisabled());

	Fixture.Camera->CameraStyle = NAME_Default;
	TestTrue(TEXT("restart after style"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	Fixture.Controller->UnPossess();
	Fixture.Modifier->Tick(0.0f);
	TestTrue(TEXT("old pawn ownership cancels"), Fixture.Modifier->IsDisabled());
	TestFalse(TEXT("unowned pawn rejected"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatFeedbackClockPrecisionTest, "VB.Unit.Camera.CombatFeedback.ClockPrecision",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatFeedbackClockPrecisionTest::RunTest(const FString& Parameters)
{
	FFeedbackCameraFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	FVBScreenImpactSettings Settings;
	Settings.CameraSeconds = 0.2f;
	Settings.EffectSeconds = 0.2f;
	Settings.FOVAmplitude = 2.0f;
	const double StartTimes[] = {31.862710408866405, 32.90390921384096, 86400.005, 86400.003};
	TestTrue(TEXT("upward float rounding reproducer"), static_cast<double>(static_cast<float>(StartTimes[0])) > StartTimes[0]);
	TestTrue(TEXT("downward float rounding control"), static_cast<double>(static_cast<float>(StartTimes[1])) < StartTimes[1]);
	for (const double StartTime : StartTimes)
	{
		const FString Context = FString::Printf(TEXT("start %.15f"), StartTime);
		Fixture.World->RealTimeSeconds = StartTime;
		TestTrue(Context + TEXT(" admitted"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
		FMinimalViewInfo Initial;
		Initial.FOV = 90.0f;
		Fixture.Modifier->ModifyCamera(0.0f, Initial);
		TestTrue(Context + TEXT(" same-clock full impulse"), Initial.FOV == 92.0f);
		TestFalse(Context + TEXT(" camera keeps same-frame feedback"), Fixture.Modifier->IsDisabled());
		Fixture.Modifier->Tick(0.0f);
		TestFalse(Context + TEXT(" tick keeps same-frame feedback"), Fixture.Modifier->IsDisabled());

		Fixture.World->RealTimeSeconds = StartTime + 0.1;
		Fixture.Modifier->Tick(0.0f);
		TestFalse(Context + TEXT(" survives within lifetime"), Fixture.Modifier->IsDisabled());
		FMinimalViewInfo During;
		During.FOV = 90.0f;
		Fixture.Modifier->ModifyCamera(0.0f, During);
		TestTrue(Context + TEXT(" decayed impulse remains positive"), During.FOV > 90.0f && During.FOV < 92.0f);

		Fixture.World->RealTimeSeconds = StartTime + static_cast<double>(Settings.EffectSeconds) + 0.001;
		Fixture.Modifier->Tick(0.0f);
		TestTrue(Context + TEXT(" expires after lifetime"), Fixture.Modifier->IsDisabled());
		FMinimalViewInfo Expired;
		Expired.FOV = 90.0f;
		Fixture.Modifier->ModifyCamera(0.0f, Expired);
		TestEqual(Context + TEXT(" expired impulse leaves view intact"), Expired.FOV, 90.0f);
	}

	Fixture.World->RealTimeSeconds = 100.0;
	TestTrue(TEXT("clock reversal setup admitted"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	Fixture.World->RealTimeSeconds = 99.99;
	Fixture.Modifier->Tick(0.0f);
	TestTrue(TEXT("actual clock reversal still cancels"), Fixture.Modifier->IsDisabled());
	Fixture.World->RealTimeSeconds = 200.0;
	TestTrue(TEXT("camera clock reversal setup admitted"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	Fixture.World->RealTimeSeconds = 199.99;
	FMinimalViewInfo Reversed;
	Reversed.FOV = 90.0f;
	Fixture.Modifier->ModifyCamera(0.0f, Reversed);
	TestTrue(TEXT("camera clock reversal still cancels"), Fixture.Modifier->IsDisabled());
	TestEqual(TEXT("reversed clock leaves view intact"), Reversed.FOV, 90.0f);
	return true;
}

// 교체 규칙: 진행 중인 사건보다 낮은 우선순위는 버리고, 같거나 높으면 다시 건다.
//   만료됐지만 아직 Tick 이 안 돈 사건은 막지 못한다 - 막으면 다음 가드가 이유 없이 사라진다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatFeedbackPriorityTest, "VB.Unit.Camera.CombatFeedback.Priority",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatFeedbackPriorityTest::RunTest(const FString& Parameters)
{
	FFeedbackCameraFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	FVBScreenImpactSettings Strong;
	Strong.Priority = 3;
	Strong.CameraSeconds = 1.0f;
	Strong.EffectSeconds = 1.0f;
	FVBScreenImpactSettings Weak = Strong;
	Weak.Priority = 1;

	Fixture.World->RealTimeSeconds = 10.0;
	TestTrue(TEXT("strong admitted"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Strong, FVector::ZeroVector, {}));
	TestFalse(TEXT("weak rejected while strong runs"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Weak, FVector::ZeroVector, {}));
	TestTrue(TEXT("rejection keeps strong running"), Fixture.Modifier->IsFeedbackActive());
	TestTrue(TEXT("equal priority restarts"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Strong, FVector::ZeroVector, {}));

	Fixture.World->RealTimeSeconds = 11.5;
	TestTrue(TEXT("expired strong does not block weak"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Weak, FVector::ZeroVector, {}));
	TestTrue(TEXT("higher priority replaces weak"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Strong, FVector::ZeroVector, {}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAirChaseFeedbackPriorityTest, "VB.Unit.Camera.CombatFeedback.AuthoredAirChase",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAirChaseFeedbackPriorityTest::RunTest(const FString& Parameters)
{
	const UVBCombatFeedbackProfile* Chase = LoadObject<UVBCombatFeedbackProfile>(nullptr,
		TEXT("/Game/VB/Data/DA_CombatFeedback_AirChase"));
	const UVBCombatFeedbackProfile* Launch = LoadObject<UVBCombatFeedbackProfile>(nullptr,
		TEXT("/Game/VB/Data/DA_CombatFeedback_HeavyHit"));
	const UVBCombatFeedbackProfile* Parry = LoadObject<UVBCombatFeedbackProfile>(nullptr,
		TEXT("/Game/VB/Data/DA_CombatFeedback_Parry"));
	if (!TestNotNull(TEXT("authored chase"), Chase) || !TestNotNull(TEXT("authored launcher"), Launch)
		|| !TestNotNull(TEXT("authored parry"), Parry))
	{
		return false;
	}

	TestTrue(TEXT("chase replaces outgoing launch but yields to parry"), Chase->Screen.Priority >= Launch->Screen.Priority
		&& Chase->Screen.Priority < Parry->Screen.Priority);
	FFeedbackCameraFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	Fixture.World->RealTimeSeconds = 10.0;
	TestTrue(TEXT("launcher admitted"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Launch->Screen, FVector::ZeroVector, {}, Launch));
	TestTrue(TEXT("immediate chase transition admitted"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Chase->Screen, FVector::ZeroVector, {}, Chase));
	const UMaterialInstanceDynamic* Screen = Fixture.Modifier->GetScreenMaterialInstance();
	TestTrue(TEXT("chase installs its actual screen material"), Screen && Chase->Screen.Material
		&& Screen->GetMaterial() == Chase->Screen.Material->GetMaterial());
	TestTrue(TEXT("parry takes priority"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Parry->Screen, FVector::ZeroVector, {}, Parry));
	Screen = Fixture.Modifier->GetScreenMaterialInstance();
	TestFalse(TEXT("chase cannot replace parry"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Chase->Screen, FVector::ZeroVector, {}, Chase));
	TestEqual(TEXT("rejected chase preserves the active screen"), Fixture.Modifier->GetScreenMaterialInstance(), Screen);
	return true;
}

// 방향 반동: 카메라 축에 투영한 사건 방향으로 민다. 정면을 볼 때 오른쪽 = 요 +, 아래 = 피치 -. 방향이 없으면 안 민다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatFeedbackKickTest, "VB.Unit.Camera.CombatFeedback.DirectionalKick",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatFeedbackKickTest::RunTest(const FString& Parameters)
{
	FFeedbackCameraFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	FVBScreenImpactSettings Settings;
	Settings.CameraSeconds = 0.2f;
	Settings.EffectSeconds = 0.2f;
	Settings.RotationAmplitude = FRotator::ZeroRotator;
	Settings.FOVAmplitude = 0.0f;
	Settings.KickDegrees = 2.0f;
	Fixture.World->RealTimeSeconds = 50.0;

	const auto Kick = [&](const FVector& Direction)
	{
		Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, Direction, {});
		FMinimalViewInfo View;
		View.Rotation = FRotator::ZeroRotator;
		Fixture.Modifier->ModifyCamera(0.0f, View);
		return View.Rotation;
	};
	const FRotator Right = Kick(FVector::RightVector);
	TestTrue(TEXT("right swing yaws right"), FMath::IsNearlyEqual(Right.Yaw, 2.0f, 1e-3f) && FMath::IsNearlyZero(Right.Pitch, 1e-3f));
	const FRotator Down = Kick(FVector::DownVector);
	TestTrue(TEXT("down swing pitches down"), FMath::IsNearlyEqual(Down.Pitch, -2.0f, 1e-3f) && FMath::IsNearlyZero(Down.Yaw, 1e-3f));
	const FRotator None = Kick(FVector::ZeroVector);
	TestTrue(TEXT("no direction no kick"), None.IsNearlyZero(1e-3f));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatFeedbackSwingCueRecipientTest, "VB.Unit.Camera.CombatFeedback.SwingCueRecipient",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatFeedbackSwingCueRecipientTest::RunTest(const FString& Parameters)
{
	FFeedbackCameraFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AActor* ContextOwner = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("non-pawn context owner"), ContextOwner))
	{
		return false;
	}

	UVBCombatFeedbackProfile* Profile = NewObject<UVBCombatFeedbackProfile>();
	Profile->Screen.FOVAmplitude = 2.0f;
	Profile->Screen.RotationAmplitude = FRotator::ZeroRotator;
	FGameplayEffectContextHandle Context(new FGameplayEffectContext());
	Context.AddInstigator(ContextOwner, Fixture.Pawn);
	Context.AddSourceObject(Profile);
	FGameplayCueParameters CueParams;
	CueParams.EffectContext = Context;
	TestTrue(TEXT("explicit cue instigator remains unset"), !CueParams.Instigator.IsValid());
	TestTrue(TEXT("cue instigator resolves to non-pawn context owner"), CueParams.GetInstigator() == ContextOwner);
	TestTrue(TEXT("local-player gate accepts avatar effect causer"), CueParams.IsInstigatorLocallyControlledPlayer(ContextOwner));
	Fixture.Modifier->StopFeedback();
	Fixture.World->RealTimeSeconds = 20.0;
	const UVBGC_SwingImpactShake* Cue = GetDefault<UVBGC_SwingImpactShake>();
	TestTrue(TEXT("real swing cue executes"), Cue->OnExecute(ContextOwner, CueParams));
	TestTrue(TEXT("effect-causer pawn receives screen feedback"), Fixture.Modifier->IsFeedbackActive());
	FMinimalViewInfo View;
	View.FOV = 90.0f;
	Fixture.Modifier->ModifyCamera(0.0f, View);
	TestEqual(TEXT("cue produces actual FOV impulse"), View.FOV, 92.0f);

	Fixture.Modifier->StopFeedback();
	Context.AddInstigator(ContextOwner, ContextOwner);
	TestFalse(TEXT("non-pawn causer fails local-player gate"), CueParams.IsInstigatorLocallyControlledPlayer(ContextOwner));
	TestFalse(TEXT("unrelated context is rejected by cue"), Cue->OnExecute(ContextOwner, CueParams));
	TestFalse(TEXT("rejected cue leaves screen feedback stopped"), Fixture.Modifier->IsFeedbackActive());
	return true;
}

// 일반 스윙의 방향 흔들림: 좌우, 상하는 반동, 앞으로 지르는 성분은 시야각, 무기 무게는 큐 RawMagnitude 로 곱한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatFeedbackSwingWeightTest, "VB.Unit.Camera.CombatFeedback.SwingDirectionWeight",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatFeedbackSwingWeightTest::RunTest(const FString& Parameters)
{
	UVBCombatFeedbackModifier::FDirectionalKick K;
	K = UVBCombatFeedbackModifier::ComputeDirectionalKick(FVector::ForwardVector, FRotator::ZeroRotator, 2.0f, 1.5f);
	TestTrue(TEXT("a thrust narrows the view and does not turn it"), FMath::IsNearlyEqual(K.FOV, -1.5f)
		&& FMath::IsNearlyZero(K.Yaw) && FMath::IsNearlyZero(K.Pitch));
	K = UVBCombatFeedbackModifier::ComputeDirectionalKick(FVector::RightVector, FRotator::ZeroRotator, 2.0f, 1.5f);
	TestTrue(TEXT("a slash to the right yaws right, no FOV"), FMath::IsNearlyEqual(K.Yaw, 2.0f) && FMath::IsNearlyZero(K.FOV));
	K = UVBCombatFeedbackModifier::ComputeDirectionalKick(FVector::DownVector, FRotator::ZeroRotator, 2.0f, 1.5f);
	TestTrue(TEXT("a chop pitches down"), FMath::IsNearlyEqual(K.Pitch, -2.0f) && FMath::IsNearlyZero(K.Yaw));
	// 카메라가 오른쪽(요 90)을 보면 월드 정면 스윙은 화면 왼쪽으로 민다 - 투영은 월드가 아니라 지금 시선 기준이다.
	K = UVBCombatFeedbackModifier::ComputeDirectionalKick(FVector::ForwardVector, FRotator(0.0f, 90.0f, 0.0f), 2.0f, 1.5f);
	TestTrue(TEXT("the projection follows the current view"), FMath::IsNearlyEqual(K.Yaw, -2.0f, 1e-3f)
		&& FMath::IsNearlyZero(K.FOV, 1e-3f));

	FVBScreenImpactSettings Base;
	Base.KickDegrees = 2.0f;
	Base.KickForwardFOV = 1.5f;
	Base.FOVAmplitude = 1.0f;
	Base.RotationAmplitude = FRotator(1.0f, 1.0f, 1.0f);
	const FVBScreenImpactSettings Blade = UVBCombatFeedbackModifier::ScaleSettings(Base, 0.7f);
	TestTrue(TEXT("weapon weight scales kick, forward FOV, FOV and shake"), FMath::IsNearlyEqual(Blade.KickDegrees, 1.4f)
		&& FMath::IsNearlyEqual(Blade.KickForwardFOV, 1.05f) && FMath::IsNearlyEqual(Blade.FOVAmplitude,
			0.7f) && FMath::IsNearlyEqual(Blade.RotationAmplitude.Pitch, 0.7f));
	TestEqual(TEXT("an unknown weight leaves the profile as authored"),
		UVBCombatFeedbackModifier::ScaleSettings(Base, NAN).KickDegrees, 2.0f);
	TestEqual(TEXT("a negative weight never flips the kick"), UVBCombatFeedbackModifier::ScaleSettings(Base, -1.0f).KickDegrees, 2.0f);

	// 실제 스윙 큐를 지나 화면까지: RawMagnitude 0.7 이면 찌르기의 시야각이 0.7 배, RawMagnitude 0(안 실음)이면 1 배.
	FFeedbackCameraFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UVBCombatFeedbackProfile* Profile = NewObject<UVBCombatFeedbackProfile>();
	Profile->Screen.FOVAmplitude = 0.0f;
	Profile->Screen.RotationAmplitude = FRotator::ZeroRotator;
	Profile->Screen.KickDegrees = 2.0f;
	Profile->Screen.KickForwardFOV = 2.0f;
	FGameplayEffectContextHandle Context(new FGameplayEffectContext());
	Context.AddInstigator(Fixture.Pawn, Fixture.Pawn);
	Context.AddSourceObject(Profile);
	FGameplayCueParameters CueParams;
	CueParams.EffectContext = Context;
	CueParams.Instigator = Fixture.Pawn;
	CueParams.Normal = FVector::ForwardVector;
	const UVBGC_SwingImpactShake* Cue = GetDefault<UVBGC_SwingImpactShake>();
	const auto ViewFOV = [&](float RawMagnitude)
	{
		Fixture.Modifier->StopFeedback();
		Fixture.World->RealTimeSeconds += 10.0;
		CueParams.RawMagnitude = RawMagnitude;
		const bool bExecuted = Cue->OnExecute(Fixture.Pawn, CueParams);
		FMinimalViewInfo View;
		View.FOV = 90.0f;
		View.Rotation = FRotator::ZeroRotator;
		Fixture.Modifier->ModifyCamera(0.0f, View);
		return bExecuted ? View.FOV : -1.0f;
	};
	TestTrue(TEXT("Blade weight narrows a thrust by 0.7 x"), FMath::IsNearlyEqual(ViewFOV(0.7f), 88.6f, 1e-3f));
	TestTrue(TEXT("no weight in the cue = authored strength"), FMath::IsNearlyEqual(ViewFOV(0.0f), 88.0f, 1e-3f));
	return true;
}

// 처형이 끝날 때 장면이 아직 화면을 담당할 때만 되돌린다 - 그 사이 다른 화면, 다른 폰이 들어왔으면 그대로 둔다.
//   대조: OwnsView 를 늘 참으로 하면 "foreign view kept" 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherViewReleaseTest, "VB.Unit.Camera.FinisherViewRelease",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBFinisherViewReleaseTest::RunTest(const FString& Parameters)
{
	using FDirector = UVBFinisherDirectorSubsystem;
	FFeedbackCameraFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UVBFinisherDirectorSubsystem* Director = Fixture.World->GetSubsystem<UVBFinisherDirectorSubsystem>();
	AVBFinisherCamera* Ours = Fixture.World->SpawnActor<AVBFinisherCamera>();
	AActor* Foreign = Fixture.World->SpawnActor<AActor>();
	APawn* OtherPawn = Fixture.World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("director"), Director) || !TestNotNull(TEXT("our camera"), Ours) ||
		!TestNotNull(TEXT("foreign view"), Foreign) || !TestNotNull(TEXT("other pawn"), OtherPawn))
	{
		return false;
	}

	TestTrue(TEXT("our camera, same pawn"), FDirector::OwnsView(Ours, Ours, nullptr, Fixture.Pawn, Fixture.Pawn));
	TestTrue(TEXT("blending to our camera"), FDirector::OwnsView(Ours, Fixture.Pawn, Ours, Fixture.Pawn, Fixture.Pawn));
	TestFalse(TEXT("foreign current view"), FDirector::OwnsView(Ours, Foreign, nullptr, Fixture.Pawn, Fixture.Pawn));
	TestFalse(TEXT("blending away to a foreign view"), FDirector::OwnsView(Ours, Ours, Foreign, Fixture.Pawn, Fixture.Pawn));
	TestFalse(TEXT("pawn changed"), FDirector::OwnsView(Ours, Ours, nullptr, Fixture.Pawn, OtherPawn));
	TestFalse(TEXT("no camera"), FDirector::OwnsView(nullptr, nullptr, nullptr, Fixture.Pawn, Fixture.Pawn));

	auto Seed = [&]()
	{
		FVBFinisherViewLease& Lease = Director->ViewLeases.AddDefaulted_GetRef();
		Lease.Controller = Fixture.Controller;
		Lease.Camera = Ours;
		Lease.Pawn = Fixture.Pawn;
		Director->bRunning = true;
	};
	const FRotator ForeignRotation(0.0f, 33.0f, 0.0f);
	Ours->SetActorRotation(FRotator(-10.0f, 120.0f, 0.0f));

	// 다른 화면이 들어온 뒤 끝남: 뷰타깃, 컨트롤 회전을 건드리지 않는다.
	Seed();
	Fixture.Camera->SetViewTarget(Foreign);
	Fixture.Controller->SetControlRotation(ForeignRotation);
	Director->End();
	TestTrue(TEXT("foreign view kept"), Fixture.Camera->GetViewTarget() == Foreign);
	TestTrue(TEXT("foreign control rotation kept"), Fixture.Controller->GetControlRotation().Equals(ForeignRotation, 0.01f));
	TestEqual(TEXT("leases cleared"), Director->ViewLeases.Num(), 0);

	// 우리 카메라가 화면을 담당한 채 끝남: 폰으로 돌아가고 컨트롤 회전을 카메라에 맞춘다.
	Ours = Fixture.World->SpawnActor<AVBFinisherCamera>();
	if (!TestNotNull(TEXT("second camera"), Ours))
	{
		return false;
	}

	Ours->SetActorRotation(FRotator(-10.0f, 120.0f, 0.0f));
	Seed();
	Fixture.Camera->SetViewTarget(Ours);
	Director->End();
	TestTrue(TEXT("own view returns to the pawn"), Fixture.Camera->GetViewTarget() == Fixture.Pawn);
	TestTrue(TEXT("control rotation follows the camera"), Fixture.Controller->GetControlRotation().Equals(Ours->GetViewRotation(), 0.01f));

	// 두 번째 End 는 아무것도 쓰지 않는다.
	Fixture.Camera->SetViewTarget(Foreign);
	Director->End();
	TestTrue(TEXT("duplicate end writes nothing"), Fixture.Camera->GetViewTarget() == Foreign);

	Ours = Fixture.World->SpawnActor<AVBFinisherCamera>();
	if (!TestNotNull(TEXT("obstructed camera"), Ours)) { return false; }
	Seed();
	Fixture.Camera->SetViewTarget(Ours);
	Director->ReleaseObstructedView(Ours);
	TestTrue(TEXT("unresolved obstruction returns to gameplay view immediately"), Fixture.Camera->GetViewTarget() == Fixture.Pawn);
	TestEqual(TEXT("obstructed viewer lease removed"), Director->ViewLeases.Num(), 0);
	TestTrue(TEXT("releasing one view does not stop finisher timeline"), Director->bRunning);
	Director->End();

	Ours = Fixture.World->SpawnActor<AVBFinisherCamera>();
	if (!TestNotNull(TEXT("foreign-view obstruction camera"), Ours)) { return false; }
	Seed();
	Fixture.Camera->SetViewTarget(Foreign);
	Fixture.Controller->SetControlRotation(ForeignRotation);
	Director->ReleaseObstructedView(Ours);
	TestTrue(TEXT("obstruction release preserves foreign view"), Fixture.Camera->GetViewTarget() == Foreign);
	TestTrue(TEXT("obstruction release preserves foreign control rotation"), Fixture.Controller->GetControlRotation().Equals(ForeignRotation, 0.01f));
	Director->End();
	return true;
}

namespace
{
	float ReadScalar(const UMaterialInstanceDynamic* Instance, FName Name)
	{
		if (Instance)
		{
			for (const FScalarParameterValue& Value : Instance->ScalarParameterValues)
			{
				if (Value.ParameterInfo.Name == Name)
				{
					return Value.ParameterValue;
				}
			}
		}

		return -1.0f;
	}

	// 시험용 직선 곡선(자산 측정값이 아니다): 0 에서 시작해 1/4 에서 최대, 끝에서 0.
	FRuntimeFloatCurve MakeRiseFadeEnvelope()
	{
		FRuntimeFloatCurve Envelope;
		FRichCurve& Curve = *Envelope.GetRichCurve();
		for (const FVector2f Key : {FVector2f(0.0f, 0.0f), FVector2f(0.25f, 1.0f), FVector2f(1.0f, 0.0f)})
		{
			Curve.SetKeyInterpMode(Curve.AddKey(Key.X, Key.Y), RCIM_Linear);
		}

		return Envelope;
	}

	FVBScreenImpactSettings MakeEnvelopeSettings()
	{
		FVBScreenImpactSettings Settings;
		Settings.CameraSeconds = 1.0f;
		Settings.EffectSeconds = 1.0f;
		Settings.FOVAmplitude = 2.0f;
		Settings.RotationAmplitude = FRotator::ZeroRotator;
		Settings.Material = UMaterial::GetDefaultMaterial(MD_PostProcess);
		Settings.MaterialEnvelope = MakeRiseFadeEnvelope();
		return Settings;
	}

	void SampleCamera(UVBCombatFeedbackModifier* Modifier)
	{
		FMinimalViewInfo View;
		View.FOV = 90.0f;
		Modifier->ModifyCamera(0.0f, View);
	}
}

// Director 연결만 심는다 - 장면 전체 복제, notify 저작은 통합 검사 몫이다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatFeedbackFinisherViewTest, "VB.Unit.Camera.CombatFeedback.FinisherView",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatFeedbackFinisherViewTest::RunTest(const FString& Parameters)
{
	FFeedbackCameraFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UVBFinisherDirectorSubsystem* Director = Fixture.World->GetSubsystem<UVBFinisherDirectorSubsystem>();
	AVBFinisherCamera* FinisherCamera = Fixture.World->SpawnActor<AVBFinisherCamera>();
	AVBFinisherCamera* UnregisteredCamera = Fixture.World->SpawnActor<AVBFinisherCamera>();
	AActor* Unrelated = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("director"), Director) || !TestNotNull(TEXT("finisher camera"), FinisherCamera) ||
		!TestNotNull(TEXT("unregistered camera"), UnregisteredCamera) || !TestNotNull(TEXT("unrelated view"), Unrelated))
	{
		return false;
	}

	Director->PendingAttacker = Fixture.Pawn;
	FVBFinisherViewLease& Lease = Director->ViewLeases.AddDefaulted_GetRef();
	Lease.Controller = Fixture.Controller;
	Lease.Camera = FinisherCamera;
	Lease.Pawn = Fixture.Pawn;
	Director->bRunning = true;
	Fixture.World->RealTimeSeconds = 20.0;
	FVBScreenImpactSettings Settings;
	Settings.FOVAmplitude = 2.0f;
	Settings.RotationAmplitude = FRotator::ZeroRotator;

	Fixture.Camera->SetViewTarget(Unrelated);
	TestFalse(TEXT("arbitrary cinematic rejected"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	Fixture.Camera->SetViewTarget(UnregisteredCamera);
	TestFalse(TEXT("camera class alone grants no permission"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	Fixture.Camera->SetViewTarget(FinisherCamera);
	Director->ViewLeases[0].Controller.Reset();
	TestFalse(TEXT("another controller's camera rejected"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	Director->ViewLeases[0].Controller = Fixture.Controller;
	Director->PendingAttacker.Reset();
	TestFalse(TEXT("camera for another attacker rejected"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	Director->PendingAttacker = Fixture.Pawn;

	UVBCombatFeedbackProfile* Profile = NewObject<UVBCombatFeedbackProfile>();
	Profile->Screen = Settings;
	FVBTimeDilationStep Slow;
	Slow.Scale = 0.1f;
	Slow.RealDuration = 0.2f;
	Profile->SlowMotionSteps.Add(Slow);
	UVBTimeDilationSubsystem* Time = Fixture.World->GetSubsystem<UVBTimeDilationSubsystem>();
	if (!TestNotNull(TEXT("time subsystem"), Time))
	{
		return false;
	}

	Time->Push(TEXT("FinisherPhaseBTest"), 0.45f);
	FVBCombatFeedbackEvent Event;
	Event.Profile = Profile;
	Event.Recipient = Fixture.Pawn;
	Event.bWorldEffects = false;
	TestTrue(TEXT("existing feedback consumer accepts own finisher view"),
		UVBGC_CombatFeedback::PlayFeedback(Fixture.World, Event, TEXT("FinisherViewTest")));
	TestEqual(TEXT("contact accent preserves Director slow instead of stacking profile slow"), Time->GetAppliedScale(), 0.45f);
	Time->Pop(TEXT("FinisherPhaseBTest"));
	Fixture.Modifier->Tick(0.0f);
	TestTrue(TEXT("finisher impulse survives tick"), Fixture.Modifier->IsFeedbackActive());
	FMinimalViewInfo View;
	View.FOV = 90.0f;
	Fixture.Modifier->ModifyCamera(0.0f, View);
	TestEqual(TEXT("finisher view receives real camera impulse"), View.FOV, 92.0f);
	Director->bRunning = false;
	Fixture.Modifier->Tick(0.0f);
	TestTrue(TEXT("scene end cancels even while camera actor remains"), Fixture.Modifier->IsDisabled());
	Director->bRunning = true;
	TestTrue(TEXT("new finisher impulse admitted"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	Fixture.Camera->SetViewTarget(Fixture.Pawn);
	FMinimalViewInfo ChangedView;
	ChangedView.FOV = 90.0f;
	Fixture.Modifier->ModifyCamera(0.0f, ChangedView);
	TestTrue(TEXT("impulse does not follow return to pawn"), Fixture.Modifier->IsDisabled());
	TestEqual(TEXT("return view left intact"), ChangedView.FOV, 90.0f);

	TestTrue(TEXT("ordinary pawn feedback unchanged"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	Fixture.Camera->SetViewTarget(FinisherCamera);
	Fixture.Modifier->Tick(0.0f);
	TestTrue(TEXT("old ordinary impulse does not transfer to new cinematic"), Fixture.Modifier->IsDisabled());
	Fixture.Camera->SetViewTarget(Fixture.Pawn);
	TestTrue(TEXT("ordinary feedback path still runs"), UVBGC_CombatFeedback::PlayFeedback(Fixture.World, Event, TEXT("OrdinaryViewTest")));
	TestEqual(TEXT("ordinary profile slow remains supported"), Time->GetAppliedScale(), 0.1f);
	Time->Pop(TEXT("CombatFeedback"));

	// 넘김을 허락한 사건은 폰 -> 감독이 등록한 처형 카메라 -> 폰을 오가도 시작 시각과 위상을 지킨다.
	FVBScreenImpactSettings Handoff = MakeEnvelopeSettings();
	Handoff.bFollowFinisherHandoff = true;
	Fixture.Camera->SetViewTarget(Fixture.Pawn);
	Fixture.World->RealTimeSeconds = 30.0;
	TestTrue(TEXT("handoff pulse admitted on pawn"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Handoff, FVector::ZeroVector, {}));
	const UMaterialInstanceDynamic* Instance = Fixture.Modifier->GetScreenMaterialInstance();
	if (!TestNotNull(TEXT("screen material instance"), Instance))
	{
		return false;
	}

	Fixture.Camera->SetViewTarget(FinisherCamera);
	Fixture.World->RealTimeSeconds = 30.2;
	Fixture.Modifier->Tick(0.0f);
	FMinimalViewInfo OnFinisher;
	OnFinisher.FOV = 90.0f;
	Fixture.Modifier->ModifyCamera(0.0f, OnFinisher);
	TestTrue(TEXT("handoff keeps the pulse on the registered finisher camera"), Fixture.Modifier->IsFeedbackActive()
		&& OnFinisher.FOV > 90.0f);
	TestTrue(TEXT("handoff age continues from the original start"),
		FMath::IsNearlyEqual(ReadScalar(Instance, TEXT("ParryAge")), 0.2f, 1.0e-4f));
	Fixture.Camera->SetViewTarget(Fixture.Pawn);
	Fixture.World->RealTimeSeconds = 30.3;
	Fixture.Modifier->Tick(0.0f);
	SampleCamera(Fixture.Modifier);
	TestTrue(TEXT("return to the same pawn keeps the phase"), Fixture.Modifier->IsFeedbackActive() &&
		FMath::IsNearlyEqual(ReadScalar(Instance, TEXT("ParryAge")), 0.3f, 1.0e-4f));

	// 넘김을 허락해도 등록 안 된 뷰, 다른 컨트롤러, 끝난 장면, 빙의 변경은 끊는다.
	auto StartOnPawn = [&]()
	{
		Fixture.Camera->SetViewTarget(Fixture.Pawn);
		return Fixture.Modifier->StartFeedback(Fixture.Pawn, Handoff, FVector::ZeroVector, {});
	};
	auto ExpectCancelled = [&](const TCHAR* What)
	{
		Fixture.Modifier->Tick(0.0f);
		TestTrue(What, Fixture.Modifier->IsDisabled());
		TestEqual(FString(What) + TEXT(" clears strength"),
			ReadScalar(Fixture.Modifier->GetScreenMaterialInstance(), TEXT("ParryAmount")), 0.0f);
	};
	TestTrue(TEXT("untrusted setup 1"), StartOnPawn());
	Fixture.Camera->SetViewTarget(UnregisteredCamera);
	ExpectCancelled(TEXT("unregistered camera cancels the handoff"));
	TestTrue(TEXT("untrusted setup 2"), StartOnPawn());
	Fixture.Camera->SetViewTarget(FinisherCamera);
	Director->ViewLeases[0].Controller.Reset();
	ExpectCancelled(TEXT("camera leased to another controller cancels the handoff"));
	Director->ViewLeases[0].Controller = Fixture.Controller;
	TestTrue(TEXT("untrusted setup 3"), StartOnPawn());
	Fixture.Camera->SetViewTarget(FinisherCamera);
	Director->bRunning = false;
	ExpectCancelled(TEXT("scene end cancels the handoff"));
	Director->bRunning = true;
	TestTrue(TEXT("untrusted setup 4"), StartOnPawn());
	Fixture.Controller->UnPossess();
	ExpectCancelled(TEXT("possession change cancels the handoff"));
	return true;
}


// 화면 재질의 시간 모양: 곡선이 있으면 세기 0, 나이 0 에서 시작해 오르고 내린다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatFeedbackMaterialEnvelopeTest, "VB.Unit.Camera.CombatFeedback.MaterialEnvelopePlayback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatFeedbackMaterialEnvelopeTest::RunTest(const FString& Parameters)
{
	FFeedbackCameraFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	const FVBScreenImpactSettings Settings = MakeEnvelopeSettings();
	if (!TestNotNull(TEXT("post-process test material"), Settings.Material.Get()))
	{
		return false;
	}

	Fixture.World->RealTimeSeconds = 50.0;
	TestTrue(TEXT("curved pulse admitted"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	const UMaterialInstanceDynamic* Instance = Fixture.Modifier->GetScreenMaterialInstance();
	if (!TestNotNull(TEXT("screen material instance"), Instance))
	{
		return false;
	}

	TestEqual(TEXT("start writes zero strength"), ReadScalar(Instance, TEXT("ParryAmount")), 0.0f);
	TestEqual(TEXT("start writes age zero"), ReadScalar(Instance, TEXT("ParryAge")), 0.0f);

	SampleCamera(Fixture.Modifier);
	TestEqual(TEXT("first frame is still zero"), ReadScalar(Instance, TEXT("ParryAmount")), 0.0f);
	Fixture.World->RealTimeSeconds = 50.25;
	SampleCamera(Fixture.Modifier);
	const float Peak = ReadScalar(Instance, TEXT("ParryAmount"));
	TestTrue(TEXT("peak is positive"), Peak > 0.0f);
	TestTrue(TEXT("age follows the real clock"), FMath::IsNearlyEqual(ReadScalar(Instance, TEXT("ParryAge")), 0.25f, 1.0e-4f));
	Fixture.World->RealTimeSeconds = 50.125;
	SampleCamera(Fixture.Modifier);
	TestTrue(TEXT("rise is half the peak"), FMath::IsNearlyEqual(ReadScalar(Instance, TEXT("ParryAmount")), Peak * 0.5f, 1.0e-3f));
	Fixture.World->RealTimeSeconds = 50.625;
	SampleCamera(Fixture.Modifier);
	TestTrue(TEXT("fade is half the peak"), FMath::IsNearlyEqual(ReadScalar(Instance, TEXT("ParryAmount")), Peak * 0.5f, 1.0e-3f));
	Fixture.World->RealTimeSeconds = 50.99;
	SampleCamera(Fixture.Modifier);
	TestTrue(TEXT("end approaches neutral"), ReadScalar(Instance, TEXT("ParryAmount")) < Peak * 0.05f);
	Fixture.World->RealTimeSeconds = 51.001;
	Fixture.Modifier->Tick(0.0f);
	TestTrue(TEXT("expires on the real clock"), Fixture.Modifier->IsDisabled());
	TestEqual(TEXT("end is neutral"), ReadScalar(Instance, TEXT("ParryAmount")), 0.0f);

	// 같은 재질이면 인스턴스를 다시 쓴다 - 지난 사건의 나이(0.99)가 남으면 안 된다.
	Fixture.World->RealTimeSeconds = 60.0;
	TestTrue(TEXT("second pulse admitted"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Settings, FVector::ZeroVector, {}));
	TestTrue(TEXT("instance reused"), Fixture.Modifier->GetScreenMaterialInstance() == Instance);
	TestEqual(TEXT("reused instance restarts at zero strength"), ReadScalar(Instance, TEXT("ParryAmount")), 0.0f);
	TestEqual(TEXT("reused instance restarts at age zero"), ReadScalar(Instance, TEXT("ParryAge")), 0.0f);

	// 곡선이 없으면 시작이 최대다.
	FVBScreenImpactSettings Legacy = Settings;
	Legacy.MaterialEnvelope = FRuntimeFloatCurve();
	TestTrue(TEXT("legacy pulse admitted"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Legacy, FVector::ZeroVector, {}));
	TestTrue(TEXT("legacy starts at full strength"), ReadScalar(Instance, TEXT("ParryAmount")) > 0.0f);
	TestEqual(TEXT("legacy also restarts age"), ReadScalar(Instance, TEXT("ParryAge")), 0.0f);
	return true;
}

// 같은 프로필, 같은 우선순위의 연타는 진행 중인 것의 위상을 지키고 슬로우를 다시 걸지 않는다. 새 FX 는 넘겨받지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatFeedbackKeepPhaseTest, "VB.Unit.Camera.CombatFeedback.SameProfileKeepPhase",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatFeedbackKeepPhaseTest::RunTest(const FString& Parameters)
{
	FFeedbackCameraFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UVBTimeDilationSubsystem* Time = Fixture.World->GetSubsystem<UVBTimeDilationSubsystem>();
	if (!TestNotNull(TEXT("time subsystem"), Time))
	{
		return false;
	}

	UVBCombatFeedbackProfile* Profile = NewObject<UVBCombatFeedbackProfile>();
	Profile->Screen = MakeEnvelopeSettings();
	Profile->Screen.bKeepPhaseOnRetrigger = true;
	FVBTimeDilationStep Slow;
	Slow.Scale = 0.1f;
	Slow.RealDuration = 0.2f;
	Profile->SlowMotionSteps.Add(Slow);
	FVBCombatFeedbackEvent Event;
	Event.Profile = Profile;
	Event.Recipient = Fixture.Pawn;
	Event.bWorldEffects = false;

	Fixture.World->RealTimeSeconds = 30.0;
	TestTrue(TEXT("first event plays"), UVBGC_CombatFeedback::PlayFeedback(Fixture.World, Event, TEXT("KeepPhaseTest")));
	TestEqual(TEXT("first event slows"), Time->GetAppliedScale(), 0.1f);
	Time->Pop(TEXT("CombatFeedback"));
	const UMaterialInstanceDynamic* Instance = Fixture.Modifier->GetScreenMaterialInstance();

	Fixture.World->RealTimeSeconds = 30.2;
	TestFalse(TEXT("same-profile retrigger is coalesced"), UVBGC_CombatFeedback::PlayFeedback(Fixture.World, Event, TEXT("KeepPhaseTest")));
	TestEqual(TEXT("coalesced retrigger does not restart the slow"), Time->GetAppliedScale(), 1.0f);
	TestTrue(TEXT("running pulse kept"), Fixture.Modifier->IsFeedbackActive());
	SampleCamera(Fixture.Modifier);
	TestTrue(TEXT("age keeps the original start"), FMath::IsNearlyEqual(ReadScalar(Instance, TEXT("ParryAge")), 0.2f, 1.0e-4f));

	// 거절된 연타가 낸 FX 는 화면이 넘겨받지 않는다 - 게임 시계로 제 수명을 돈다.
	UNiagaraComponent* Fresh = NewObject<UNiagaraComponent>(Fixture.Pawn);
	const ENiagaraAgeUpdateMode FreshMode = Fresh->GetAgeUpdateMode();
	TestFalse(TEXT("direct retrigger is coalesced"),
		Fixture.Modifier->StartFeedback(Fixture.Pawn, Profile->Screen, FVector::ZeroVector, {Fresh}, Profile));
	TestEqual(TEXT("coalesced retrigger leaves its effect alone"), Fresh->GetAgeUpdateMode(), FreshMode);

	UVBCombatFeedbackProfile* Other = DuplicateObject(Profile, GetTransientPackage());
	FVBCombatFeedbackEvent OtherEvent = Event;
	OtherEvent.Profile = Other;
	TestTrue(TEXT("another profile at equal priority replaces"),
		UVBGC_CombatFeedback::PlayFeedback(Fixture.World, OtherEvent, TEXT("KeepPhaseTest")));
	Time->Pop(TEXT("CombatFeedback"));
	SampleCamera(Fixture.Modifier);
	TestEqual(TEXT("replacement restarts age"), ReadScalar(Instance, TEXT("ParryAge")), 0.0f);

	Fixture.World->RealTimeSeconds = 32.0;
	TestTrue(TEXT("expired same profile is admitted again"),
		UVBGC_CombatFeedback::PlayFeedback(Fixture.World, OtherEvent, TEXT("KeepPhaseTest")));
	Time->Pop(TEXT("CombatFeedback"));

	// 넘김을 끈 프로필은 같은 우선순위 연타가 처음부터 다시 건다.
	Profile->Screen.bKeepPhaseOnRetrigger = false;
	Fixture.World->RealTimeSeconds = 40.0;
	TestTrue(TEXT("legacy first"), UVBGC_CombatFeedback::PlayFeedback(Fixture.World, Event, TEXT("KeepPhaseTest")));
	Fixture.World->RealTimeSeconds = 40.2;
	TestTrue(TEXT("legacy retrigger restarts"), UVBGC_CombatFeedback::PlayFeedback(Fixture.World, Event, TEXT("KeepPhaseTest")));
	Time->Pop(TEXT("CombatFeedback"));
	return true;
}

// 잘못된 곡선은 저장 검사와 실행이 같이 거절한다. 다른 감쇠로 바꿔 내지 않고, 진행 중인 사건도 끊지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatFeedbackInvalidCurveTest, "VB.Unit.Camera.CombatFeedback.InvalidCurve",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatFeedbackInvalidCurveTest::RunTest(const FString& Parameters)
{
	const FVBScreenImpactSettings Good = MakeEnvelopeSettings();
	FString Reason;
	TestTrue(TEXT("rise/fade curve valid"), UVBCombatFeedbackModifier::ValidateMaterialEnvelope(Good, Reason));
	TestTrue(TEXT("no curve is legacy and valid"), UVBCombatFeedbackModifier::ValidateMaterialEnvelope(FVBScreenImpactSettings(), Reason));

	TArray<TPair<FString, FVBScreenImpactSettings>> Bad;
	{
		FVBScreenImpactSettings S = Good;
		S.MaterialEnvelope = FRuntimeFloatCurve();
		S.MaterialEnvelope.GetRichCurve()->AddKey(0.0f, 1.0f);
		S.MaterialEnvelope.GetRichCurve()->AddKey(1.0f, 0.0f);
		Bad.Add({TEXT("instant-on start"), S});
	}

	{
		FVBScreenImpactSettings S = Good;
		S.MaterialEnvelope.GetRichCurve()->UpdateOrAddKey(1.0f, 0.5f);
		Bad.Add({TEXT("non-neutral end"), S});
	}

	{
		FVBScreenImpactSettings S = Good;
		S.EffectSeconds = 0.0f;
		Bad.Add({TEXT("zero duration"), S});
	}

	{
		FVBScreenImpactSettings S = Good;
		S.MaterialEnvelope.GetRichCurve()->UpdateOrAddKey(0.25f, NAN);
		Bad.Add({TEXT("non-finite value"), S});
	}

	{
		// 키는 모두 0 이지만 3차 보간이 사이에서 1 을 넘는다 - 키만 보면 놓친다.
		FVBScreenImpactSettings S = Good;
		S.MaterialEnvelope = FRuntimeFloatCurve();
		FRichCurve& Curve = *S.MaterialEnvelope.GetRichCurve();
		const FKeyHandle Start = Curve.AddKey(0.0f, 0.0f);
		Curve.AddKey(1.0f, 0.0f);
		Curve.SetKeyInterpMode(Start, RCIM_Cubic, false);
		Curve.SetKeyTangentMode(Start, RCTM_User, false);
		Curve.GetKey(Start).LeaveTangent = 10.0f;
		Bad.Add({TEXT("interpolation overshoot"), S});
	}

	{
		FVBScreenImpactSettings S = Good;
		S.MaterialEnvelope = FRuntimeFloatCurve();
		S.MaterialEnvelope.ExternalCurve = NewObject<UCurveFloat>();
		Bad.Add({TEXT("empty external curve"), S});
	}

	for (const TPair<FString, FVBScreenImpactSettings>& Case : Bad)
	{
		FString CaseReason;
		TestFalse(Case.Key + TEXT(" rejected"), UVBCombatFeedbackModifier::ValidateMaterialEnvelope(Case.Value, CaseReason));
		TestFalse(Case.Key + TEXT(" names a reason"), CaseReason.IsEmpty());
	}

#if WITH_EDITOR
	// 슬로우를 넣어 빈 프로필 오류와 섞이지 않게 한다 - 곡선만이 판정을 가른다.
	UVBCombatFeedbackProfile* Profile = NewObject<UVBCombatFeedbackProfile>();
	FVBTimeDilationStep Slow;
	Slow.Scale = 0.5f;
	Slow.RealDuration = 0.1f;
	Profile->SlowMotionSteps.Add(Slow);
	Profile->Screen = Good;
	FDataValidationContext GoodContext;
	TestTrue(TEXT("profile save check accepts the good curve"), Profile->IsDataValid(GoodContext) != EDataValidationResult::Invalid);
	Profile->Screen = Bad[0].Value;
	FDataValidationContext BadContext;
	TestTrue(TEXT("profile save check rejects the same curve"), Profile->IsDataValid(BadContext) == EDataValidationResult::Invalid);
#endif

	FFeedbackCameraFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	Fixture.World->RealTimeSeconds = 70.0;
	TestTrue(TEXT("valid pulse running"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Good, FVector::ZeroVector, {}));
	AddExpectedError(TEXT("CombatFeedback:"), EAutomationExpectedErrorFlags::Contains, 1);
	FVBScreenImpactSettings Louder = Bad[0].Value;
	Louder.Priority = Good.Priority + 1;
	TestFalse(TEXT("runtime rejects the invalid curve"), Fixture.Modifier->StartFeedback(Fixture.Pawn, Louder, FVector::ZeroVector, {}));
	TestTrue(TEXT("rejection keeps the running pulse"), Fixture.Modifier->IsFeedbackActive());
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSwingCombatScreenOnlyValidationTest, "VB.Unit.Combat.SwingCombatProfile.ScreenOnlyValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSwingCombatScreenOnlyValidationTest::RunTest(const FString& Parameters)
{
	UVBSwingCombatProfile* Profile = NewObject<UVBSwingCombatProfile>();
	FDataValidationContext Empty;
	TestTrue(TEXT("identity profile without feedback remains invalid"), Profile->IsDataValid(Empty) == EDataValidationResult::Invalid);
	Profile->ScreenFeedback = NewObject<UVBCombatFeedbackProfile>();
	FDataValidationContext ScreenOnly;
	TestTrue(TEXT("screen-only profile is meaningful"), Profile->IsDataValid(ScreenOnly) != EDataValidationResult::Invalid);
	TestEqual(TEXT("screen-only profile has no validation errors"), ScreenOnly.GetNumErrors(), 0);
	Profile->Scales.DamageScale = -1.0f;
	FDataValidationContext BadScale;
	TestTrue(TEXT("feedback does not bypass numeric validation"), Profile->IsDataValid(BadScale) == EDataValidationResult::Invalid);
	return true;
}
#endif
#endif
