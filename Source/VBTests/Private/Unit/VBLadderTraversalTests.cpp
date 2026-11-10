#include <limits>
#include "Misc/AutomationTest.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "Misc/ScopeExit.h"
#include "Animation/AnimMontage.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Character/VBCharacter.h"
#include "Character/VBPawnExtensionComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Data/VBClimbAnimSet.h"
#include "Data/VBTraversalConfig.h"
#include "UObject/UnrealType.h"
#include "Traversal/VBLadder.h"
#include "Traversal/VBLadderMode.h"
#include "Traversal/VBLadderModeHost.h"
#include "Traversal/VBTraversalComponent.h"
#include "Traversal/VBTraversalDiagnostics.h"
#include "Traversal/VBTraversalRules.h"
#include "Traversal/VBTraversalSession.h"
#include "Unit/VBIsolatedGameWorld.h"

// 사다리 보행 막기와 Shift 가속.
// 실제로 막히는지, 탄 사람만 지나가는지, 몽타주가 빨라지는지는 엔진 이동과 애님이라 여기서 못 보고 PIE 로 본다.

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderPlayRateTest, "VB.Unit.Traversal.LadderPlayRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderPlayRateTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("not sprinting keeps the authored speed"),
		VBTraversalRules::PlayRate::ResolveTraversalPlayRate(false, 1.0f, 2.0f, 2.5f), 1.0f);
	TestEqual(TEXT("sprinting up uses the up rate"), VBTraversalRules::PlayRate::ResolveTraversalPlayRate(true, 1.0f, 2.0f, 2.5f), 2.0f);
	TestEqual(TEXT("sprinting down uses the down rate"),
		VBTraversalRules::PlayRate::ResolveTraversalPlayRate(true, -1.0f, 2.0f, 2.5f), 2.5f);
	TestEqual(TEXT("idle (input under the climb threshold) is never accelerated"),
		VBTraversalRules::PlayRate::ResolveTraversalPlayRate(true, 0.1f, 2.0f, 2.5f), 1.0f);
	TestEqual(TEXT("rate is capped like the data field"),
		VBTraversalRules::PlayRate::ResolveTraversalPlayRate(true, 1.0f, 8.0f, 2.0f), 3.0f);
	TestEqual(TEXT("a slow-down value does not slow the climb"),
		VBTraversalRules::PlayRate::ResolveTraversalPlayRate(true, 1.0f, 0.5f, 2.0f), 1.0f);
	TestEqual(TEXT("broken data disables acceleration"),
		VBTraversalRules::PlayRate::ResolveTraversalPlayRate(true, -1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN()), 1.0f);
	// 등반, 옆걸음은 방향이 하나라 입력 크기(양수)를 넘긴다 - 같은 배율이 위아래, 좌우 모두에 걸린다.
	TestEqual(TEXT("climb/shimmy magnitude uses the single rate"),
		VBTraversalRules::PlayRate::ResolveTraversalPlayRate(true, 0.9f, 2.0f, 2.0f), 2.0f);
	return true;
}

// 질주 자동 진입의 의도 판정만 본다 - 주기, 막는 몸 상태, 놓은 뒤 잠금, 실제 판정 호출은 TryAutoTraversal 안의 엔진 상태라 PIE 로 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAutoTraversalIntentTest, "VB.Unit.Traversal.AutoTraversalIntent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAutoTraversalIntentTest::RunTest(const FString& Parameters)
{
	constexpr float MinDot = 0.7f;
	TestTrue(TEXT("sprinting straight ahead is intent"), VBTraversalRules::Auto::HasAutoTraversalIntent(true, 1.0f, 600.0f, MinDot, 0.0f));
	TestFalse(TEXT("running without sprint is not intent"),
		VBTraversalRules::Auto::HasAutoTraversalIntent(false, 1.0f, 600.0f, MinDot, 0.0f));
	TestFalse(TEXT("sprint key held without forward input is not intent"),
		VBTraversalRules::Auto::HasAutoTraversalIntent(true, 0.0f, 0.0f, MinDot, 0.0f));
	TestFalse(TEXT("brushing past sideways is not intent"),
		VBTraversalRules::Auto::HasAutoTraversalIntent(true, 0.3f, 600.0f, MinDot, 0.0f));
	TestFalse(TEXT("backing away is not intent"), VBTraversalRules::Auto::HasAutoTraversalIntent(true, -1.0f, 600.0f, MinDot, 0.0f));
	TestTrue(TEXT("pushing into a blocker at zero speed is intent when no speed floor is set"),
		VBTraversalRules::Auto::HasAutoTraversalIntent(true, 1.0f, 0.0f, MinDot, 0.0f));
	TestFalse(TEXT("a speed floor rejects a stopped push"),
		VBTraversalRules::Auto::HasAutoTraversalIntent(true, 1.0f, 100.0f, MinDot, 250.0f));
	TestFalse(TEXT("broken input never counts"),
		VBTraversalRules::Auto::HasAutoTraversalIntent(true, std::numeric_limits<float>::quiet_NaN(), 600.0f, MinDot, 0.0f));

	// 놓은 뒤 잠금 - 사다리, 매달림에서 C 로 놓은 직후는 입력이 CMC 로 안 가 가속이 0 이다
	// 그 순간 붙을 의도는 거짓, 아직 쥐고 있음은 참이어야 잠금이 안 풀린다 - 두 판정이 일부러 갈린다
	const FVector2D HeldForward(0.0f, 1.0f);
	TestFalse(TEXT("right after a drop the zero CMC acceleration is not admission intent"),
		VBTraversalRules::Auto::HasAutoTraversalIntent(true, 0.0f, 0.0f, MinDot, 0.0f));
	TestTrue(TEXT("but the held keys keep the drop latch closed"), VBTraversalRules::Auto::HoldsAutoTraversalInput(true, HeldForward));
	TestFalse(TEXT("releasing the move key opens the latch"), VBTraversalRules::Auto::HoldsAutoTraversalInput(true, FVector2D::ZeroVector));
	TestFalse(TEXT("releasing sprint opens the latch"), VBTraversalRules::Auto::HoldsAutoTraversalInput(false, HeldForward));
	TestFalse(TEXT("a resting stick under the move threshold counts as released"),
		VBTraversalRules::Auto::HoldsAutoTraversalInput(true, FVector2D(0.1f, 0.1f)));
	return true;
}

// 자유 등반 꼭대기 올라서기의 설 바닥 판정. 구간 [112, 262], 걷기 바닥 0.71, 턱 45.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBClimbTopOutFloorTest, "VB.Unit.Traversal.ClimbTopOutFloor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBClimbTopOutFloorTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("flat top inside the band is usable"),
		VBTraversalRules::Climb::IsClimbTopOutFloorUsable(1.0f, 0.71f, 250.0f, 250.0f, FFloatInterval(112.0f, 262.0f), 45.0f));
	TestFalse(TEXT("steep top is refused"),
		VBTraversalRules::Climb::IsClimbTopOutFloorUsable(0.5f, 0.71f, 250.0f, 250.0f, FFloatInterval(112.0f, 262.0f), 45.0f));
	TestFalse(TEXT("floor above the band is refused"),
		VBTraversalRules::Climb::IsClimbTopOutFloorUsable(1.0f, 0.71f, 270.0f, 270.0f, FFloatInterval(112.0f, 262.0f), 45.0f));
	TestFalse(TEXT("floor below the band (drop behind a thin wall) is refused"),
		VBTraversalRules::Climb::IsClimbTopOutFloorUsable(1.0f, 0.71f, 100.0f, 100.0f, FFloatInterval(112.0f, 262.0f), 45.0f));
	TestFalse(TEXT("capsule resting only on the near corner is refused"),
		VBTraversalRules::Climb::IsClimbTopOutFloorUsable(1.0f, 0.71f, 150.0f, 250.0f, FFloatInterval(112.0f, 262.0f), 45.0f));
	TestTrue(TEXT("a curb within step height is usable"),
		VBTraversalRules::Climb::IsClimbTopOutFloorUsable(1.0f, 0.71f, 200.0f, 230.0f, FFloatInterval(112.0f, 262.0f), 45.0f));
	TestFalse(TEXT("broken height is refused"), VBTraversalRules::Climb::IsClimbTopOutFloorUsable(1.0f, 0.71f,
		std::numeric_limits<float>::quiet_NaN(), 250.0f, FFloatInterval(112.0f, 262.0f), 45.0f));
	return true;
}

// 사다리에 붙을 자리(클라 찾기, 서버 재검사 공용). 사다리 축 원점, 바깥 법선 +X.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderStartPlausibleTest, "VB.Unit.Traversal.LadderStartPlausible",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderStartPlausibleTest::RunTest(const FString& Parameters)
{
	const FVector Near(0.0f, 0.0f, 100.0f);
	const FVector Normal(1.0f, 0.0f, 0.0f);
	TestTrue(TEXT("in front within reach"), VBTraversalRules::Ladder::IsLadderStartPlausible(FVector(80, 0, 100), Near, Normal, 220.0f));
	TestFalse(TEXT("behind the ladder is refused"),
		VBTraversalRules::Ladder::IsLadderStartPlausible(FVector(-80, 0, 100), Near, Normal, 220.0f));
	TestFalse(TEXT("exactly beside the rails (on the face plane) is refused"),
		VBTraversalRules::Ladder::IsLadderStartPlausible(FVector(0, 80, 100), Near, Normal, 220.0f));
	TestFalse(TEXT("beyond reach is refused"),
		VBTraversalRules::Ladder::IsLadderStartPlausible(FVector(400, 0, 100), Near, Normal, 320.0f));
	TestTrue(TEXT("server slack admits a lagged requester"),
		VBTraversalRules::Ladder::IsLadderStartPlausible(FVector(300, 0, 100), Near, Normal, 320.0f));
	TestFalse(TEXT("broken location is refused"),
		VBTraversalRules::Ladder::IsLadderStartPlausible(FVector(std::numeric_limits<float>::quiet_NaN(), 0, 0), Near, Normal, 220.0f));
	return true;
}

// 막이의 윗면이 꼭대기 착지면과 같은 높이라 끝에 선 capsule 이 바닥으로 잡을 수 있다
// 막이는 벽이라 걸을 수 없고 step-up 도 없으므로 CMC 가 capsule 중심 아래 line trace 로 실제 착지면을 찾는다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderBlockerIsNotFloorTest, "VB.Unit.Traversal.LadderBlockerIsNotFloor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderBlockerIsNotFloorTest::RunTest(const FString& Parameters)
{
	const UBoxComponent* Blocker = GetDefault<AVBLadder>()->GetBlocker();
	if (!TestNotNull(TEXT("ladder blocker"), Blocker))
	{
		return false;
	}

	TestTrue(TEXT("blocker is never walkable floor"),
		Blocker->GetWalkableSlopeOverride().GetWalkableSlopeBehavior() == WalkableSlope_Unwalkable);
	TestTrue(TEXT("characters cannot step up onto the blocker"), Blocker->CanCharacterStepUpOn == ECB_No);
	TestTrue(TEXT("it still blocks pawns"), Blocker->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderBlockerBoundsTest, "VB.Unit.Traversal.LadderBlockerBounds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderBlockerBoundsTest::RunTest(const FString& Parameters)
{
	// 기본 사다리(높이 400, 폭 66, 가로대 깊이 0, 굵기 4): 세로대 면 x=0 의 앞뒤 반지름 2, 세로대 바깥까지 폭 35.
	const FBox Flat = AVBLadder::ComputeBlockerBounds(400.0f, 66.0f, 0.0f, 4.0f);
	TestEqual(TEXT("front face is the rail surface"), Flat.Max.X, 2.0);
	TestEqual(TEXT("back face is the rail surface when rungs are flush"), Flat.Min.X, -2.0);
	TestEqual(TEXT("width reaches the outside of the rails"), Flat.Max.Y, 35.0);
	TestEqual(TEXT("bottom is the ladder base"), Flat.Min.Z, 0.0);
	TestEqual(TEXT("top is the ladder height"), Flat.Max.Z, 400.0);
	// 가로대를 뒤로 뺀 사다리: 뒤 면이 가로대 바깥까지 간다(타는 사람 쪽 앞 면은 그대로).
	const FBox Deep = AVBLadder::ComputeBlockerBounds(300.0f, 60.0f, 26.3f, 4.0f);
	TestTrue(TEXT("back face follows set-back rungs"), FMath::IsNearlyEqual(Deep.Min.X, -28.3, 1e-4));
	TestEqual(TEXT("front face stays at the rails"), Deep.Max.X, 2.0);
	// 깨진 치수는 음수 범위를 만들지 않는다.
	const FBox Broken = AVBLadder::ComputeBlockerBounds(-10.0f, -5.0f, -3.0f, 0.0f);
	TestTrue(TEXT("broken dimensions never invert the box"), Broken.Max.X >= Broken.Min.X && Broken.Max.Y >= Broken.Min.Y
		&& Broken.Max.Z >= Broken.Min.Z);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderTopOutTriggerTest, "VB.Unit.Traversal.LadderTopOutTrigger",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderTopOutTriggerTest::RunTest(const FString& Parameters)
{
	// 꼭대기 한계 211.3, 위 입력 문턱 0.5(등반 꼭대기와 같은 값).
	TestTrue(TEXT("server, aligned, full up input, at the top"),
		VBTraversalRules::Ladder::ShouldTryLadderTopOut(true, true, 1.0f, 211.3f, 211.3f, 0.5f));
	TestTrue(TEXT("server, aligned, threshold input, past the top"),
		VBTraversalRules::Ladder::ShouldTryLadderTopOut(true, true, 0.5f, 212.0f, 211.3f, 0.5f));
	// 서버만 정한다 - 소유 클라는 같은 틱을 돌려도 멀티캐스트를 기다린다.
	TestFalse(TEXT("client never decides"), VBTraversalRules::Ladder::ShouldTryLadderTopOut(false, true, 1.0f, 211.3f, 211.3f, 0.5f));
	// 위쪽에서 붙어 위를 쥔 채로 있어도 붙는 보간이 끝나기 전에는 안 한다(Codex R2-03).
	TestFalse(TEXT("entry blend not finished"), VBTraversalRules::Ladder::ShouldTryLadderTopOut(true, false, 1.0f, 211.3f, 211.3f, 0.5f));
	TestFalse(TEXT("below the top"), VBTraversalRules::Ladder::ShouldTryLadderTopOut(true, true, 1.0f, 171.0f, 211.3f, 0.5f));
	TestFalse(TEXT("weak up input"), VBTraversalRules::Ladder::ShouldTryLadderTopOut(true, true, 0.3f, 211.3f, 211.3f, 0.5f));
	TestFalse(TEXT("down input at the top"), VBTraversalRules::Ladder::ShouldTryLadderTopOut(true, true, -1.0f, 211.3f, 211.3f, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderTopOutBandTest, "VB.Unit.Traversal.LadderTopOutBand",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderTopOutBandTest::RunTest(const FString& Parameters)
{
	// 꼭대기 400(P06 발판 높이), 스킨 2, 허용 40.
	FFloatInterval Band = VBTraversalRules::Ladder::ComputeLadderTopOutBand(400.0f, 2.0f, 40.0f);
	const float& MinZ = Band.Min;
	const float& MaxZ = Band.Max;
	TestTrue(TEXT("floor level with the top is inside"), 400.0f >= MinZ && 400.0f <= MaxZ);
	TestTrue(TEXT("floor one rung above is inside"), 440.0f >= MinZ && 440.0f <= MaxZ);
	TestFalse(TEXT("floor above the tolerance is outside"), 441.0f >= MinZ && 441.0f <= MaxZ);
	// 꼭대기보다 낮은 바닥은 서는 캡슐이 사다리 막는 상자에 걸친다(Codex R2-01: 360 이면 상자와 33 < 반지름 34) - 받지 않는다.
	TestFalse(TEXT("floor below the top is outside"), 360.0f >= MinZ && 360.0f <= MaxZ);
	TestFalse(TEXT("floor just past the skin is outside"), 397.0f >= MinZ && 397.0f <= MaxZ);
	// 깨진 값은 아래로 넓히지 않는다.
	Band = VBTraversalRules::Ladder::ComputeLadderTopOutBand(400.0f, -5.0f, -10.0f);
	TestTrue(TEXT("negative skin/tolerance collapse to the top"), MinZ == 400.0f && MaxZ == 400.0f);
	return true;
}

// 실제 컴포넌트의 사다리 모드와 호스트에 접근한다. 시험은 그 뒤 모드의 공개 함수만 호출한다.
struct FVBTraversalTestAccess
{
	static FTickFunction& Intent(UVBTraversalComponent& Traversal) { return Traversal.LadderIntentTick; }
	static FVBLadderMode& Mode(UVBTraversalComponent& Traversal) { return Traversal.LadderMode; }
	static IVBLadderModeHost& Host(UVBTraversalComponent& Traversal) { return Traversal.LadderHost; }
	static void StartLadder(UVBTraversalComponent& Traversal, AVBLadder* Ladder)
	{
		Traversal.LadderMode.Start(Traversal.LadderHost, Traversal.Session, Ladder, 0.0f);
	}

	static void StartClimb(UVBTraversalComponent& Traversal)
	{
		Traversal.ClimbMode.Start(Traversal, Traversal.Session, FVector::ZeroVector, FVector::ForwardVector, nullptr);
	}

	// 세션 전이. 매달림, 등반 시작 경로는 렛지, 벽 기하가 있어야 해서 전이를 직접 부른다.
	static void SetState(UVBTraversalComponent& Traversal, EVBTraversalPhase Phase, EVBTraversalSurface Surface) { Traversal.SetTraversalState(Phase, Surface); }
	static void BeginMove(UVBTraversalComponent& Traversal) { Traversal.BeginMove(); }
	static void Attach(UVBTraversalComponent& Traversal, EVBTraversalSurface Surface) { Traversal.Attach(Surface); }
	static void Detach(UVBTraversalComponent& Traversal) { Traversal.Detach(); }
	static void EndSession(UVBTraversalComponent& Traversal) { Traversal.EndSession(); }
	static void ClearClimbState(UVBTraversalComponent& Traversal) { Traversal.ClimbMode.ClearOnExit(Traversal); }
	static void Recover(UVBTraversalComponent& Traversal) { Traversal.RecoverFromTraversal(); }
	static bool HasFootprint(const UVBTraversalComponent& Traversal, const FVector& Point, const FVector& Normal, float Side)
	{
		return Traversal.ClimbMode.HasFootprintAt(Traversal, Point, Normal, Side);
	}

	static bool TryStartClimb(UVBTraversalComponent& Traversal) { return Traversal.ClimbMode.TryStart(Traversal); }
	static void UpdateClimb(UVBTraversalComponent& Traversal, float DeltaTime) { Traversal.ClimbMode.Update(Traversal, DeltaTime); }
	static const FVBHangRuntime& Hang(const UVBTraversalComponent& Traversal) { return Traversal.HangMode.GetState(); }
	static FVector& ClimbSurfacePoint(UVBTraversalComponent& Traversal) { return Climb(Traversal).SurfacePoint; }

	static void PrimeOwnerWithoutAnimSet(UVBTraversalComponent& Traversal, AVBCharacter* Owner)
	{
		Traversal.OwnerCharacter = Owner;
		Traversal.ClimbAnimSet = nullptr;
	}

	static void SetAnimSet(UVBTraversalComponent& Traversal, UVBClimbAnimSet* Set) { Traversal.ClimbAnimSet = Set; }

	// 등반, 장기 진단 그룹: Seed* 는 모든 필드에 한 값을 쓰고 *Is 는 그 값인지 확인한다.
	static void SeedClimb(UVBTraversalComponent& Traversal, double Value, bool bPrimed)
	{
		FVBClimbDiagnostics& Diag = Climb(Traversal).Diag;
		Diag.StatusLog.NextTime =Diag.ClampLog.NextTime = Diag.JumpLog.NextTime = Diag.TopOutLog.NextTime = Value;
		Diag.MaxBodyRate = Diag.MaxLimbRate = static_cast<float>(Value);
		for (FVector& Limb : Diag.PrevLimbRel)
		{
			Limb = FVector(Value);
		}

		Diag.bLimbRatePrimed = bPrimed;
	}

	static bool ClimbIs(const UVBTraversalComponent& Traversal, double Value, bool bPrimed)
	{
		const FVBClimbDiagnostics& Diag = Traversal.ClimbMode.GetState().Diag;
		bool bLimbs = true;
		for (const FVector& Limb : Diag.PrevLimbRel)
		{
			bLimbs = bLimbs && Limb == FVector(Value);
		}

		return Diag.StatusLog.NextTime == Value && Diag.ClampLog.NextTime == Value && Diag.JumpLog.NextTime == Value
			&& Diag.TopOutLog.NextTime == Value && Diag.MaxBodyRate == static_cast<float>(Value)
			&& Diag.MaxLimbRate == static_cast<float>(Value) && bLimbs && Diag.bLimbRatePrimed == bPrimed;
	}

	static void SeedLongLived(UVBTraversalComponent& Traversal, double Value, bool bWarned)
	{
		const_cast<FVBClimbLongLivedDiagnostics&>(Traversal.ClimbMode.GetLongLivedDiag()).ProbeLog.NextTime = Value;
		const_cast<FVBHangLongLivedDiagnostics&>(Traversal.HangMode.GetLongLivedDiag()).bShimmyFallbackWarned = bWarned;
	}

	static bool LongLivedIs(const UVBTraversalComponent& Traversal, double Value, bool bWarned)
	{
		return Traversal.ClimbMode.GetLongLivedDiag().ProbeLog.NextTime == Value
			&& Traversal.HangMode.GetLongLivedDiag().bShimmyFallbackWarned == bWarned;
	}

	// 모드는 상태를 const 로만 내준다. 제품 헤더에 쓰기 통로를 늘리지 않으려고 시험만 const 를 푼다.
	static FVBClimbRuntime& Climb(UVBTraversalComponent& Traversal) { return const_cast<FVBClimbRuntime&>(Traversal.ClimbMode.GetState()); }
	static double& RegrabBlockUntil(UVBTraversalComponent& Traversal) { return Traversal.Reentry.HangRegrabBlockUntil; }
};

namespace VBLadderTraversalTest
{
	UWorld* NewWorld()
	{
		const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
		return UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	}

	UVBClimbAnimSet* DefaultClips()
	{
		return LoadObject<UVBClimbAnimSet>(nullptr, TEXT("/Game/VB/Data/DA_ClimbAnimSet_Default.DA_ClimbAnimSet_Default"));
	}

	// 모드의 요청을 기록하는 호스트. 탑승자와 클립은 실제 것이라 시작 후 탑승이 유지되고, 틱 대상은 시험이 정한다.
	class FRecordingHost final : public IVBLadderModeHost
	{
	public:
		AVBCharacter* Rider = nullptr;
		const UVBClimbAnimSet* Clips = nullptr;
		FVBLadderTickTargets Targets;
		bool bRiding = false;
		int32 MoveInputSends = 0;
		float LastSentInput = 0.0f;
		int32 DropRequests = 0;
		int32 FreshIntentRequests = 0;

		virtual AVBCharacter* GetCharacter() const override { return Rider; }
		virtual const UVBClimbAnimSet* GetClips() const override { return Clips; }
		virtual const UVBTraversalConfig* GetConfig() const override { return GetDefault<UVBTraversalConfig>(); }
		virtual bool IsRidingLadder() const override { return bRiding; }
		virtual bool IsInSurfaceMode() const override { return bRiding; }
		virtual bool IsInAnyTraversal() const override { return bRiding; }
		virtual bool IsBodyBusyForTraversal() const override { return false; }
		virtual void SetAttached(bool bInRiding) override { bRiding = bInRiding; }
		virtual void EndSessionFalling() override {}
		virtual void RequireFreshAutoIntent() override { ++FreshIntentRequests; }
		virtual void SendLadderStartRequest(AVBLadder*, float) override {}
		virtual void SendLadderMoveInput(float InputY) override { ++MoveInputSends; LastSentInput = InputY; }
		virtual void SendLadderDropRequest() override { ++DropRequests; }
		virtual void ConfirmLadderDrop() override {}
		virtual void ConfirmLadderTopOut(const FVector&, const FVector&) override {}
		virtual FVBTopOutResult FindTopOutStand(const FVector&, const FVector&, const FFloatInterval&) const override
		{
			FVBTopOutResult Refused;
			Refused.Refusal = TEXT("test host");
			return Refused;
		}

		virtual void PlayTopOutMontage(const FVector&, const FVector&, const TCHAR*) override {}
		virtual FVBLadderTickTargets GetLadderTickTargets() override { return Targets; }
	};
}

// 기록 호스트로 모드를 검사한다: 탑승 중 추가하는 틱 순서(직접 추가한 의존만)와 전송 규칙 -
// 요청 함수는 한 번 보내고, 받는 함수(ApplyMoveInput)는 적용만 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderModeContractTest, "VB.Unit.Traversal.LadderModeContract",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBLadderModeContractTest::RunTest(const FString& Parameters)
{
	using namespace VBLadderTraversalTest;
	UWorld* World = NewWorld();
	if (!TestNotNull(TEXT("mode fixture world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UVBClimbAnimSet* Clips = DefaultClips();
	AVBCharacter* Rider = World->SpawnActor<AVBCharacter>();
	AVBLadder* Ladder = World->SpawnActor<AVBLadder>();
	// 빙의 컨트롤러 대역. 여기서는 액터 틱 함수만 사용한다.
	AActor* Controller = World->SpawnActor<AActor>();
	UVBTraversalComponent* Traversal = UVBTraversalComponent::FindFor(Rider);
	if (!TestNotNull(TEXT("default clips"), Clips) || !TestNotNull(TEXT("ladder"), Ladder)
		|| !TestNotNull(TEXT("controller stand-in"), Controller)
		|| !TestNotNull(TEXT("traversal"), Traversal))
	{
		return false;
	}

	Controller->PrimaryActorTick.bCanEverTick = true;
	USkeletalMeshComponent* Mesh = Rider->GetMesh();
	UCharacterMovementComponent* Movement = Rider->GetCharacterMovement();
	FTickFunction& Intent = FVBTraversalTestAccess::Intent(*Traversal);
	FTickFunction& Placement = Traversal->PrimaryComponentTick;

	FRecordingHost Host;
	Host.Rider = Rider;
	Host.Clips = Clips;
	Host.Targets.Owner = Traversal;
	Host.Targets.IntentTick = &Intent;
	Host.Targets.PlacementTick = &Placement;
	Host.Targets.Controller = Controller;
	Host.Targets.Movement = Movement;
	Host.Targets.Mesh = Mesh;
	FVBLadderMode Mode;
	FVBTraversalSession Session;

	const auto Has = [](const FTickFunction& Dependent, UObject* Object, FTickFunction& Prerequisite)
	{
		return Dependent.GetPrerequisites().Contains(FTickPrerequisite(Object, Prerequisite));
	};
	const auto AnyLadderEdge = [&]()
	{
		return Has(Intent, Controller, Controller->PrimaryActorTick) || Has(Movement->PrimaryComponentTick, Traversal, Intent)
			|| Has(Placement, Movement, Movement->PrimaryComponentTick) || Has(Mesh->PrimaryComponentTick, Traversal, Placement);
	};
	// 새 시험 월드는 액터 초기화 전이라 게임 월드에 있는 엔진 의존(CMC 뒤 메시)을 직접 추가한다.
	Mesh->PrimaryComponentTick.AddPrerequisite(Movement, Movement->PrimaryComponentTick);
	TestFalse(TEXT("no ladder edges before the ride"), AnyLadderEdge());

	Mode.Start(Host, Session, Ladder, 0.0f);
	TestTrue(TEXT("start raises the riding flag through the host"), Host.bRiding);
	TestTrue(TEXT("start recovers by falling"), Session.GetRecoverMode() == MOVE_Falling);
	TestTrue(TEXT("start enters the surface through the session"), Movement->MovementMode == MOVE_Flying);
	TestTrue(TEXT("stage A after the controller (input)"), Has(Intent, Controller, Controller->PrimaryActorTick));
	TestTrue(TEXT("CMC after stage A"), Has(Movement->PrimaryComponentTick, Traversal, Intent));
	TestTrue(TEXT("stage B after the CMC"), Has(Placement, Movement, Movement->PrimaryComponentTick));
	TestTrue(TEXT("mesh after stage B"), Has(Mesh->PrimaryComponentTick, Traversal, Placement));
	TestTrue(TEXT("stage A is enabled while riding"), Intent.IsTickFunctionEnabled());
	const int32 MeshEdges = Mesh->PrimaryComponentTick.GetPrerequisites().Num();
	const int32 MovementEdges = Movement->PrimaryComponentTick.GetPrerequisites().Num();
	Mode.Start(Host, Session, Ladder, 0.0f);
	TestEqual(TEXT("a second start does not duplicate mesh edges"), Mesh->PrimaryComponentTick.GetPrerequisites().Num(), MeshEdges);
	TestEqual(TEXT("a second start does not duplicate CMC edges"), Movement->PrimaryComponentTick.GetPrerequisites().Num(), MovementEdges);

	Mode.SetMoveInput(Host, 2.0f);
	TestEqual(TEXT("the requesting call sends once"), Host.MoveInputSends, 1);
	TestEqual(TEXT("it sends the clamped value"), Host.LastSentInput, 1.0f);
	Mode.ApplyMoveInput(-0.3f);
	TestEqual(TEXT("the receiving call does not send again"), Host.MoveInputSends, 1);
	TestEqual(TEXT("the receiving call applies"), Mode.GetState().MoveInput, -0.3f);
	Mode.RequestDrop(Host);
	TestEqual(TEXT("a drop asks for a fresh auto intent"), Host.FreshIntentRequests, 1);
	TestEqual(TEXT("a drop sends one request"), Host.DropRequests, 1);

	Mode.ClearRiding(Host);
	TestFalse(TEXT("clear lowers the riding flag"), Host.bRiding);
	TestFalse(TEXT("clear removes every ladder edge"), AnyLadderEdge());
	TestTrue(TEXT("clear keeps the engine edge"), Has(Mesh->PrimaryComponentTick, Movement, Movement->PrimaryComponentTick));
	TestFalse(TEXT("stage A is disabled off the ladder"), Intent.IsTickFunctionEnabled());
	Mode.SetMoveInput(Host, 1.0f);
	TestEqual(TEXT("no send when not riding"), Host.MoveInputSends, 1);

	// 다른 곳에서 이미 B 를 CMC 뒤로 걸었으면 모드 소유가 아니므로 남긴다.
	Traversal->AddTickPrerequisiteComponent(Movement);
	Mode.Start(Host, Session, Ladder, 0.0f);
	Mode.ClearRiding(Host);
	TestTrue(TEXT("a pre-existing edge survives"), Has(Placement, Movement, Movement->PrimaryComponentTick));
	TestFalse(TEXT("owned edges still go"), Has(Mesh->PrimaryComponentTick, Traversal, Placement));
	return true;
}

// 실제 캡슐의 막는 상자 무시: 모드는 자신이 추가한 무시만, 최종 복구에서만 해제한다 -
// 올라서기는 탑승을 먼저 정리하고 몽타주가 끝날 때까지 상자를 계속 통과해야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderReleaseOwnIgnoreOnlyTest, "VB.Unit.Traversal.LadderReleaseOwnIgnoreOnly",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBLadderReleaseOwnIgnoreOnlyTest::RunTest(const FString& Parameters)
{
	using namespace VBLadderTraversalTest;
	using Access = FVBTraversalTestAccess;
	UWorld* World = NewWorld();
	if (!TestNotNull(TEXT("ignore fixture world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UVBClimbAnimSet* Clips = DefaultClips();
	AVBCharacter* Rider = World->SpawnActor<AVBCharacter>();
	AVBLadder* Ladder = World->SpawnActor<AVBLadder>();
	UVBTraversalComponent* Traversal = UVBTraversalComponent::FindFor(Rider);
	UBoxComponent* Blocker = Ladder ? Ladder->GetBlocker() : nullptr;
	UCapsuleComponent* Capsule = Rider ? Rider->GetCapsuleComponent() : nullptr;
	if (!TestNotNull(TEXT("default clips"), Clips) || !TestNotNull(TEXT("traversal"), Traversal) || !TestNotNull(TEXT("blocker"), Blocker)
		|| !TestNotNull(TEXT("capsule"), Capsule))
	{
		return false;
	}

	Access::PrimeOwnerWithoutAnimSet(*Traversal, Rider);
	Access::SetAnimSet(*Traversal, Clips);
	const auto Ignores = [&]() { return Capsule->GetMoveIgnoreComponents().Contains(Blocker); };

	Access::StartLadder(*Traversal, Ladder);
	TestTrue(TEXT("riding"), Traversal->IsOnLadder());
	TestTrue(TEXT("the rider's capsule passes the blocker"), Ignores());
	// 올라서기와 같은 순서: 탑승을 정리하고 세션은 몽타주 끝까지 유지한다.
	Access::Mode(*Traversal).ClearRiding(Access::Host(*Traversal));
	TestTrue(TEXT("after the ride is cleared the ignore stays"), Ignores());
	Access::Host(*Traversal).EndSessionFalling();
	TestFalse(TEXT("the final recovery returns the ignore"), Ignores());

	// 탑승 전부터 있던 무시는 모드가 해제하지 않는다.
	Capsule->IgnoreComponentWhenMoving(Blocker, true);
	Access::StartLadder(*Traversal, Ladder);
	Traversal->RequestLadderDrop();
	TestFalse(TEXT("the drop ended the ride"), Traversal->IsOnLadder());
	TestTrue(TEXT("a pre-existing ignore survives the drop"), Ignores());
	return true;
}

// 모드 시작은 실제 시작 경로로 자기 진단 그룹만 비우고 다른 그룹은 그대로 둔다.
// 애님 세트가 없으면 시작 직후 끝나므로 확인 전에 그룹을 다시 쓰는 곳이 없다.
// 사다리 진단은 모드의 private 상태라 등반 코드가 접근할 수 없다. 통째 비우기는 ModeRuntimeResetOnEntry 가 MoveInput 으로 확인한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTraversalDiagnosticsResetOnEntryTest, "VB.Unit.Traversal.DiagnosticsResetOnEntry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBTraversalDiagnosticsResetOnEntryTest::RunTest(const FString& Parameters)
{
	using Access = FVBTraversalTestAccess;
	UWorld* World = VBLadderTraversalTest::NewWorld();
	if (!TestNotNull(TEXT("diagnostics fixture world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AVBCharacter* Character = World->SpawnActor<AVBCharacter>();
	AVBLadder* Ladder = World->SpawnActor<AVBLadder>();
	UVBTraversalComponent* Traversal = UVBTraversalComponent::FindFor(Character);
	if (!TestNotNull(TEXT("character"), Character) || !TestNotNull(TEXT("ladder"), Ladder) || !TestNotNull(TEXT("traversal"), Traversal))
	{
		return false;
	}

	Access::PrimeOwnerWithoutAnimSet(*Traversal, Character);

	constexpr double Stale = 1.0e6; // a log gate from a long-gone session
	Access::SeedClimb(*Traversal, Stale, true);
	Access::SeedLongLived(*Traversal, Stale, true);
	Access::StartLadder(*Traversal, Ladder);
	TestTrue(TEXT("ladder start leaves the climb group"), Access::ClimbIs(*Traversal, Stale, true));
	TestTrue(TEXT("ladder start leaves the long-lived diagnostics"), Access::LongLivedIs(*Traversal, Stale, true));

	AddExpectedError(TEXT("StartClimb: ClimbAnimSet"), EAutomationExpectedErrorFlags::Contains, 1);
	Access::StartClimb(*Traversal);
	TestFalse(TEXT("a climb without its anim set does not start"), Traversal->IsClimbing());
	TestTrue(TEXT("climb start clears the whole climb group"), Access::ClimbIs(*Traversal, 0.0, false));
	TestTrue(TEXT("climb start leaves the long-lived diagnostics"), Access::LongLivedIs(*Traversal, Stale, true));
	return true;
}

// 모드 시작은 그 모드의 실행 상태 전체를 비운다. 사다리는 공개 함수로 이전 탑승을 남긴다: 종료가 비우지 않는 입력이
// 놓기 뒤에도 남고, 다음 시작이 그것을 비워야 한다. 등반은 종료가 비우지 않는 필드에만 값을 넣는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTraversalModeRuntimeResetTest, "VB.Unit.Traversal.ModeRuntimeResetOnEntry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBTraversalModeRuntimeResetTest::RunTest(const FString& Parameters)
{
	using Access = FVBTraversalTestAccess;
	UWorld* World = VBLadderTraversalTest::NewWorld();
	if (!TestNotNull(TEXT("mode runtime fixture world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UVBClimbAnimSet* Set = VBLadderTraversalTest::DefaultClips();
	AVBCharacter* Rider = World->SpawnActor<AVBCharacter>();
	AVBCharacter* Climber = World->SpawnActor<AVBCharacter>();
	AVBLadder* Ladder = World->SpawnActor<AVBLadder>();
	UVBTraversalComponent* RiderTraversal = UVBTraversalComponent::FindFor(Rider);
	UVBTraversalComponent* ClimberTraversal = UVBTraversalComponent::FindFor(Climber);
	if (!TestNotNull(TEXT("default climb anim set"), Set) || !TestNotNull(TEXT("ladder"), Ladder)
		|| !TestNotNull(TEXT("rider traversal"), RiderTraversal) || !TestNotNull(TEXT("climber traversal"), ClimberTraversal))
	{
		return false;
	}

	constexpr double RegrabStale = 1.0e6;

	Access::PrimeOwnerWithoutAnimSet(*RiderTraversal, Rider);
	Access::SetAnimSet(*RiderTraversal, Set);
	const FVBLadderRuntime& Ride = Access::Mode(*RiderTraversal).GetState();
	Access::StartLadder(*RiderTraversal, Ladder);
	RiderTraversal->SetLadderMoveInput(0.6f);
	RiderTraversal->RequestLadderDrop();
	TestFalse(TEXT("the first ride ended"), RiderTraversal->IsOnLadder());
	TestEqual(TEXT("the exit keeps the last input"), Ride.MoveInput, 0.6f);
	Access::RegrabBlockUntil(*RiderTraversal) = RegrabStale;
	Access::StartLadder(*RiderTraversal, Ladder);
	TestTrue(TEXT("the ladder is still mounted after entry"), RiderTraversal->IsOnLadder());
	TestTrue(TEXT("entry wrote this ride's ladder"), Ride.ActiveLadder.Get() == Ladder);
	TestEqual(TEXT("entry dropped the last ride's input"), Ride.MoveInput, 0.0f);
	TestEqual(TEXT("entry leaves the re-entry memory"), Access::RegrabBlockUntil(*RiderTraversal), RegrabStale);

	Access::PrimeOwnerWithoutAnimSet(*ClimberTraversal, Climber);
	Access::SetAnimSet(*ClimberTraversal, Set);
	FVBClimbRuntime& Climb = Access::Climb(*ClimberTraversal);
	Climb.CorrHandL = 5.0f;
	Climb.WeightFootR = 0.5f;
	Climb.PendingSince = 99.0;
	Access::StartClimb(*ClimberTraversal);
	TestEqual(TEXT("entry dropped the last climb's hand correction"), Climb.CorrHandL, 0.0f);
	TestEqual(TEXT("entry dropped the last climb's foot weight"), Climb.WeightFootR, 0.0f);
	TestEqual(TEXT("entry dropped the last climb's pending direction time"), Climb.PendingSince, 0.0);
	return true;
}

// 모드가 가진 몽타주는 컴포넌트부터 몽타주까지 모든 단계가 리플렉션 프로퍼티여야 GC 에 보인다.
// 중간에 UPROPERTY 가 빠지면 재생 중인 몽타주가 해제될 수 있다. 구조만 확인한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTraversalModeRuntimeGCTest, "VB.Unit.Traversal.ModeRuntimeKeepsMontagesForGC",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBTraversalModeRuntimeGCTest::RunTest(const FString& Parameters)
{
	struct FChain
	{
		TArray<const TCHAR*> Links; // 컴포넌트에서 실행 상태 구조체까지의 멤버 경로
		const UScriptStruct* Runtime;
		TArray<const TCHAR*> Montages;
	};
	const FChain Chains[] = {
		{{TEXT("HangMode"), TEXT("State")}, FVBHangRuntime::StaticStruct(), {TEXT("ShimmyMontage")}},
		{{TEXT("LadderMode"), TEXT("State")}, FVBLadderRuntime::StaticStruct(), {TEXT("ActiveMontage"), TEXT("ResumeMontage")}},
		{{TEXT("ClimbMode"), TEXT("State")}, FVBClimbRuntime::StaticStruct(), {TEXT("ActiveMontage"), TEXT("PendingMontage")}},
	};
	for (const FChain& Chain : Chains)
	{
		const UStruct* Outer = UVBTraversalComponent::StaticClass();
		FString Path;
		for (const TCHAR* Link : Chain.Links)
		{
			Path += Path.IsEmpty() ? FString(Link) : FString(TEXT(".")) + Link;
			const FStructProperty* Member = Outer ? CastField<FStructProperty>(Outer->FindPropertyByName(Link)) : nullptr;
			TestTrue(FString::Printf(TEXT("%s is a reflected struct property"), *Path), Member != nullptr);
			Outer = Member ? Member->Struct : nullptr;
		}

		TestTrue(FString::Printf(TEXT("%s is the runtime group"), *Path), Outer == Chain.Runtime);
		for (const TCHAR* Name : Chain.Montages)
		{
			const FObjectPropertyBase* Montage = CastField<FObjectPropertyBase>(Chain.Runtime->FindPropertyByName(Name));
			TestTrue(FString::Printf(TEXT("%s.%s is a reflected montage reference"), *Path, Name),
				Montage && Montage->PropertyClass && Montage->PropertyClass->IsChildOf(UAnimMontage::StaticClass()));
		}
	}

	return true;
}

namespace VBLadderTraversalTest
{
	// PlayerState ASC 대신 캐릭터에 ASC 를 붙인다. 사망 판정 소유자가 체력 세트를 요구한다.
	UVBAbilitySystemComponent* GiveAbilitySystem(AVBCharacter& Body)
	{
		UVBAbilitySystemComponent* ASC = NewObject<UVBAbilitySystemComponent>(&Body);
		Body.AddInstanceComponent(ASC);
		ASC->RegisterComponent();
		ASC->AddAttributeSetSubobject(NewObject<UVBHealthAttributeSet>(&Body));
		UVBPawnExtensionComponent::FindFor(&Body)->InitializeAbilitySystem(ASC, &Body);
		return ASC;
	}

	struct FSessionFixture
	{
		UWorld* World = nullptr;
		AVBCharacter* Body = nullptr;
		AVBLadder* Ladder = nullptr;
		UVBTraversalComponent* Traversal = nullptr;
		UVBAbilitySystemComponent* ASC = nullptr;

		bool Build(FAutomationTestBase& Test)
		{
			World = NewWorld();
			if (!Test.TestNotNull(TEXT("session fixture world"), World))
			{
				return false;
			}

			UVBClimbAnimSet* Clips = DefaultClips();
			Body = World->SpawnActor<AVBCharacter>();
			Ladder = World->SpawnActor<AVBLadder>();
			Traversal = UVBTraversalComponent::FindFor(Body);
			if (!Test.TestNotNull(TEXT("default clips"), Clips) || !Test.TestNotNull(TEXT("ladder"), Ladder)
				|| !Test.TestNotNull(TEXT("traversal"), Traversal))
			{
				return false;
			}

			FVBTraversalTestAccess::PrimeOwnerWithoutAnimSet(*Traversal, Body);
			FVBTraversalTestAccess::SetAnimSet(*Traversal, Clips);
			ASC = GiveAbilitySystem(*Body);
			return Test.TestNotNull(TEXT("ASC"), ASC);
		}

		~FSessionFixture() { if (World) { World->DestroyWorld(false); } }
	};
}

// 전이마다 두 축, 공개 조회, 상태 태그 다섯이 함께 맞는지 본다. 사다리는 실제 시작, 놓기 경로로 간다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTraversalSessionAxesTest, "VB.Unit.Traversal.SessionAxes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBTraversalSessionAxesTest::RunTest(const FString& Parameters)
{
	using Access = FVBTraversalTestAccess;
	VBLadderTraversalTest::FSessionFixture F;
	if (!F.Build(*this))
	{
		return false;
	}

	UVBTraversalComponent& T = *F.Traversal;

	const FGameplayTag Tags[] = { VBGameplayTags::State_Traversal_Hang, VBGameplayTags::State_Traversal_Ladder,
		VBGameplayTags::State_Traversal_Climb, VBGameplayTags::State_Traversal_Move, VBGameplayTags::State_Traversal_Exit };
	const auto Expect = [&](const TCHAR* Step, EVBTraversalPhase Phase, EVBTraversalSurface Surface, const FGameplayTag OnlyTag)
	{
		TestTrue(FString::Printf(TEXT("%s: phase"), Step), T.GetTraversalPhase() == Phase);
		TestEqual(FString::Printf(TEXT("%s: doing traversal"), Step), T.IsDoingTraversal(), Phase != EVBTraversalPhase::None);
		TestEqual(FString::Printf(TEXT("%s: hanging"), Step), T.IsHanging(), Surface == EVBTraversalSurface::Hang);
		TestEqual(FString::Printf(TEXT("%s: on ladder"), Step), T.IsOnLadder(), Surface == EVBTraversalSurface::Ladder);
		TestEqual(FString::Printf(TEXT("%s: climbing"), Step), T.IsClimbing(), Surface == EVBTraversalSurface::Climb);
		TestEqual(FString::Printf(TEXT("%s: owns capsule facing"), Step), T.OwnsCapsuleFacing(), Surface != EVBTraversalSurface::None);
		TestEqual(FString::Printf(TEXT("%s: ticks only while attached"), Step), T.IsComponentTickEnabled(), Phase == EVBTraversalPhase::Attached);
		for (const FGameplayTag& Tag : Tags)
		{
			TestEqual(FString::Printf(TEXT("%s: %s"), Step, *Tag.ToString()), F.ASC->GetTagCount(Tag), Tag == OnlyTag ? 1 : 0);
		}
	};
	using P = EVBTraversalPhase;
	using S = EVBTraversalSurface;

	Expect(TEXT("idle"), P::None, S::None, FGameplayTag());
	Access::BeginMove(T);
	Expect(TEXT("move"), P::Move, S::None, VBGameplayTags::State_Traversal_Move);
	Access::Detach(T);
	Expect(TEXT("detach during a move changes nothing"), P::Move, S::None, VBGameplayTags::State_Traversal_Move);
	Access::EndSession(T);
	Expect(TEXT("move ended"), P::None, S::None, FGameplayTag());

	Access::Attach(T, S::Hang);
	Expect(TEXT("hang"), P::Attached, S::Hang, VBGameplayTags::State_Traversal_Hang);
	Access::Detach(T);
	Expect(TEXT("hang climb-up"), P::Exiting, S::None, VBGameplayTags::State_Traversal_Exit);
	Access::EndSession(T);
	Access::Detach(T);
	Expect(TEXT("detach after the session ended does not revive it"), P::None, S::None, FGameplayTag());

	Access::Attach(T, S::Climb);
	Expect(TEXT("climb"), P::Attached, S::Climb, VBGameplayTags::State_Traversal_Climb);
	Access::ClearClimbState(T);
	Expect(TEXT("climb top-out clears the climb"), P::Exiting, S::None, VBGameplayTags::State_Traversal_Exit);
	Access::EndSession(T);

	Access::StartLadder(T, F.Ladder);
	Expect(TEXT("ladder"), P::Attached, S::Ladder, VBGameplayTags::State_Traversal_Ladder);
	T.RequestLadderDrop();
	Expect(TEXT("ladder drop"), P::None, S::None, FGameplayTag());

	// 면은 Attached 일 때만 있다. 어긋난 요청은 Error 를 내고 값을 그대로 둔다.
	AddExpectedError(TEXT("EVBTraversalSurface::Hang"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("EVBTraversalPhase::Attached"), EAutomationExpectedErrorFlags::Contains, 1);
	Access::SetState(T, P::None, S::Hang);
	Expect(TEXT("a surface without attaching is refused"), P::None, S::None, FGameplayTag());
	Access::Attach(T, S::Climb);
	Access::SetState(T, P::Attached, S::None);
	Expect(TEXT("attaching without a surface is refused"), P::Attached, S::Climb, VBGameplayTags::State_Traversal_Climb);
	Access::EndSession(T);
	return true;
}

// 세션은 빌린 것을 기록한 만큼만 돌려준다. 캐릭터가 사라졌으면 기록만 지운다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTraversalSessionRestoreTest, "VB.Unit.Traversal.SessionRestore",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBTraversalSessionRestoreTest::RunTest(const FString& Parameters)
{
	VBLadderTraversalTest::FSessionFixture F;
	if (!F.Build(*this))
	{
		return false;
	}

	AVBCharacter& Body = *F.Body;
	UCharacterMovementComponent* Movement = Body.GetCharacterMovement();
	UCapsuleComponent* Capsule = Body.GetCapsuleComponent();
	UBoxComponent* Obstacle = F.Ladder->GetBlocker();
	if (!TestNotNull(TEXT("movement"), Movement) || !TestNotNull(TEXT("capsule"), Capsule) || !TestNotNull(TEXT("obstacle"), Obstacle))
	{
		return false;
	}

	const auto Ignores = [&]() { return Capsule->GetMoveIgnoreComponents().Contains(Obstacle); };
	FVBTraversalSession Session;
	Session.EnterMove(Body, Obstacle, MOVE_Falling);
	TestTrue(TEXT("a move flies"), Movement->MovementMode == MOVE_Flying);
	TestTrue(TEXT("a move ignores the obstacle"), Ignores());
	TestTrue(TEXT("a move records the recovery mode"), Session.GetRecoverMode() == MOVE_Falling);
	TestTrue(TEXT("a move records the obstacle"), Session.GetIgnoredObstacle() == Obstacle);
	Session.Restore(&Body);
	TestTrue(TEXT("restore applies the recorded mode"), Movement->MovementMode == MOVE_Falling);
	TestFalse(TEXT("restore returns the ignore"), Ignores());
	TestNull(TEXT("restore clears the record"), Session.GetIgnoredObstacle());

	Session.SetRecoverMode(MOVE_Walking);
	Movement->Velocity = FVector(100.0, 0.0, 0.0);
	Session.EnterSurface(Body, nullptr);
	TestTrue(TEXT("a surface flies"), Movement->MovementMode == MOVE_Flying);
	TestTrue(TEXT("a surface stops the movement"), Movement->Velocity.IsNearlyZero());
	TestTrue(TEXT("a surface keeps the chosen recovery mode"), Session.GetRecoverMode() == MOVE_Walking);
	Session.Restore(&Body);
	TestTrue(TEXT("restore walks"), Movement->MovementMode == MOVE_Walking);

	Session.EnterSurface(Body, Obstacle);
	Session.Restore(nullptr);
	TestNull(TEXT("no character still clears the record"), Session.GetIgnoredObstacle());
	TestTrue(TEXT("no character changes nothing on the capsule"), Ignores());
	Capsule->IgnoreComponentWhenMoving(Obstacle, false);
	return true;
}

// 붙은 면이 이동 입력을 가져간다. 사다리는 위아래 축만 쓰고, 붙어 있지 않으면 입력은 CMC 로 간다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTraversalMoveInputRoutingTest, "VB.Unit.Traversal.MoveInputRouting",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBTraversalMoveInputRoutingTest::RunTest(const FString& Parameters)
{
	using Access = FVBTraversalTestAccess;
	VBLadderTraversalTest::FSessionFixture F;
	if (!F.Build(*this))
	{
		return false;
	}

	UVBTraversalComponent& T = *F.Traversal;
	const FVector2D Input(0.7, 0.4);
	TestFalse(TEXT("off a surface the input is not consumed"), T.TryConsumeMoveInput(Input));

	Access::StartLadder(T, F.Ladder);
	TestTrue(TEXT("the ladder consumes the input"), T.TryConsumeMoveInput(Input));
	TestEqual(TEXT("the ladder takes the vertical axis"), Access::Mode(T).GetState().MoveInput, 0.4f);
	T.RequestLadderDrop();

	Access::Attach(T, EVBTraversalSurface::Climb);
	TestTrue(TEXT("the climb consumes the input"), T.TryConsumeMoveInput(Input));
	TestTrue(TEXT("the climb takes both axes"), Access::Climb(T).MoveInput.Equals(Input));
	TestTrue(TEXT("a zero input reaches the climb"), T.TryConsumeMoveInput(FVector2D::ZeroVector));
	TestTrue(TEXT("the zero input clears the climb"), Access::Climb(T).MoveInput.IsZero());
	Access::EndSession(T);

	Access::Attach(T, EVBTraversalSurface::Hang);
	TestTrue(TEXT("the hang consumes the input"), T.TryConsumeMoveInput(FVector2D(0.7, 0.0)));
	TestEqual(TEXT("the hang takes the sideways axis"), Access::Hang(T).MoveInput.X, 0.7, 1e-4);
	Access::EndSession(T);

	TestFalse(TEXT("after the session the input is not consumed"), T.TryConsumeMoveInput(Input));
	return true;
}

// 조정값으로 옮긴 숫자는 옮기기 전 코드의 값이어야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBClimbAnimSetTunableDefaultsTest, "VB.Unit.Traversal.ClipTunableDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBClimbAnimSetTunableDefaultsTest::RunTest(const FString& Parameters)
{
	const UVBClimbAnimSet* Defaults = GetDefault<UVBClimbAnimSet>();
	TestEqual(TEXT("montage stop blend"), Defaults->MontageStopBlendTime, 0.15f);
	TestEqual(TEXT("hang shimmy threshold"), Defaults->HangShimmyInputThreshold, 0.2f);
	TestEqual(TEXT("climb input size squared"), Defaults->ClimbMoveInputSizeSquaredMin, 0.04f);
	TestEqual(TEXT("climb axis threshold"), Defaults->ClimbAxisInputThreshold, 0.4f);
	TestEqual(TEXT("climb correction fade"), Defaults->ClimbIKCorrectionFadeSpeed, 10.0f);
	TestEqual(TEXT("climb step scale"), Defaults->ClimbMaxStepSpeedScale, 8.0f);
	TestEqual(TEXT("ladder input threshold"), Defaults->LadderMoveInputThreshold, 0.2f);
	TestEqual(TEXT("ladder step scale"), Defaults->LadderMaxStepSpeedScale, 8.0f);
	return true;
}

#if WITH_EDITOR
// chooser 없는 설정은 검증에서 걸리고, 배포 자산은 값을 갖고 있어야 맨틀이 돈다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTraversalConfigChooserRequiredTest, "VB.Unit.Traversal.ConfigChooserRequired",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTraversalConfigChooserRequiredTest::RunTest(const FString& Parameters)
{
	const UVBTraversalConfig* Empty = NewObject<UVBTraversalConfig>();
	FDataValidationContext EmptyContext;
	TestTrue(TEXT("no chooser is invalid"), Empty->IsDataValid(EmptyContext) == EDataValidationResult::Invalid);

	const UVBTraversalConfig* Shipped = LoadObject<UVBTraversalConfig>(nullptr,
		TEXT("/Game/VB/Data/DA_Traversal_Default.DA_Traversal_Default"));
	if (!TestNotNull(TEXT("shipped config"), Shipped))
	{
		return false;
	}
	FDataValidationContext ShippedContext;
	TestTrue(TEXT("shipped config has its chooser"), Shipped->IsDataValid(ShippedContext) != EDataValidationResult::Invalid);
	return true;
}
#endif

// 복구는 세션을 먼저 끝내므로 정리 중의 Detach 가 올라서기 태그를 켜지 않는다. 사다리 놓기와 등반 복구 둘 다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTraversalRecoverNoExitTest, "VB.Unit.Traversal.RecoverNeverRaisesExit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBTraversalRecoverNoExitTest::RunTest(const FString& Parameters)
{
	using Access = FVBTraversalTestAccess;
	VBLadderTraversalTest::FSessionFixture F;
	if (!F.Build(*this))
	{
		return false;
	}

	UVBTraversalComponent& T = *F.Traversal;
	int32 ExitRaised = 0;
	const FDelegateHandle Handle = F.ASC->RegisterGameplayTagEvent(VBGameplayTags::State_Traversal_Exit,
		EGameplayTagEventType::NewOrRemoved)
		.AddLambda([&ExitRaised](const FGameplayTag, int32 Count) { ExitRaised += Count > 0 ? 1 : 0; });
	ON_SCOPE_EXIT { F.ASC->RegisterGameplayTagEvent(VBGameplayTags::State_Traversal_Exit, EGameplayTagEventType::NewOrRemoved).Remove(Handle); };

	Access::StartLadder(T, F.Ladder);
	T.RequestLadderDrop();
	TestTrue(TEXT("the ladder drop ended the session"), T.GetTraversalPhase() == EVBTraversalPhase::None);
	TestEqual(TEXT("the ladder drop never raised the exit tag"), ExitRaised, 0);

	Access::Attach(T, EVBTraversalSurface::Climb);
	Access::Recover(T);
	TestTrue(TEXT("the climb recovery ended the session"), T.GetTraversalPhase() == EVBTraversalPhase::None);
	TestEqual(TEXT("the climb recovery never raised the exit tag"), ExitRaised, 0);
	return true;
}

// 두 축에 값을 넣는 곳은 SetTraversalState 하나다. 트래버설 소스에 다른 대입이 생기면 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTraversalSessionOneWriterTest, "VB.Unit.Traversal.SessionAxesOneWriter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBTraversalSessionOneWriterTest::RunTest(const FString& Parameters)
{
	const auto CountIn = [](const FString& Text)
	{
		int32 Count = 0;
		for (const TCHAR* Needle : { TEXT("TraversalPhase = "), TEXT("AttachedSurface = ") })
		{
			for (int32 At = Text.Find(Needle, ESearchCase::CaseSensitive); At != INDEX_NONE; At = Text.Find(Needle,
				ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 1))
			{
				++Count;
			}
		}

		return Count;
	};
	TArray<FString> Files;
	const FString Root = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/VB"));
	IFileManager::Get().FindFilesRecursive(Files, *FPaths::Combine(Root, TEXT("Private/Traversal")), TEXT("*.cpp"), true, false);
	IFileManager::Get().FindFilesRecursive(Files, *FPaths::Combine(Root, TEXT("Public/Traversal")), TEXT("*.h"), true, false, false);
	if (!TestTrue(TEXT("traversal sources found"), Files.Num() > 0))
	{
		return false;
	}

	for (const FString& File : Files)
	{
		FString Text;
		if (!TestTrue(FString::Printf(TEXT("read %s"), *File), FFileHelper::LoadFileToString(Text, *File)))
		{
			continue;
		}

		const FString Name = FPaths::GetCleanFilename(File);
		int32 Allowed = 0;
		if (Name == TEXT("VBTraversalComponent.h"))
		{
			Allowed = 2; // 멤버 기본값
		}
		else if (Name == TEXT("VBTraversalComponent.cpp"))
		{
			const int32 Begin = Text.Find(TEXT("void UVBTraversalComponent::SetTraversalState("));
			const int32 End = Begin == INDEX_NONE ? INDEX_NONE : Text.Find(TEXT("\n}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Begin);
			if (!TestTrue(TEXT("SetTraversalState body found"), Begin != INDEX_NONE && End != INDEX_NONE))
			{
				continue;
			}

			TestEqual(TEXT("SetTraversalState writes both axes"), CountIn(Text.Mid(Begin, End - Begin)), 2);
			Allowed = 2;
		}

		TestEqual(FString::Printf(TEXT("%s: axis assignments"), *Name), CountIn(Text), Allowed);
	}

	return true;
}
// 벽 옆 끝: 몸을 둘 자리는 가는 쪽 손발 자리까지 면이 있어야 한다. 진입도 같은 판정을 쓴다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTraversalClimbSideEdgeTest, "VB.Unit.Traversal.ClimbSideEdgeFootprint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTraversalClimbSideEdgeTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this, /*bPhysicsScene=*/true)) return false;
	UVBClimbAnimSet* Clips = VBLadderTraversalTest::DefaultClips();
	const UVBTraversalConfig* Cfg = GetDefault<UVBTraversalConfig>();
	if (!TestNotNull(TEXT("default clips"), Clips))
	{
		return false;
	}

	const double Span = Clips->ClimbLimbHalfSpan;
	TestTrue(TEXT("the shipped clip set has a limb footprint"), Span > 0.0);

	// 벽: 앞면 X=-25(법선 -X, 오른쪽 축 +Y), 폭 Y -200~200, 높이 Z -200~200.
	AActor* Wall = Isolated.World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
	Wall->AddInstanceComponent(Box);
	Wall->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(25.0, 200.0, 200.0));
	Box->SetCollisionProfileName(TEXT("VBClimbable"));
	Box->RegisterComponent();

	AVBCharacter* Body = Isolated.World->SpawnActor<AVBCharacter>(FVector(-120.0, 0.0, 0.0), FRotator::ZeroRotator);
	UVBTraversalComponent* Traversal = UVBTraversalComponent::FindFor(Body);
	if (!TestNotNull(TEXT("traversal"), Traversal))
	{
		return false;
	}

	VBLadderTraversalTest::GiveAbilitySystem(*Body);
	FVBTraversalTestAccess::PrimeOwnerWithoutAnimSet(*Traversal, Body);
	FVBTraversalTestAccess::SetAnimSet(*Traversal, Clips);

	const FVector N(-1.0, 0.0, 0.0);
	const double Z = -150.0;
	const FVector NearRight(-25.0, 200.0 - Span + 5.0, Z);
	TestFalse(TEXT("near the right edge: the right footprint is off the wall"),
		FVBTraversalTestAccess::HasFootprint(*Traversal, NearRight, N, 1.0f));
	TestTrue(TEXT("near the right edge: the left footprint is on the wall"),
		FVBTraversalTestAccess::HasFootprint(*Traversal, NearRight, N, -1.0f));
	const FVector Inside(-25.0, 200.0 - Span - 5.0, Z);
	TestTrue(TEXT("inside: the right footprint is on the wall"), FVBTraversalTestAccess::HasFootprint(*Traversal, Inside, N, 1.0f));
	TestTrue(TEXT("inside: the left footprint is on the wall"), FVBTraversalTestAccess::HasFootprint(*Traversal, Inside, N, -1.0f));
	// 손 높이에 면이 없으면 발 높이에 있어도 아니다.
	const FVector UnderTop(-25.0, 0.0, 200.0 - Cfg->ClimbBodyProbeRise + 5.0);
	TestFalse(TEXT("hand height above the wall: no footprint"), FVBTraversalTestAccess::HasFootprint(*Traversal, UnderTop, N, 1.0f));
	TestFalse(TEXT("no side is never a footprint"), FVBTraversalTestAccess::HasFootprint(*Traversal, Inside, N, 0.0f));

	// 진입: 오른쪽 모서리 앞에서는 거절하고 세션을 열지 않는다. 가운데에서는 붙는다.
	Body->SetActorLocation(FVector(-120.0, 200.0 - Span + 5.0, 0.0));
	TestFalse(TEXT("entry at the right edge is refused"), FVBTraversalTestAccess::TryStartClimb(*Traversal));
	TestFalse(TEXT("refused entry opens no session"), Traversal->IsDoingTraversal());
	Body->SetActorLocation(FVector(-120.0, 0.0, 0.0));
	TestTrue(TEXT("entry in the middle starts"), FVBTraversalTestAccess::TryStartClimb(*Traversal));
	TestTrue(TEXT("entry in the middle climbs"), Traversal->IsClimbing());

	// 이동: 가운데에서는 옆으로 간다. 오른쪽 끝에서는 옆 성분만 지워지고 위로는 간다.
	FVector& Surface = FVBTraversalTestAccess::ClimbSurfacePoint(*Traversal);
	Traversal->SetClimbMoveInput(FVector2D(1.0, 0.0));
	const FVector MiddleBefore = Surface;
	FVBTraversalTestAccess::UpdateClimb(*Traversal, 0.1f);
	TestTrue(TEXT("in the middle the climber moves right"), Surface.Y > MiddleBefore.Y + 1.0);
	Surface.Y = 200.0 - Span;
	const FVector EdgeBefore = Surface;
	FVBTraversalTestAccess::UpdateClimb(*Traversal, 0.1f);
	TestEqual(TEXT("at the right edge the sideways input is refused"), Surface.Y, EdgeBefore.Y, 0.5);
	Traversal->SetClimbMoveInput(FVector2D(1.0, 1.0));
	FVBTraversalTestAccess::UpdateClimb(*Traversal, 0.1f);
	TestEqual(TEXT("at the right edge up and right still refuses sideways"), Surface.Y, EdgeBefore.Y, 0.5);
	TestTrue(TEXT("at the right edge up and right still goes up"), Surface.Z > EdgeBefore.Z + 1.0);

	// 말단 속도 측정은 콘솔 변수가 켜졌을 때만 돈다. 위 틱들은 꺼진 채로 돌았다.
	IConsoleVariable* Telemetry = IConsoleManager::Get().FindConsoleVariable(TEXT("vb.Traversal.StatusTelemetry"));
	if (!TestNotNull(TEXT("status telemetry variable"), Telemetry))
	{
		return false;
	}

	TestFalse(TEXT("status telemetry is off by default"), VBTraversalDiagnostics::IsStatusTelemetryEnabled());
	TestFalse(TEXT("telemetry off: no limb is sampled"), FVBTraversalTestAccess::Climb(*Traversal).Diag.bLimbRatePrimed);
	Telemetry->Set(1, ECVF_SetByCode);
	ON_SCOPE_EXIT { Telemetry->Set(0, ECVF_SetByCode); };
	FVBTraversalTestAccess::UpdateClimb(*Traversal, 0.1f);
	TestTrue(TEXT("telemetry on: limbs are sampled"), FVBTraversalTestAccess::Climb(*Traversal).Diag.bLimbRatePrimed);

	Traversal->SetClimbMoveInput(FVector2D::ZeroVector);
	FVBTraversalTestAccess::Recover(*Traversal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLogGateTest, "VB.Unit.Traversal.LogGate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBLogGateTest::RunTest(const FString& Parameters)
{
	// 진단 로그 간격 - 처음은 바로 찍고, 간격 전에는 막고, 정확히 간격이 지난 순간(>=)에는 다시 찍는다.
	FVBLogGate Gate{0.5};
	TestTrue(TEXT("first call logs"), Gate.Ready(10.0));
	TestFalse(TEXT("inside the interval it stays quiet"), Gate.Ready(10.49));
	TestTrue(TEXT("exactly one interval later it logs again"), Gate.Ready(10.5));
	TestEqual(TEXT("next time moves one interval past the log"), Gate.NextTime, 11.0);
	return true;
}

#endif
