#include "Misc/AutomationTest.h"
#include "Data/VBEnemyConfig.h"
#include "Data/VBHitReactConfig.h"
#include "Animation/AnimMontage.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLaunchGetUpPickTest, "VB.Unit.Enemy.Knockdown.LaunchGetUp",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBLaunchGetUpPickTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UAnimMontage> Normal(NewObject<UAnimMontage>());
	TStrongObjectPtr<UAnimMontage> Quick(NewObject<UAnimMontage>());
	FVBKnockdownSet Set;
	Set.GetUpMontage = Normal.Get();
	Set.DownHoldSeconds = 4.5f;
	Set.LaunchDownHoldSeconds = 0.3f;

	TestTrue(TEXT("unset quick get-up keeps the normal get-up after a launch"), Set.OwnClips().GetUpFor(true) == Normal.Get());
	TestEqual(TEXT("unset quick get-up keeps the normal hold after a launch"), Set.HoldSecondsFor(Set.OwnClips(), true), 4.5f);

	Set.LaunchGetUpMontage = Quick.Get();
	TestTrue(TEXT("launch landing uses the quick get-up"), Set.OwnClips().GetUpFor(true) == Quick.Get());
	TestEqual(TEXT("launch landing uses the short hold"), Set.HoldSecondsFor(Set.OwnClips(), true), 0.3f);
	TestTrue(TEXT("an ordinary fall keeps the normal get-up"), Set.OwnClips().GetUpFor(false) == Normal.Get());
	TestEqual(TEXT("an ordinary fall keeps the normal hold"), Set.HoldSecondsFor(Set.OwnClips(), false), 4.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDownedEscapeTest, "VB.Unit.Enemy.Knockdown.Escape",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBDownedEscapeTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UAnimMontage> KipUp(NewObject<UAnimMontage>());
	FVBKnockdownSet Set;
	Set.MinHoldSeconds = 0.4f;
	TestFalse(TEXT("no escape montage: never escapes"), Set.CanEscape(true, 5.0));
	Set.EscapeGetUpMontage = KipUp.Get();
	TestFalse(TEXT("too early in the hold"), Set.CanEscape(true, 0.39));
	TestTrue(TEXT("after the minimum hold"), Set.CanEscape(true, 0.4));
	TestFalse(TEXT("not while falling or getting up"), Set.CanEscape(false, 5.0));
	return true;
}

// 코드 상수에서 옮긴 값은 기본값이 그대로여야 하고, 0 이하 주기는 타이머가 안 걸리므로 저장 때 잡아야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBKnockdownWatchTunablesTest, "VB.Unit.Enemy.Knockdown.WatchTunables",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBKnockdownWatchTunablesTest::RunTest(const FString& Parameters)
{
	FVBKnockdownSet Set;
	TestEqual(TEXT("counter watch default"), Set.DownAttackWatchSeconds, 0.2f);
	TestEqual(TEXT("air watch default"), Set.AirWatchSeconds, 0.05f);
	TestEqual(TEXT("takeoff grace default"), Set.AirTakeoffGraceSeconds, 0.25f);
	TestEqual(TEXT("redirect limit default"), GetDefault<UVBHitReactConfig>()->MaxRedirectDegrees, 90.0f);

	TArray<FText> Problems;
	Set.FindProblems(Problems);
	TestEqual(TEXT("defaults are clean"), Problems.Num(), 0);
	Set.AirWatchSeconds = 0.0f;
	Set.FindProblems(Problems);
	TestEqual(TEXT("a dead watch period is reported"), Problems.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBKnockdownClipPoolTest, "VB.Unit.Enemy.Knockdown.ClipPool",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBKnockdownClipPoolTest::RunTest(const FString& Parameters)
{
	auto Montage = [](){ return NewObject<UAnimMontage>(GetTransientPackage()); };
	TStrongObjectPtr<UAnimMontage> Keep[16];
	int32 Next = 0;
	auto Make = [&](){ check(Next < UE_ARRAY_COUNT(Keep)); Keep[Next].Reset(Montage()); return Keep[Next++].Get(); };

	FVBKnockdownSet Set;
	Set.FallMontage = Make();
	Set.GetUpMontage = Make();
	TestEqual(TEXT("own clips follow the same rule: no lying loop, no ground path"), Set.ClipsFor(false).Num(), 0);
	Set.DownIdleMontage = Make();
	TestEqual(TEXT("no variants: own clips only on the ground path"), Set.ClipsFor(false).Num(), 1);
	TestEqual(TEXT("own clips without launch clips: no launch path"), Set.ClipsFor(true).Num(), 0);

	FVBKnockdownClips Frank; // 땅만: 넘어짐, 대기, 기상
	Frank.Fall = Make(); Frank.DownIdle = Make(); Frank.GetUp = Make();
	FVBKnockdownClips NoIdle; // 대기 없는 묶음은 어느 길도 못 덮는다
	NoIdle.Fall = Make(); NoIdle.GetUp = Make();
	FVBKnockdownClips Air; // 띄움만
	Air.Launch = Make(); Air.AirLoop = Make(); Air.Land = Make(); Air.DownIdle = Make(); Air.GetUp = Make();
	Set.ClipVariants = {Frank, NoIdle, Air};

	const TArray<FVBKnockdownClips> Ground = Set.ClipsFor(false);
	TestEqual(TEXT("ground choices = own + the full ground variant"), Ground.Num(), 2);
	TestTrue(TEXT("a variant without a lying loop is not a ground choice"), !Ground.ContainsByPredicate([&](const FVBKnockdownClips& C){ return C.Fall == NoIdle.Fall; }));
	const TArray<FVBKnockdownClips> Launch = Set.ClipsFor(true);
	TestTrue(TEXT("launch choices = the launch variant only"), Launch.Num() == 1 && Launch[0].Launch == Air.Launch);
	TestTrue(TEXT("the set now has a launch path through a variant"), Set.IsLaunchAuthored());
	TestTrue(TEXT("a picked variant owns its clips"), Air.Contains(Air.Land) && !Air.Contains(Frank.Fall));

	// 반쯤 채운 묶음은 고를 때 빠지므로 저장 검사가 잡는다.
	TArray<FText> Problems;
	Set.FindProblems(Problems);
	TestEqual(TEXT("the half bundle (no lying loop) is one problem"), Problems.Num(), 1);
	Set.ClipVariants.Add(FVBKnockdownClips());
	Problems.Reset();
	Set.FindProblems(Problems);
	TestEqual(TEXT("an empty bundle is a problem too"), Problems.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBKnockdownSetValidationTest, "VB.Unit.Enemy.Knockdown.Validation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBKnockdownSetValidationTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UAnimMontage> Keep[6];
	for (TStrongObjectPtr<UAnimMontage>& M : Keep)
	{
		M.Reset(NewObject<UAnimMontage>(GetTransientPackage()));
	}

	FVBKnockdownSet Set;
	TArray<FText> Problems;
	Set.FindProblems(Problems);
	TestEqual(TEXT("an untouched set has no problems"), Problems.Num(), 0);

	Set.EscapeGetUpMontage = Keep[0].Get();
	Problems.Reset(); Set.FindProblems(Problems);
	TestEqual(TEXT("escape get-up without any ground path"), Problems.Num(), 1);
	Set.FallMontage = Keep[1].Get(); Set.DownIdleMontage = Keep[2].Get(); Set.GetUpMontage = Keep[3].Get();
	Problems.Reset(); Set.FindProblems(Problems);
	TestEqual(TEXT("a full ground path clears it"), Problems.Num(), 0);

	Set.LaunchMontage = Keep[4].Get();
	Problems.Reset(); Set.FindProblems(Problems);
	TestEqual(TEXT("launch clips only in part"), Problems.Num(), 1);
	Set.LaunchMontage = nullptr;

	// 띄움 묶음만 있고 땅 묶음이 없으면 넘어짐 데이터로 인정되지 않는다 - 크게 알린다.
	FVBKnockdownSet LaunchOnly;
	FVBKnockdownClips Air;
	Air.Launch = Keep[0].Get(); Air.AirLoop = Keep[1].Get(); Air.Land = Keep[2].Get(); Air.DownIdle = Keep[3].Get(); Air.GetUp = Keep[4].Get();
	LaunchOnly.ClipVariants = {Air};
	Problems.Reset(); LaunchOnly.FindProblems(Problems);
	TestEqual(TEXT("launch without ground is one problem"), Problems.Num(), 1);
	TestFalse(TEXT("launch without ground is not a knockdown"), LaunchOnly.IsAuthored());

	Set.MaxAirSeconds = 2.5f;
	Set.AirHitExtendSeconds = 0.4f;
	Set.MaxAirTotalSeconds = 0.0f;
	Problems.Reset(); Set.FindProblems(Problems);
	TestEqual(TEXT("air extension capped by MaxAirSeconds does nothing"), Problems.Num(), 1);
	Set.MaxAirTotalSeconds = 4.0f;
	Problems.Reset(); Set.FindProblems(Problems);
	TestEqual(TEXT("a total cap above MaxAirSeconds is fine"), Problems.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDownedDeathPickTest, "VB.Unit.Enemy.DownedDeathPick",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBDownedDeathPickTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UVBEnemyConfig> Config(NewObject<UVBEnemyConfig>());
	UAnimMontage* Standing = NewObject<UAnimMontage>(Config.Get());
	UAnimMontage* Lying = NewObject<UAnimMontage>(Config.Get());

	TestNull(TEXT("no montages: standing death has none"), Config->PickDeathMontage(false));
	Config->DeathMontages.Add(Standing);
	TestNull(TEXT("unset downed death keeps the prone ragdoll"), Config->PickDeathMontage(true));
	TestTrue(TEXT("standing death picks from DeathMontages"), Config->PickDeathMontage(false) == Standing);

	Config->DownedDeathMontage = Lying;
	TestTrue(TEXT("downed death plays the lying montage"), Config->PickDeathMontage(true) == Lying);
	TestTrue(TEXT("standing death never takes the lying montage"), Config->PickDeathMontage(false) == Standing);
	return true;
}
#endif
