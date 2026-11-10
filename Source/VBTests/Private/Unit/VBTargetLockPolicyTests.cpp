#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Character/VBTargetLockPolicy.h"
#include "Character/VBTargetLockComponent.h"
#include "Engine/World.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Data/VBTargetLockConfig.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLockFramingTest, "VB.LockOn.CameraFraming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FVBLockFramingTest::RunTest(const FString& Parameters)
{
	FVBTargetLockFraming Framing;
	TestTrue(TEXT("default rear framing is valid"), Framing.IsValid());
	const FRotator View = Framing.DesiredRotation(140.0f, FVector(300, 0, 400), FVector::ZeroVector);
	TestNearlyEqual(TEXT("rear view follows owner yaw despite the old camera side"), View.Yaw, 140.0);
	TestTrue(TEXT("a falling target can remain below the ordinary lock pitch limit"), View.Pitch < -20.0);
	TestTrue(TEXT("downward view remains bounded"), View.Pitch >= Framing.MinPitch);
	Framing.MinPitch = 30.0f;
	TestFalse(TEXT("reversed pitch limits are rejected"), Framing.IsValid());
	Framing.MinPitch = -65.0f;
	Framing.InterpSpeed = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("nonfinite interpolation cannot reach control rotation"), Framing.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLockInputPolicyTest, "VB.LockOn.InputLatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBLockInputPolicyTest::RunTest(const FString& Parameters)
{
	VBTargetLockPolicy::FStickLatch Latch;
	TestEqual(TEXT("Below press does not switch"), Latch.Update(0.6f, 0.65f, 0.25f), 0);
	TestEqual(TEXT("Cross press switches once"), Latch.Update(0.7f, 0.65f, 0.25f), 1);
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		TestEqual(TEXT("Holding cannot repeat"), Latch.Update(1.0f, 0.65f, 0.25f), 0);
	}

	TestEqual(TEXT("Noise above release keeps latch"), Latch.Update(0.3f, 0.65f, 0.25f), 0);
	TestEqual(TEXT("Still held after noise"), Latch.Update(0.9f, 0.65f, 0.25f), 0);
	TestEqual(TEXT("Neutral rearms"), Latch.Update(0.2f, 0.65f, 0.25f), 0);
	TestEqual(TEXT("Second gesture switches"), Latch.Update(0.9f, 0.65f, 0.25f), 1);
	TestEqual(TEXT("Crossing neutral between samples can reverse"), Latch.Update(-0.9f, 0.65f, 0.25f), -1);
	TestEqual(TEXT("NaN cannot switch"), Latch.Update(std::numeric_limits<float>::quiet_NaN(), 0.65f, 0.25f), 0);
	Latch.Reset();
	TestEqual(TEXT("Device reset rearms"), Latch.Update(-0.9f, 0.65f, 0.25f), -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLockSelectionPolicyTest, "VB.LockOn.SelectionBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBLockSelectionPolicyTest::RunTest(const FString& Parameters)
{
	using namespace VBTargetLockPolicy;
	TestFalse(TEXT("Beyond acquisition distance"), IsWithinDistance(FMath::Square(2200.0), 2000.0f));
	TestTrue(TEXT("Same target remains within retention distance"), IsWithinDistance(FMath::Square(2200.0), 2500.0f));
	TestTrue(TEXT("Exact retention boundary"), IsWithinDistance(FMath::Square(2500.0), 2500.0f));
	TestFalse(TEXT("Past retention boundary"), IsWithinDistance(FMath::Square(2501.0), 2500.0f));
	const FVector2D A(0.35, 0.5), B(0.55, 0.5), C(0.80, 0.5);
	TestTrue(TEXT("Screen neighbour precedes farther screen target"), ScreenScore(A, B, 1.5f) < ScreenScore(A, C, 1.5f));
	TestTrue(TEXT("Right of current, even on left half of screen"), IsOnSide(FVector2D(0.2,0.5), FVector2D(0.3,0.5), 1, 0.015f));
	TestFalse(TEXT("Wrong direction excluded"), IsOnSide(B, A, 1, 0.015f));
	TestFalse(TEXT("Vertically overlapping target excluded"), IsOnSide(A, FVector2D(A.X, 0.8), 1, 0.015f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLockAssetPolicyTest, "VB.LockOn.AuthoredInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBLockAssetPolicyTest::RunTest(const FString& Parameters)
{
	const UVBTargetLockConfig* Config = LoadObject<UVBTargetLockConfig>(nullptr,
		TEXT("/Game/VB/Data/DA_TargetLock_Default.DA_TargetLock_Default"));
	if (!TestNotNull(TEXT("Lock config"), Config))
	{
		return false;
	}

	TestFalse(TEXT("Ordinary hit does not create persistent lock"), Config->bAutoLockOnHit);
	TestFalse(TEXT("Death does not automatically select another enemy"), Config->bAutoRetargetOnDeath);
	TestEqual(TEXT("Authored acquisition eight metres"), Config->MaxLockDistance, 800.0f);
	TestEqual(TEXT("Authored search eight metres"), Config->TargetSearchRadius, 800.0f);
	TestEqual(TEXT("Authored retention twelve metres"), Config->LockBreakDistance, 1200.0f);
	TestTrue(TEXT("Acquisition has retention hysteresis"), Config->LockBreakDistance > Config->MaxLockDistance);
	TestTrue(TEXT("Hit recenter enabled independently"), Config->bRecenterCameraOnHit);
	TestTrue(TEXT("Recenter ends before next permitted burst"), Config->HitRecenterDuration > 0.0f
		&& Config->HitRecenterDuration < Config->HitRecenterQuietSeconds);
	TestTrue(TEXT("Release threshold below press"), Config->StickSwitchReleaseThreshold < Config->StickSwitchPressThreshold);
	TestTrue(TEXT("Retention wider than acquisition"), Config->LockBreakDistance > Config->MaxLockDistance);
	const UInputAction* Action = LoadObject<UInputAction>(nullptr, TEXT("/Game/VB/Input/Actions/IA_SwitchTarget.IA_SwitchTarget"));
	if (!TestNotNull(TEXT("Switch action"), Action))
	{
		return false;
	}

	TestFalse(TEXT("Switch axis cannot consume free look"), Action->bConsumeInput);
	TestEqual(TEXT("Signed direction type"), Action->ValueType, EInputActionValueType::Axis1D);
	for (const TCHAR* ContextName : {TEXT("IMC_KeyboardMouse"), TEXT("IMC_Gamepad")})
	{
		const UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr,
			*FString::Printf(TEXT("/Game/VB/Input/Contexts/%s.%s"), ContextName, ContextName));
		if (!TestNotNull(ContextName, Context))
		{
			continue;
		}

		const bool bPad = FString(ContextName) == TEXT("IMC_Gamepad");
		int32 Count = 0;
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Action != Action)
			{
				continue;
			}

			++Count;
			TestEqual(TEXT("No Pressed trigger hides analog neutral updates"), Mapping.Triggers.Num(), 0);
			if (bPad)
			{
				TestEqual(TEXT("Unscaled stick X"), Mapping.Key, EKeys::Gamepad_RightX);
				TestEqual(TEXT("No shared look sensitivity modifier"), Mapping.Modifiers.Num(), 0);
			}
			else
			{
				TestTrue(TEXT("Wheel only"), Mapping.Key == EKeys::MouseScrollUp || Mapping.Key == EKeys::MouseScrollDown);
				if (Mapping.Key == EKeys::MouseScrollDown)
				{
					TestTrue(TEXT("Wheel down supplies negative direction"), Mapping.Modifiers.Num() == 1
						&& Mapping.Modifiers[0]->IsA<UInputModifierNegate>());
				}
			}
		}

		TestEqual(TEXT("Exactly one pad axis or two wheel directions"), Count, bPad ? 1 : 2);
	}

	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitRecenterWindowTest, "VB.LockOn.HitRecenterWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBHitRecenterWindowTest::RunTest(const FString& Parameters)
{
	using namespace VBTargetLockPolicy;
	FHitRecenterWindow Window;
	TestTrue(TEXT("First hit after idle can assist"), Window.RegisterHit(10.0, 0.8f, 0.35f));
	Window.Start(10.0, 0.25f);
	TestEqual(TEXT("No initial snap"), Window.GetAlpha(10.0), 0.0);
	TestEqual(TEXT("Halfway easing"), Window.GetAlpha(10.125), 0.5);
	TestFalse(TEXT("Another hit cannot restart active assist"), Window.RegisterHit(10.1, 0.8f, 0.35f));
	TestEqual(TEXT("End time unchanged by hit"), Window.EndTime, 10.25);
	TestEqual(TEXT("Hitch cannot overshoot endpoint"), Window.GetAlpha(11.0), 1.0);
	Window.Cancel();
	for (int32 Hit = 1; Hit <= 8; ++Hit)
	{
		TestFalse(TEXT("Continuous combo cannot reacquire camera"), Window.RegisterHit(10.1 + Hit * 0.3, 0.8f, 0.35f));
	}

	TestTrue(TEXT("Quiet gap rearms a new burst"), Window.RegisterHit(13.4, 0.8f, 0.35f));
	Window.Start(13.4, 0.25f);
	Window.RecordInput(13.45);
	TestFalse(TEXT("Manual input immediately cancels"), Window.bActive);
	TestFalse(TEXT("Late hit during input grace cannot restart"), Window.RegisterHit(13.5, 0.0f, 0.35f));
	TestFalse(TEXT("Cancellation does not resume by time passing"), Window.bActive);
	TestTrue(TEXT("New burst after input and hit silence allowed"), Window.RegisterHit(14.4, 0.8f, 0.35f));
	Window.Start(14.4, 0.0f);
	TestFalse(TEXT("Zero duration never owns camera"), Window.bActive);
	TestEqual(TEXT("Zero duration has finite progress"), Window.GetAlpha(14.4), 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitRecenterVisibilityTest, "VB.LockOn.HitRecenterBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBHitRecenterVisibilityTest::RunTest(const FString& Parameters)
{
	using namespace VBTargetLockPolicy;
	const FVector2D Half(0.25, 0.30);
	TestTrue(TEXT("Central target needs no recenter"), IsInRecenterSafeFrame(FVector2D(0.5,0.5), Half));
	TestTrue(TEXT("Safe frame edge included"), IsInRecenterSafeFrame(FVector2D(0.75,0.5), Half));
	TestFalse(TEXT("Side target may need correction"), IsInRecenterSafeFrame(FVector2D(0.9,0.5), Half));
	TestFalse(TEXT("Vertical edge target may need correction"), IsInRecenterSafeFrame(FVector2D(0.5,0.95), Half));
	TestFalse(TEXT("Offscreen target is not safe"), IsInRecenterSafeFrame(FVector2D(1.1,0.5), Half));
	TestTrue(TEXT("Recent server hit accepted"), IsHitNotificationFresh(10.1, 10.0, 0.35f));
	TestTrue(TEXT("Small server clock skew tolerated"), IsHitNotificationFresh(9.95, 10.0, 0.35f));
	TestFalse(TEXT("Old packet cannot turn camera"), IsHitNotificationFresh(11.0, 10.0, 0.35f));
	TestFalse(TEXT("Unsynchronised future packet ignored"), IsHitNotificationFresh(9.0, 10.0, 0.35f));
	TestFalse(TEXT("Invalid timestamp ignored"), IsHitNotificationFresh(10.0, std::numeric_limits<double>::quiet_NaN(), 0.35f));
	return true;
}
// 플레이어 해제는 락 키와 뒤로 질주 둘뿐이다. 헤드리스 묶음에서 돌도록 앱 문맥에 둔다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLockReleasePolicyTest, "VB.LockOn.ReleaseIntent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLockReleasePolicyTest::RunTest(const FString& Parameters)
{
	using namespace VBTargetLockPolicy;
	TestTrue(TEXT("Retreat sprint"), IsRetreatSprint(true, true, FVector(-100, 0, 0), FVector(200, 0, 0), .5f));
	TestFalse(TEXT("Forward sprint keeps lock"), IsRetreatSprint(true, true, FVector(100, 0, 0), FVector(200, 0, 0), .5f));
	TestFalse(TEXT("Sideways sprint keeps lock"), IsRetreatSprint(true, true, FVector(0, 100, 0), FVector(200, 0, 0), .5f));
	TestFalse(TEXT("Dodge without sprint cannot release"), IsRetreatSprint(false, true, FVector(-100, 0, 0), FVector(200, 0, 0), .5f));
	TestFalse(TEXT("Air movement cannot release"), IsRetreatSprint(true, false, FVector(-100, 0, 0), FVector(200, 0, 0), .5f));
	TestFalse(TEXT("No input cannot release"), IsRetreatSprint(true, true, FVector::ZeroVector, FVector(200, 0, 0), .5f));
	return true;
}

// 락온 중 방향 공격 전환의 순수 정책. 헤드리스에서 돈다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDirectionalPickTest, "VB.Unit.LockOn.DirectionalPick",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDirectionalPickTest::RunTest(const FString& Parameters)
{
	using namespace VBTargetLockPolicy;
	const FDirectionalParams P;
	auto Dir = [](float Degrees) { return FVector2D(FMath::Cos(FMath::DegreesToRadians(Degrees)), FMath::Sin(FMath::DegreesToRadians(Degrees))); };
	auto Make = [&Dir](float Degrees, float Distance, bool bEligible = true, float Vertical = 0.0f)
	{
		FDirectionalCandidate C;
		C.GroundDirection = Dir(Degrees);
		C.Distance2D = Distance;
		C.VerticalDelta = Vertical;
		C.bEligible = bEligible;
		return C;
	};
	const FVector2D Current = Dir(0.0f); // 지금 대상 A 는 정면(0도)
	const TArray<FDirectionalCandidate> Side = { Make(90.0f, 300.0f) }; // B 는 오른쪽 90도
	TestEqual(TEXT("neutral input keeps A"), PickDirectionalTarget(FVector2D::ZeroVector, Current, true, Side, P), INDEX_NONE);
	TestEqual(TEXT("input below the magnitude keeps A"), PickDirectionalTarget(Dir(90.0f) * 0.3f, Current, true, Side, P), INDEX_NONE);
	TestEqual(TEXT("clear input toward B switches"), PickDirectionalTarget(Dir(90.0f), Current, true, Side, P), 0);
	TestEqual(TEXT("small aim error still finds B (within the cone)"), PickDirectionalTarget(Dir(70.0f), Current, true, Side, P), 0);
	TestEqual(TEXT("input toward A (keep cone) keeps A even if B exists"), PickDirectionalTarget(Dir(15.0f), Current, true, { Make(25.0f, 100.0f) }, P), INDEX_NONE);
	TestEqual(TEXT("B outside the candidate cone keeps A"), PickDirectionalTarget(Dir(90.0f), Current, true, { Make(150.0f, 200.0f) }, P), INDEX_NONE);
	// 각 이점(10도): 입력 30도면 A 와는 30도. B(55도)는 입력과 25도라 A 보다 5도만 나음 -> 유지. 입력 40도에 B(42도)는 2도 vs A 40도 -> 전환.
	TestEqual(TEXT("B must beat A's angle by the advantage - ambiguous strafe keeps A"), PickDirectionalTarget(Dir(30.0f), Current, true, { Make(55.0f, 200.0f) }, P), INDEX_NONE);
	TestEqual(TEXT("B clearly better aligned than A switches"), PickDirectionalTarget(Dir(40.0f), Current, true, { Make(42.0f, 200.0f) }, P), 0);
	TestEqual(TEXT("beyond max distance keeps A"), PickDirectionalTarget(Dir(90.0f), Current, true, { Make(90.0f, 700.0f) }, P), INDEX_NONE);
	TestEqual(TEXT("other floor (vertical) keeps A"), PickDirectionalTarget(Dir(90.0f), Current, true, { Make(90.0f, 200.0f, true, 200.0f) }, P), INDEX_NONE);
	TestEqual(TEXT("ineligible (dead/blocked/friendly) keeps A"), PickDirectionalTarget(Dir(90.0f), Current, true, { Make(90.0f, 200.0f, false) }, P), INDEX_NONE);
	// 방향이 거리보다 앞선다: 겨눈 방향의 먼 B(90도, 500) vs 옆의 가까운 C(60도, 100).
	const TArray<FDirectionalCandidate> FarAligned = { Make(60.0f, 100.0f), Make(90.0f, 500.0f) };
	TestEqual(TEXT("aligned far B beats off-axis near C"), PickDirectionalTarget(Dir(90.0f), Current, true, FarAligned, P), 1);
	// 동점 묶음(3도 안)에서는 가까운 쪽, 순서와 무관.
	const TArray<FDirectionalCandidate> TieAB = { Make(90.0f, 400.0f), Make(92.0f, 200.0f) };
	const TArray<FDirectionalCandidate> TieBA = { Make(92.0f, 200.0f), Make(90.0f, 400.0f) };
	TestEqual(TEXT("tie band picks the closer (order A,B)"), PickDirectionalTarget(Dir(90.0f), Current, true, TieAB, P), 1);
	TestEqual(TEXT("tie band picks the closer (order B,A)"), PickDirectionalTarget(Dir(90.0f), Current, true, TieBA, P), 0);
	TestEqual(TEXT("no candidates keeps A"), PickDirectionalTarget(Dir(90.0f), Current, true, {}, P), INDEX_NONE);
	TestEqual(TEXT("NaN input keeps A"),
		PickDirectionalTarget(FVector2D(std::numeric_limits<float>::quiet_NaN(), 0.0f), Current, true, Side, P), INDEX_NONE);

	// 입력 -> 월드 방향: UVBHeroComponent::HandleMove 와 같은 회전(앞 = yaw 의 X 축, 오른쪽 = Y 축). 카메라가 +Y 를 보면(yaw 90) 앞 키는 +Y, 오른쪽 키는 -X.
	TestTrue(TEXT("yaw 90 forward key points +Y"), WorldMoveIntent(FVector2D(0.0f, 1.0f), 90.0f).Equals(FVector2D(0.0f, 1.0f), 1e-4f));
	TestTrue(TEXT("yaw 90 right key points -X"), WorldMoveIntent(FVector2D(1.0f, 0.0f), 90.0f).Equals(FVector2D(-1.0f, 0.0f), 1e-4f));
	TestTrue(TEXT("yaw 0 right key points +Y"), WorldMoveIntent(FVector2D(1.0f, 0.0f), 0.0f).Equals(FVector2D(0.0f, 1.0f), 1e-4f));
	TestTrue(TEXT("keyboard diagonal is clamped to length 1"),
		FMath::IsNearlyEqual(WorldMoveIntent(FVector2D(1.0f, 1.0f), 30.0f).Size(), 1.0, 1e-4));
	TestTrue(TEXT("NaN yaw gives no direction"), WorldMoveIntent(FVector2D(1.0f, 0.0f), std::numeric_limits<float>::quiet_NaN()).IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLockRangePolicyTest, "VB.LockOn.AcquisitionRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBLockRangePolicyTest::RunTest(const FString& Parameters)
{
	using namespace VBTargetLockPolicy;
	TestEqual(TEXT("Search limits all acquisition routes"), AcquisitionRange(2000, 800), 800.0f);
	TestEqual(TEXT("Max range limits broad overlaps"), AcquisitionRange(800, 1500), 800.0f);
	TestEqual(TEXT("Invalid settings fail closed"), AcquisitionRange(-1, 800), 0.0f);
	TestTrue(TEXT("Eight metre boundary allowed"), IsWithinDistance(640000, AcquisitionRange(800, 800)));
	TestFalse(TEXT("Nine metre acquisition rejected"), IsWithinDistance(810000, AcquisitionRange(800, 800)));
	TestTrue(TEXT("Existing nine metre target retained"), IsWithinDistance(810000, 1200));
	TestTrue(TEXT("Front target selectable"), IsInAcquisitionCone(FVector::ForwardVector, FVector(1, .2, 0), 45));
	TestFalse(TEXT("Peripheral target excluded on initial lock"), IsInAcquisitionCone(FVector::ForwardVector, FVector(.2, 1, 0), 45));
	TestFalse(TEXT("Behind excluded"), IsInAcquisitionCone(FVector::ForwardVector, FVector(-1, 0, 0), 45));
	return true;
}
// 락온 시선 보정은 자유 시선과 같은 방향, 크기로 카메라를 돌려야 한다.
// 자유 시선은 APlayerController::AddYaw/AddPitchInput 의 legacy input scale 을 거치므로 날값을 쓰면 pitch 가 뒤집힌다.
// 기준은 복사한 숫자가 아니라 controller 자체다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLockLookConversionTest, "VB.LockOn.LookMatchesFreeLook",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLockLookConversionTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		OwnedPackage->SetDirtyFlag(false);
	};
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("controller"), Controller))
	{
		return false;
	}

	// FreeLook - 입력 한 번에 자유 시선이 실제로 적용하는 값을 controller 에서 읽는다.
	const auto FreeLook = [Controller](float Yaw, float Pitch)
	{
		Controller->RotationInput = FRotator::ZeroRotator;
		Controller->AddYawInput(Yaw);
		Controller->AddPitchInput(Pitch);
		return FVector2D(Controller->RotationInput.Yaw, Controller->RotationInput.Pitch);
	};
	for (const FVector2D Input : {FVector2D(1.0, 1.0), FVector2D(-0.4, 0.7), FVector2D(0.0, -1.0)})
	{
		const FVector2D Lock = UVBTargetLockComponent::LookInputToControlDegrees(Controller, Input.X, Input.Y);
		const FVector2D Free = FreeLook(Input.X, Input.Y);
		TestTrue(FString::Printf(TEXT("Lock-on equals free look for (%.1f, %.1f): lock (%.2f, %.2f) free (%.2f, %.2f)"),
			Input.X, Input.Y, Lock.X, Lock.Y, Free.X, Free.Y), Lock.Equals(Free, 1e-4));
	}
	// Invert Y 는 두 경로 앞에서 입력에 적용되므로 (UVBHeroComponent::HandleLook) 입력을 뒤집으면 둘 다 똑같이 뒤집힌다.
	const FVector2D Lock = UVBTargetLockComponent::LookInputToControlDegrees(Controller, 0.0f, 1.0f);
	const FVector2D Inverted = UVBTargetLockComponent::LookInputToControlDegrees(Controller, 0.0f, -1.0f);
	TestEqual(TEXT("Inverted input flips lock-on pitch"), Inverted.Y, -Lock.Y);
	TestEqual(TEXT("Inverted input flips free-look pitch"), FreeLook(0.0f, -1.0f).Y, -FreeLook(0.0f, 1.0f).Y);
	// 반대 대조 - 옛 날값 경로. legacy scale 아래서 날값은 자유 시선 값과 달라야 한다.
	// 같게 나오면 scale 이 바뀌어 이 시험이 변환을 더는 검사하지 못한다는 뜻이다.
	if (GetDefault<UInputSettings>()->bEnableLegacyInputScales)
	{
		TestFalse(TEXT("Raw pitch (old lock path) differs from free look under legacy scales"),
			FMath::IsNearlyEqual(1.0, FreeLook(0.0f, 1.0f).Y, 1e-4));
	}
	else
	{
		AddWarning(TEXT("Legacy input scales are off: raw and free look agree, the opposite case is not exercised"));
	}

	TestTrue(TEXT("No controller: raw input passes through"),
		UVBTargetLockComponent::LookInputToControlDegrees(nullptr, 0.5f, -0.5f).Equals(FVector2D(0.5, -0.5)));
	return true;
}
#endif
