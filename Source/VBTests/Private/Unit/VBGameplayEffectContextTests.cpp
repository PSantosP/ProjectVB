// ProjectVB Copyright (c) 2025. All Rights Reserved.

// 타격 사실을 싣는 FVBGameplayEffectContext 의 우리 코드만 본다 - 필드가 NetSerialize 를 지나 살아남고, base NetSerialize 가 먼저 돌고,
// handle 이 파생 타입을 되살리고, Duplicate 가 깊게 복사하고, 읽는 쪽 기본값이 맞는지.
// base 참조(instigator, causer, hit result)의 원격 전송은 엔진 몫이라 네트워크 실행에서 본다.

#include "Misc/AutomationTest.h"

#include "AbilitySystemGlobals.h"
#include "AbilitySystem/VBAbilitySystemGlobals.h"
#include "AbilitySystem/VBGameplayEffectContext.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBHitImpact.h"
#include "UObject/CoreNet.h"

// 익명이 아닌 이름 있는 namespace - unity build 가 여러 시험 파일을 한 번역 단위로 묶으면 VBHitImpactTests.cpp 의 익명 ImpactOrd 와 겹친다(C2084).
namespace VBGameplayEffectContextTest
{
	int32 ImpactOrd(EVBHitImpact Impact) { return static_cast<int32>(Impact); }

	struct FFacts
	{
		EVBHitImpact Impact = EVBHitImpact::Light;
		float Height = 0.0f;
		bool bYaw = false;
		float Yaw = 0.0f;
	};

	void WriteFacts(FVBGameplayEffectContext& Context, const FFacts& Facts)
	{
		Context.SetDeclaredHitImpact(Facts.Impact);
		Context.SetLaunchHeight(Facts.Height);
		Context.SetPushYaw(Facts.bYaw ? Facts.Yaw : TNumericLimits<float>::Lowest());
	}

	FString Describe(const FFacts& Facts)
	{
		return FString::Printf(TEXT("%s h=%.2f yaw=%s"), VBHitImpactName(Facts.Impact), Facts.Height,
			Facts.bYaw ? *FString::Printf(TEXT("%.2f"), Facts.Yaw) : TEXT("none"));
	}

	void ExpectFacts(FAutomationTestBase& Test, const FString& Case, const FVBGameplayEffectContext& Got, const FFacts& Want)
	{
		Test.TestEqual(Case + TEXT(": impact"), ImpactOrd(Got.GetDeclaredHitImpact()), ImpactOrd(Want.Impact));
		Test.TestEqual(Case + TEXT(": launch height"), Got.GetLaunchHeight(), Want.Height);
		float Yaw = -1.0f;
		const bool bYaw = Got.GetPushYaw(Yaw);
		Test.TestEqual(Case + TEXT(": yaw present"), bYaw, Want.bYaw);
		Test.TestEqual(Case + TEXT(": yaw"), Yaw, Want.bYaw ? Want.Yaw : 0.0f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEffectContextAllocationTest, "VB.Unit.Damage.EffectContext.Allocation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEffectContextAllocationTest::RunTest(const FString& Parameters)
{
	// 엔진이 DefaultGame.ini 로 만든 이 프로세스의 실제 globals 를 쓴다 - 시험이 바꿔 끼우지 않는다.
	UAbilitySystemGlobals& Globals = UAbilitySystemGlobals::Get();
	TestTrue(FString::Printf(TEXT("globals class is UVBAbilitySystemGlobals (got %s)"), *Globals.GetClass()->GetName()),
		Globals.IsA<UVBAbilitySystemGlobals>());
	const FGameplayEffectContextHandle Handle(Globals.AllocGameplayEffectContext());
	TestNotNull(TEXT("a new context is FVBGameplayEffectContext"), FVBGameplayEffectContext::Get(Handle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEffectContextDeathMotionTest, "VB.Unit.Damage.EffectContext.DeathMotion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEffectContextDeathMotionTest::RunTest(const FString& Parameters)
{
	FVBDeathMotion Motion;
	Motion.InitialVelocity = FVector(0.0, 0.0, -2200.0);
	Motion.MaxAirSeconds = 2.0f;
	Motion.GroundImpactCue = VBGameplayTags::GameplayCue_Combat_AirSlam;
	FVBGameplayEffectContext Source;
	Source.SetDeathMotion(Motion);
	TUniquePtr<FVBGameplayEffectContext> Copy(Source.Duplicate());
	TestTrue(TEXT("server duplication retains lethal motion"), Copy->GetDeathMotion().IsValid()
		&& Copy->GetDeathMotion().InitialVelocity.Equals(Motion.InitialVelocity)
		&& Copy->GetDeathMotion().GroundImpactCue == Motion.GroundImpactCue);
	FNetBitWriter Writer(nullptr, 1024);
	bool bWriteOk = false;
	Source.NetSerialize(Writer, nullptr, bWriteOk);
	FVBGameplayEffectContext Reused;
	Reused.SetDeathMotion(Motion);
	FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
	bool bReadOk = false;
	Reused.NetSerialize(Reader, nullptr, bReadOk);
	TestTrue(TEXT("context transport succeeds"), bWriteOk && bReadOk && !Reader.IsError() && !Writer.IsError());
	TestFalse(TEXT("server death intent does not leak into reused remote context"), Reused.GetDeathMotion().IsValid());
	TestFalse(TEXT("empty death intent is absent"), FVBDeathMotion().IsValid());
	Motion.InitialVelocity.Z = 100.0;
	TestFalse(TEXT("upward terminal motion is rejected"), Motion.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEffectContextRoundTripTest, "VB.Unit.Damage.EffectContext.RoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEffectContextRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace VBGameplayEffectContextTest;
	const FVector Origin(100.0, -200.0, 300.0);
	for (const EVBHitImpact Impact : {EVBHitImpact::Light, EVBHitImpact::Heavy, EVBHitImpact::Knockdown, EVBHitImpact::Launch})
	{
		// 높이 0 만으로는 빠진 height 필드를 못 알아챈다 - 173.25 가 돌아와야 한다.
		for (const float Height : {0.0f, 173.25f})
		{
			for (const TPair<bool, float>& YawCase : TArray<TPair<bool, float>>{{false, 0.0f}, {true, 0.0f}, {true, -123.5f}, {true, 179.75f}})
			{
				const FFacts Want{Impact, Height, YawCase.Key, YawCase.Value};
				const FString Case = Describe(Want);

				FVBGameplayEffectContext Written;
				Written.AddOrigin(Origin);
				WriteFacts(Written, Want);
				FNetBitWriter Writer(nullptr, 1024);
				bool bWriteOk = false;
				Written.NetSerialize(Writer, nullptr, bWriteOk);

				// 재사용되는 replicated struct 처럼 이미 다른 값을 든 객체에 읽어 들인다.
				FVBGameplayEffectContext Read;
				WriteFacts(Read, FFacts{EVBHitImpact::Heavy, 55.0f, true, 33.0f});
				FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
				bool bReadOk = false;
				Read.NetSerialize(Reader, nullptr, bReadOk);

				TestTrue(Case + TEXT(": write ok"), bWriteOk && !Writer.IsError());
				TestTrue(Case + TEXT(": read ok"), bReadOk && !Reader.IsError());
				TestEqual(Case + TEXT(": bits read = bits written"), Reader.GetPosBits(), Writer.GetNumBits());
				ExpectFacts(*this, Case, Read, Want);
				// base context 가 먼저 간다 - origin 이 돌아오면 base NetSerialize 가 돈 것이다.
				TestEqual(Case + TEXT(": base origin"), Read.GetOrigin(), Origin);
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEffectContextHandleRoundTripTest, "VB.Unit.Damage.EffectContext.HandleRoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEffectContextHandleRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace VBGameplayEffectContextTest;
	const FFacts Want{EVBHitImpact::Launch, 173.25f, true, -123.5f};
	FVBGameplayEffectContext* const Source = new FVBGameplayEffectContext();
	WriteFacts(*Source, Want);
	FGameplayEffectContextHandle Written(Source);
	FNetBitWriter Writer(nullptr, 1024);
	bool bWriteOk = false;
	Written.NetSerialize(Writer, nullptr, bWriteOk);

	// 빈 수신 handle 은 보낸 쪽이 적은 struct 로 파생 타입을 되살려야 한다.
	FGameplayEffectContextHandle Read;
	FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
	bool bReadOk = false;
	Read.NetSerialize(Reader, nullptr, bReadOk);

	TestTrue(TEXT("handle write ok"), bWriteOk && !Writer.IsError());
	TestTrue(TEXT("handle read ok"), bReadOk && !Reader.IsError());
	const FVBGameplayEffectContext* const Got = FVBGameplayEffectContext::Get(Read);
	if (TestNotNull(TEXT("the read handle holds FVBGameplayEffectContext"), Got))
	{
		ExpectFacts(*this, TEXT("handle"), *Got, Want);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEffectContextDuplicateTest, "VB.Unit.Damage.EffectContext.Duplicate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEffectContextDuplicateTest::RunTest(const FString& Parameters)
{
	using namespace VBGameplayEffectContextTest;
	const FFacts Want{EVBHitImpact::Knockdown, 42.0f, true, 90.0f};
	FHitResult Hit;
	Hit.ImpactPoint = FVector(10.0, 20.0, 30.0);
	Hit.ImpactNormal = FVector(0.0, 0.0, 1.0);
	Hit.BoneName = TEXT("spine_03");

	FGameplayEffectContextHandle Original(new FVBGameplayEffectContext());
	FVBGameplayEffectContext* const OriginalFacts = FVBGameplayEffectContext::Get(Original);
	WriteFacts(*OriginalFacts, Want);
	Original.AddHitResult(Hit);

	FGameplayEffectContextHandle Copy = Original.Duplicate();
	FVBGameplayEffectContext* const CopyFacts = FVBGameplayEffectContext::Get(Copy);
	if (!TestNotNull(TEXT("the duplicate is FVBGameplayEffectContext"), CopyFacts))
	{
		return false;
	}

	ExpectFacts(*this, TEXT("duplicate"), *CopyFacts, Want);
	TestTrue(TEXT("duplicate hit result exists"), CopyFacts->GetHitResult() != nullptr);
	TestTrue(TEXT("duplicate hit result is its own copy"), CopyFacts->GetHitResult() != OriginalFacts->GetHitResult());
	if (CopyFacts->GetHitResult())
	{
		TestEqual(TEXT("duplicate impact point"), CopyFacts->GetHitResult()->ImpactPoint, Hit.ImpactPoint);
		TestEqual(TEXT("duplicate bone"), CopyFacts->GetHitResult()->BoneName, Hit.BoneName);
	}

	// 복제본을 바꿔도 원본은 그대로다.
	CopyFacts->SetLaunchHeight(7.0f);
	CopyFacts->SetPushYaw(TNumericLimits<float>::Lowest());
	CopyFacts->AddHitResult(FHitResult(), /*bReset=*/true);
	ExpectFacts(*this, TEXT("original after the duplicate changed"), *OriginalFacts, Want);
	TestEqual(TEXT("original impact point after the duplicate changed"), OriginalFacts->GetHitResult()->ImpactPoint, Hit.ImpactPoint);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEffectContextDefaultsTest, "VB.Unit.Damage.EffectContext.DefaultsAndTypes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEffectContextDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace VBGameplayEffectContextTest;
	// 아무도 사실을 쓰지 않은 context 는 SetByCaller 미설정과 같게 읽힌다: Light, 높이 0, 방향 없음.
	ExpectFacts(*this, TEXT("fresh"), FVBGameplayEffectContext(), FFacts{});

	const FGameplayEffectContextHandle Empty;
	TestNull(TEXT("empty handle gives null"), FVBGameplayEffectContext::Get(Empty));
	const FGameplayEffectContextHandle Base(new FGameplayEffectContext());
	TestNull(TEXT("engine base context gives null"), FVBGameplayEffectContext::Get(Base));

	FVBGameplayEffectContext Context;
	Context.SetDeclaredHitImpact(EVBHitImpact::None);
	TestEqual(TEXT("declared None reads Light"), ImpactOrd(Context.GetDeclaredHitImpact()), ImpactOrd(EVBHitImpact::Light));

	float Yaw = -1.0f;
	Context.SetPushYaw(std::numeric_limits<float>::quiet_NaN());
	TestFalse(TEXT("NaN yaw is no direction"), Context.GetPushYaw(Yaw));
	Context.SetPushYaw(std::numeric_limits<float>::infinity());
	TestFalse(TEXT("infinite yaw is no direction"), Context.GetPushYaw(Yaw));
	Context.SetPushYaw(TNumericLimits<float>::Lowest());
	TestFalse(TEXT("old «not set» marker is no direction"), Context.GetPushYaw(Yaw));
	Context.SetPushYaw(0.0f);
	TestTrue(TEXT("0 degrees is a direction"), Context.GetPushYaw(Yaw));
	TestEqual(TEXT("0 degrees value"), Yaw, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEffectContextStrikingShapeTest, "VB.Unit.Damage.EffectContext.StrikingShape",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEffectContextStrikingShapeTest::RunTest(const FString& Parameters)
{
	// 클라 impact cue 가 이것으로 punch, kick, slam 비주얼을 고른다 - 전송을 건너야 하고, 빈 값은 빈 채로 남아야 한다.
	for (const FGameplayTag& Want : {VBGameplayTags::Melee_Shape_HandR.GetTag(), VBGameplayTags::Melee_Shape_Slam.GetTag(), FGameplayTag()})
	{
		FVBGameplayEffectContext Written;
		Written.SetStrikingShape(Want);
		FNetBitWriter Writer(nullptr, 1024);
		bool bWriteOk = false;
		Written.NetSerialize(Writer, nullptr, bWriteOk);

		FVBGameplayEffectContext Read;
		Read.SetStrikingShape(VBGameplayTags::Melee_Shape_FootL);
		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		bool bReadOk = false;
		Read.NetSerialize(Reader, nullptr, bReadOk);

		const FString Case = Want.IsValid() ? Want.ToString() : TEXT("empty");
		TestTrue(Case + TEXT(": write and read ok"), bWriteOk && bReadOk && !Writer.IsError() && !Reader.IsError());
		TestEqual(Case + TEXT(": bits read = bits written"), Reader.GetPosBits(), Writer.GetNumBits());
		TestEqual(Case + TEXT(": shape came back"), Read.GetStrikingShape(), Want);
	}

	return true;
}
