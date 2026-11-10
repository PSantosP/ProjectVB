#include <limits>
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNode_VBLadderIK.h"
#include "Animation/VBAnimInstance.h"
#include "Animation/VBLadderIKContact.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderContactTest, "VB.Unit.LadderIK.ContactLock",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderContactTest::RunTest(const FString& Parameters)
{
	FVBLadderIKGeometry Geometry;
	Geometry.Height = 400; Geometry.Width = 66; Geometry.Spacing = 40; Geometry.Drop = 10;
	FVBLadderIKContact State;
	const float Dt = 1.0f / 60.0f;
	FVector Raw(8, 20, 151);
	State.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f);
	TestFalse(TEXT("first sample cannot classify a moving contact"), State.bPlanted);
	State.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f);
	TestFalse(TEXT("one slow swing-apex frame must not plant"), State.bPlanted);
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		State.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f);
	}

	TestTrue(TEXT("stationary contact plants"), State.bPlanted);
	TestEqual(TEXT("planted weight reaches one"), State.Weight, 1.0f);
	const FVector Anchor = State.Anchor;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Raw.Y += 0.02; Raw.Z += 0.05;
		State.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f);
	}

	TestTrue(TEXT("raw drift cannot move the planted rung or lateral anchor"), State.Anchor.Equals(Anchor));
	Raw.Z += 2;
	State.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f);
	TestFalse(TEXT("reach releases the old rung"), State.bPlanted);
	TestTrue(TEXT("release fades on the same target"), State.Anchor.Equals(Anchor));
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Raw.Z += 2;
		State.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f);
	}

	TestEqual(TEXT("swing becomes fully uncorrected"), State.Weight, 0.0f);
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		State.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f);
	}

	TestTrue(TEXT("next hold plants a new rung"), State.Anchor.Z > Anchor.Z);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderPoseDepthTest, "VB.Unit.LadderIK.PoseDepthAndCapture",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderPoseDepthTest::RunTest(const FString& Parameters)
{
	FVBLadderIKGeometry Geometry;
	Geometry.Height = 400; Geometry.Width = 66; Geometry.Spacing = 40; Geometry.Drop = 10;
	const float Dt = 1.0f / 60.0f;
	const FVector Source(-25.9, 20, 72);
	FVBLadderIKContact FarContact;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		FarContact.Update(Geometry, Source, Dt, 0, 15, 40, 0.08f, 0.06f, 12);
	}

	TestFalse(TEXT("a slow foot 26cm behind the rung must not acquire contact"), FarContact.bPlanted);
	TestEqual(TEXT("time far from the rung does not count as contact dwell"), FarContact.QuietTime, 0.0f);

	const FVector PoseOffset = Geometry.GetPoseDepthOffset(63.9f, 38.0f);
	TestTrue(TEXT("pose adaptation aligns the source support plane"), FMath::IsNearlyZero((Source + PoseOffset).X, 0.001));
	for (int32 Frame = 0; Frame < 20; ++Frame)
	{
		FarContact.Update(Geometry, Source + PoseOffset, Dt, 0, 15, 40, 0.08f, 0.06f, 12);
	}

	TestTrue(TEXT("the adapted nearby support pose acquires contact"), FarContact.bPlanted);
	const FVector Anchor = FarContact.Anchor;
	for (int32 Frame = 1; Frame <= 12; ++Frame)
	{
		FarContact.Update(Geometry, Source + PoseOffset + FVector(0, 0, Frame * 2), Dt,
			0, 15, 40, 0.08f, 0.06f, 12);
	}

	TestFalse(TEXT("swing releases contact"), FarContact.bPlanted);
	TestEqual(TEXT("contact fade finishes during swing"), FarContact.Weight, 0.0f);
	TestTrue(TEXT("release does not move the old anchor"), FarContact.Anchor.Equals(Anchor));
	TestTrue(TEXT("pose depth adaptation survives release"), Geometry.GetPoseDepthOffset(63.9f, 38.0f).Equals(PoseOffset));

	Geometry.ToWorld = FTransform(FRotator(0, 90, 0), FVector(100, 200, 0), FVector(2, 1, 1));
	Geometry.Depth = 5;
	const FVector RotatedOffset = Geometry.GetPoseDepthOffset(63.9f, 38.0f);
	TestTrue(TEXT("world support distance accounts for rotated and scaled rung depth"),
		RotatedOffset.Equals(FVector(0, 15.9, 0), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderHandContactTest, "VB.Unit.LadderIK.HandContactTransition",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderHandContactTest::RunTest(const FString& Parameters)
{
	FVBLadderIKGeometry Geometry;
	Geometry.Height = 400; Geometry.Width = 66; Geometry.Spacing = 40.255f; Geometry.Drop = 9.6f;
	FVBLadderIKContact Hand;
	const float Dt = 1.0f / 60.0f;
	const auto UpdateHand = [&](const FVector& Raw, float Step)
	{
		Hand.Update(Geometry, Raw, Step, 0, 30, 40, 0.08f, 0, 36, true, 6);
	};
	// 14:23 로그: 같은 봉 부근의 앞뒤 정렬을 3D 속도로 해제했던 재현 구간.
	const FVector Initial(-7.478, 28.138, 225.497);
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		UpdateHand(Initial, Dt);
	}

	TestTrue(TEXT("initial hand support is planted"), Hand.bPlanted);
	const FVector Anchor = Hand.Anchor;
	const FVector Samples[] = {
		FVector(-7.650, 28.048, 226.279), FVector(-6.459, 28.338, 226.251),
		FVector(-3.529, 28.805, 227.084), FVector(3.004, 28.624, 230.547),
		FVector(0.145, 28.658, 230.568), FVector(-5.154, 28.476, 229.575),
		FVector(-7.405, 28.269, 229.140) };
	const float Steps[] = { 0.249f, 0.062f, 0.046f, 0.108f, 0.106f, 0.110f, 0.111f };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Samples); ++Index)
	{
		UpdateHand(Samples[Index], Steps[Index]);
		TestTrue(TEXT("same-rung alignment keeps the hand planted"), Hand.bPlanted);
		TestTrue(TEXT("same-rung alignment preserves the anchor"), Hand.Anchor.Equals(Anchor));
		TestEqual(TEXT("same-rung alignment does not fade IK"), Hand.Weight, 1.0f);
	}
	// 축 이동이 6cm를 넘고 빠르게 다음 봉으로 이동하면 실제로 놓는다.
	UpdateHand(FVector(-7.405, 28.269, 235.140), Dt);
	TestFalse(TEXT("real upward reach releases the hand"), Hand.bPlanted);
	for (int32 Frame = 1; Frame <= 12; ++Frame)
	{
		UpdateHand(FVector(-7.405, 28.269, 235.140 + Frame * 3), Dt);
		TestFalse(TEXT("fast passage through an intermediate rung cannot plant"), Hand.bPlanted);
	}

	TestEqual(TEXT("swing releases all contact weight"), Hand.Weight, 0.0f);
	// 앞뒤 속도가 높아도 축 이동이 착지 속도로 줄면 정지 대기 없이 새 봉을 잡기 시작한다.
	UpdateHand(FVector(-5.405, 28.269, 271.140 + 20 * Dt), Dt);
	TestTrue(TEXT("arrival starts IK before waiting for full 3D stillness"), Hand.bPlanted);
	TestTrue(TEXT("arrival starts the blend immediately"), Hand.Weight > 0.0f);
	TestTrue(TEXT("arrival selects a higher rung"), Hand.Anchor.Z > Anchor.Z);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderGeometryTest, "VB.Unit.LadderIK.VisibleRungs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderGeometryTest::RunTest(const FString& Parameters)
{
	FVBLadderIKGeometry Geometry;
	Geometry.Height = 400; Geometry.Width = 66; Geometry.Spacing = 20; Geometry.Drop = 9.6f;
	Geometry.Depth = 5;
	FVector Result;
	TestTrue(TEXT("lower boundary has an actual rung"), Geometry.FindContact(FVector(0, 100, -20), 0, Result));
	TestTrue(TEXT("never invent a rung below the base"), Result.Z >= 0);
	TestEqual(TEXT("contact stays within bar width"), Result.Y, 33.0);
	TestEqual(TEXT("bar depth matches mesh"), Result.X, -5.0);
	Geometry.FindContact(FVector(0, 0, 600), 0, Result);
	TestTrue(TEXT("never invent a rung above the top"), Result.Z <= 400);
	Geometry.FindContact(FVector(0, 0, 55.5), 5.14f, Result);
	TestTrue(TEXT("subdivision rung plus foot clearance"), FMath::IsNearlyEqual(Result.Z, 55.54, 0.001));
	Geometry.Height = 1;
	TestFalse(TEXT("no visual rung means no contact"), Geometry.FindContact(FVector::ZeroVector, 0, Result));
	return true;
}

// 접촉 문턱은 clip 단위다 - 2배속이면 같은 팔다리 움직임이 두 배 빠르고 반만 머무르니 속도는 r, 시간은 1/r 로 늘린다.
// 공간 값은 늘리지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderClipRateTest, "VB.Unit.LadderIK.ClipRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderClipRateTest::RunTest(const FString& Parameters)
{
	const FVBLadderIKClipRate One = FVBLadderIKClipRate::FromEvaluated(true, true, 1.0f);
	TestTrue(TEXT("1x is the authored unit"), One.Source == EVBLadderIKRateSource::Clip && One.Speed(15) == 15 && One.Time(0.06f) == 0.06f);
	const FVBLadderIKClipRate Two = FVBLadderIKClipRate::FromEvaluated(true, true, -2.0f);
	TestTrue(TEXT("2x (sign ignored) doubles speeds and halves times"),
		Two.Source == EVBLadderIKRateSource::Clip && Two.Speed(15) == 30 && FMath::IsNearlyEqual(Two.Time(0.08f), 0.04f));
	const FVBLadderIKClipRate None = FVBLadderIKClipRate::FromEvaluated(false, true, 2.0f);
	TestTrue(TEXT("no ladder montage compares in world units"), None.Source == EVBLadderIKRateSource::NoMontage && None.Rate == 1);
	const FVBLadderIKClipRate Paused = FVBLadderIKClipRate::FromEvaluated(true, false, 2.0f);
	TestTrue(TEXT("a paused instance keeps its rate but does not move the limbs"), Paused.Source == EVBLadderIKRateSource::Stopped
		&& Paused.Rate == 1);
	const FVBLadderIKClipRate Still = FVBLadderIKClipRate::FromEvaluated(true, true, 0.001f);
	TestTrue(TEXT("an effectively stopped clip compares in world units"), Still.Source == EVBLadderIKRateSource::Stopped
		&& Still.Rate == 1);
	const FVBLadderIKClipRate Broken = FVBLadderIKClipRate::FromEvaluated(true, true, std::numeric_limits<float>::quiet_NaN());
	TestTrue(TEXT("a non-finite rate is reported as invalid, not as stopped"), Broken.Source == EVBLadderIKRateSource::Invalid
		&& Broken.Rate == 1);

	// 가로대 근처의 한 발: 빠른 접근, 10 cm/s 로 느린 머무름, 빠른 떠남(1x clip, 샘플당 1/60 s).
	// 단계 전환을 짝수 샘플에 두어 2x 재생(한 샘플 건너)이 같은 구간 속도의 두 배를 보게 한다.
	FVBLadderIKGeometry Geometry;
	Geometry.Height = 400; Geometry.Width = 66; Geometry.Spacing = 40.255f; Geometry.Drop = 9.6f;
	TArray<float> Z;
	for (int32 K = 0; K <= 70; ++K)
	{
		Z.Add(K <= 20 ? 49.4f + K : K <= 50 ? 69.4f + (K - 20) * (10.0f / 60.0f) : 74.4f + (K - 50));
	}

	const float Dt = 1.0f / 60.0f;
	const auto Run = [&](const FVBLadderIKClipRate& Rate, int32 Step, TArray<bool>& Planted, TArray<bool>& Full)
	{
		FVBLadderIKContact Foot;
		for (int32 K = 0; K < Z.Num(); K += Step)
		{
			Foot.Update(Geometry, FVector(0, 0, Z[K]), Dt, 0, Rate.Speed(15), Rate.Speed(40), Rate.Time(0.08f), Rate.Time(0.06f), 12);
			Planted.Add(Foot.bPlanted);
			Full.Add(Foot.Weight >= 0.999f);
		}
	};
	TArray<bool> Planted1, Full1, Planted2, Full2, PlantedRaw, FullRaw;
	Run(One, 1, Planted1, Full1);
	Run(Two, 2, Planted2, Full2);
	Run(One, 2, PlantedRaw, FullRaw);
	TestTrue(TEXT("the 1x control reaches full weight during the hold"), Full1.Contains(true));
	for (int32 J = 0; J < Planted2.Num(); ++J)
	{
		TestEqual(*FString::Printf(TEXT("2x plant state matches 1x at clip sample %d"), J * 2), Planted2[J], Planted1[J * 2]);
		TestEqual(*FString::Printf(TEXT("2x full weight matches 1x at clip sample %d"), J * 2), Full2[J], Full1[J * 2]);
	}

	TestFalse(TEXT("2x with 1x thresholds never plants (the measured 0/79 shape)"), PlantedRaw.Contains(true));
	return true;
}

// 노드가 실제 사다리 montage 에서 읽는 rate. 1x 가 그대로여야 하니 저작된 rate 는 1 이어야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderClipRateReadTest, "VB.Unit.LadderIK.ClipRateRead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBLadderClipRateReadTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("rate fixture world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACharacter* Character = World->SpawnActor<ACharacter>();
	auto* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
	if (!TestNotNull(TEXT("fixture character"), Character) || !TestNotNull(TEXT("player skeleton"), SkeletalMesh))
	{
		return false;
	}

	Character->GetMesh()->SetSkeletalMeshAsset(SkeletalMesh);
	Character->GetMesh()->SetAnimInstanceClass(UVBAnimInstance::StaticClass());
	UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance();
	if (!TestNotNull(TEXT("anim instance"), Anim))
	{
		return false;
	}

	const TCHAR* Paths[] = {
		TEXT("/Game/VB/Animations/Montage/Traversal/AM_VB_Ladder_Up.AM_VB_Ladder_Up"),
		TEXT("/Game/VB/Animations/Montage/Traversal/AM_VB_Ladder_Down.AM_VB_Ladder_Down"),
		TEXT("/Game/VB/Animations/Montage/Traversal/AM_VB_Ladder_Idle.AM_VB_Ladder_Idle"),
		TEXT("/Game/VB/Animations/Montage/Traversal/AM_VB_Ladder_Idle_Mirror.AM_VB_Ladder_Idle_Mirror") };
	UAnimMontage* Up = nullptr;
	for (const TCHAR* Path : Paths)
	{
		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, Path);
		if (!TestNotNull(*FString::Printf(TEXT("ladder montage %s"), Path), Montage))
		{
			return false;
		}

		Up = Up ? Up : Montage;
		TestEqual(*FString::Printf(TEXT("%s has one slot track"), Path), Montage->SlotAnimTracks.Num(), 1);
		TestEqual(*FString::Printf(TEXT("%s authored RateScale is 1"), Path), Montage->RateScale, 1.0f);
		for (const FAnimSegment& Segment : Montage->SlotAnimTracks[0].AnimTrack.AnimSegments)
		{
			TestEqual(*FString::Printf(TEXT("%s segment x sequence rate is 1"), Path), Segment.GetValidPlayRate(), 1.0f);
		}
	}

	TestTrue(TEXT("no montage reads as NoMontage"),
		FAnimNode_VBLadderIK::ReadClipRate(Anim, nullptr).Source == EVBLadderIKRateSource::NoMontage);
	TestTrue(TEXT("a montage that is not playing reads as NoMontage"),
		FAnimNode_VBLadderIK::ReadClipRate(Anim, Up).Source == EVBLadderIKRateSource::NoMontage);
	if (!TestTrue(TEXT("Up plays"), Anim->Montage_Play(Up, 1.0f) > 0.0f))
	{
		return false;
	}

	FVBLadderIKClipRate Rate = FAnimNode_VBLadderIK::ReadClipRate(Anim, Up);
	TestTrue(TEXT("1x Up reads Clip 1"), Rate.Source == EVBLadderIKRateSource::Clip && FMath::IsNearlyEqual(Rate.Rate, 1.0f));
	Anim->Montage_SetPlayRate(Up, 2.0f);
	Rate = FAnimNode_VBLadderIK::ReadClipRate(Anim, Up);
	TestTrue(TEXT("sprint 2x reads Clip 2"), Rate.Source == EVBLadderIKRateSource::Clip && FMath::IsNearlyEqual(Rate.Rate, 2.0f));
	Anim->Montage_Pause(Up);
	TestTrue(TEXT("pause keeps the positive rate"), Anim->Montage_GetPlayRate(Up) > 0.0f);
	Rate = FAnimNode_VBLadderIK::ReadClipRate(Anim, Up);
	TestTrue(TEXT("a paused Up reads Stopped"), Rate.Source == EVBLadderIKRateSource::Stopped && Rate.Rate == 1.0f);
	Anim->Montage_Resume(Up);
	Anim->Montage_SetPlayRate(Up, 0.0f);
	TestTrue(TEXT("rate 0 reads Stopped"), FAnimNode_VBLadderIK::ReadClipRate(Anim, Up).Source == EVBLadderIKRateSource::Stopped);
	return true;
}

// 사다리 top-out 지지: 마지막 지지가 top-out montage 내내 이어지고, 새로 딛지 않고, 발마다 base 보정은 그 발이 들릴 때까지 유지되며,
// 모든 끝은 튀지 않고 서서히 사라진다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderTopOutSupportTest, "VB.Unit.LadderIK.TopOutSupport",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderTopOutSupportTest::RunTest(const FString& Parameters)
{
	using Rule = FVBLadderIKModeRule;
	TestTrue(TEXT("on the ladder"), Rule::Resolve(true, false, false, false, false) == EVBLadderIKMode::Ladder);
	TestTrue(TEXT("top-out keeps the support"), Rule::Resolve(false, true, true, true, true) == EVBLadderIKMode::Support);
	TestTrue(TEXT("finish / interrupt / play failure clear the request: residual fades"),
		Rule::Resolve(false, false, false, true, true) == EVBLadderIKMode::Fade);
	TestTrue(TEXT("ladder destroyed during top-out: cached geometry fades"),
		Rule::Resolve(false, true, false, true, true) == EVBLadderIKMode::Fade);
	TestTrue(TEXT("nothing left: off"), Rule::Resolve(false, false, false, true, false) == EVBLadderIKMode::Off);
	TestTrue(TEXT("no geometry cannot fade"), Rule::Resolve(false, true, true, false, true) == EVBLadderIKMode::Off);

	// 지지는 새로 딛기를 거절하되 이미 쥔 것은 유지하고 놓는다.
	FVBLadderIKGeometry Geometry;
	Geometry.Height = 400; Geometry.Width = 66; Geometry.Spacing = 40; Geometry.Drop = 10;
	const float Dt = 1.0f / 60.0f;
	FVBLadderIKContact Held;
	FVector Raw(0, 0, 150);
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		Held.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f);
	}

	if (!TestTrue(TEXT("planted on the ladder"), Held.bPlanted))
	{
		return false;
	}

	const FVector Anchor = Held.Anchor;
	for (int32 Frame = 0; Frame < 6; ++Frame)
	{
		Held.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f, 0.06f, MAX_flt, false, 0, false);
	}

	TestTrue(TEXT("support keeps the held anchor"), Held.bPlanted && Held.Anchor.Equals(Anchor) && Held.Weight == 1.0f);
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Raw.Z += 2;
		Held.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f, 0.06f, MAX_flt, false, 0, false);
	}

	TestFalse(TEXT("the clip lifting the limb releases it"), Held.bPlanted);
	TestEqual(TEXT("released support fades to zero"), Held.Weight, 0.0f);
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Held.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f, 0.06f, MAX_flt, false, 0, false);
	}

	TestFalse(TEXT("support never plants again, even when still near a rung"), Held.bPlanted);
	FVBLadderIKContact Fresh;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Fresh.Update(Geometry, FVector(0, 0, 150), Dt, 0, 15, 40, 0.08f, 0.06f, MAX_flt, false, 0, false);
	}

	TestFalse(TEXT("an unplanted limb gets no fictitious anchor"), Fresh.bPlanted || Fresh.Weight > 0.0f);

	// 발마다 base: 들릴 때까지 유지하고 contact blend 동안 사라진다. 끝나면 남은 것을 사라지게 한다.
	FVBLadderIKSupportBase Base;
	Base.Begin(1.0f);
	for (int32 Frame = 0; Frame < 20; ++Frame)
	{
		Base.Update(false, false, Dt, 0.08f);
	}

	TestEqual(TEXT("an unlifted foot keeps its full base"), Base.Weight, 1.0f);
	Base.Update(true, false, Dt, 0.08f);
	TestTrue(TEXT("lift starts the fade"), Base.Weight < 1.0f && Base.Weight > 0.0f);
	for (int32 Frame = 0; Frame < 4; ++Frame)
	{
		Base.Update(false, false, Dt, 0.08f);
	}

	TestEqual(TEXT("the fade finishes over the blend time (5 frames at 0.08 s)"), Base.Weight, 0.0f);
	FVBLadderIKSupportBase Ending;
	Ending.Begin(0.6f);
	Ending.Update(false, true, Dt, 0.08f);
	TestTrue(TEXT("an ending fades from the current value, never jumps"), Ending.Weight < 0.6f && Ending.Weight > 0.0f);

	// 손: 마지막 요청 보정을 얼려 서서히 줄인다 - 첫 프레임 그대로, 커지지 않고, blend 시간 안에 사라진다.
	// 끊겨도 다시 시작하지 않고 계속 줄며, 다시 들어오면 지운다.
	FVBLadderIKHandHold Hand;
	const FVector Last(3.0f, -2.0f, 8.0f);
	Hand.Begin(Last);
	TestTrue(TEXT("the handoff frame applies the frozen correction unchanged"), Hand.Step(Dt, 0.08f).Equals(Last));
	double Previous = Last.Size();
	bool bMonotonic = true;
	for (int32 Frame = 0; Frame < 8; ++Frame)
	{
		const double Now = Hand.Step(Dt, 0.08f).Size();
		bMonotonic &= Now <= Previous + 1e-6;
		Previous = Now;
	}

	TestTrue(TEXT("the frozen correction only shrinks"), bMonotonic);
	TestFalse(TEXT("the frozen correction is gone after the blend time"), Hand.IsActive());
	FVBLadderIKHandHold Interrupted;
	Interrupted.Begin(Last);
	Interrupted.Step(Dt, 0.08f);
	Interrupted.Step(Dt, 0.08f);
	const float MidWeight = Interrupted.Weight;
	Interrupted.Step(Dt, 0.08f);
	TestTrue(TEXT("an interrupt keeps fading instead of restarting"), Interrupted.Weight < MidWeight);
	Interrupted.Clear();
	TestTrue(TEXT("re-entry clears the frozen correction"), !Interrupted.IsActive() && Interrupted.Step(Dt, 0.08f).IsNearlyZero());
	return true;
}
#endif
