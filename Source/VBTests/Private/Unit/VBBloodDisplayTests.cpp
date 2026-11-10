#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/GameplayCues/VBGC_MeleeImpact.h"
#include "NiagaraSystem.h"
#include "Types/VBNiagaraParameters.h"

// 피 표현 옵션의 큐 쪽 규칙 - 모든 피가 UVBGC_MeleeImpact 하나를 지나므로 이 두 함수가 틀리면 옵션 전체가 틀린다.
// 순수 계산이라 ApplicationContextMask - headless(-game) 실행에서도 발견된다.
namespace
{
	const FName Spray(TEXT("User.Spray Color"));
	const FName Streak(TEXT("User.Streak Color"));
	const FName Wound(TEXT("User.Wound Color"));

	TMap<FName, FLinearColor> CueColors()
	{
		// 실제 GCN_DamagePhysical 처럼 큐가 빛줄기 색만 덮어 둔 상태
		return { { Streak, FLinearColor(1.0f, 0.07f, 0.04f, 1.0f) } };
	}

	TMap<FName, FLinearColor> Palette()
	{
		return { { Spray, FLinearColor(0.35f, 0.55f, 0.9f, 1.0f) },
			{ Streak, FLinearColor(1.4f, 1.7f, 2.2f, 1.0f) },
			{ Wound, FLinearColor(0.35f, 0.55f, 0.9f, 1.0f) } };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBBloodDisplayColorsTest, "VB.Unit.Combat.BloodDisplay.Colors",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBBloodDisplayColorsTest::RunTest(const FString& Parameters)
{
	const TMap<FName, FLinearColor> Normal = UVBGC_MeleeImpact::ResolveBloodColors(EVBBloodDisplay::Normal, CueColors(), Palette());
	TestEqual(TEXT("보통 - 큐 색만"), Normal.Num(), 1);
	TestTrue(TEXT("보통 - 큐의 빛줄기 색 유지"), Normal.FindRef(Streak).Equals(FLinearColor(1.0f, 0.07f, 0.04f, 1.0f)));

	const TMap<FName, FLinearColor> Alternate = UVBGC_MeleeImpact::ResolveBloodColors(EVBBloodDisplay::Alternate, CueColors(), Palette());
	TestEqual(TEXT("다른 색 - 팔레트 키가 전부 들어간다"), Alternate.Num(), 3);
	// 큐가 같은 이름을 덮어 둔 색보다 팔레트가 이겨야 붉은 빛줄기가 안 남는다
	TestTrue(TEXT("다른 색 - 큐의 빛줄기 색을 팔레트가 덮는다"), Alternate.FindRef(Streak).Equals(FLinearColor(1.4f, 1.7f, 2.2f, 1.0f)));
	TestTrue(TEXT("다른 색 - 상처 색"), Alternate.FindRef(Wound).Equals(FLinearColor(0.35f, 0.55f, 0.9f, 1.0f)));

	const TMap<FName, FLinearColor> Off = UVBGC_MeleeImpact::ResolveBloodColors(EVBBloodDisplay::Off, CueColors(), Palette());
	TestTrue(TEXT("끄기 - 남는 빛줄기도 대체 색"), Off.FindRef(Streak).Equals(FLinearColor(1.4f, 1.7f, 2.2f, 1.0f)));

	// 깨진 설정값(저장 파일 손상 등): 기본 표현으로 떨어진다
	const EVBBloodDisplay Broken = static_cast<EVBBloodDisplay>(200);
	const TMap<FName, FLinearColor> Fallback = UVBGC_MeleeImpact::ResolveBloodColors(Broken, CueColors(), Palette());
	TestTrue(TEXT("모르는 값 - 큐 색 그대로"), Fallback.FindRef(Streak).Equals(FLinearColor(1.0f, 0.07f, 0.04f, 1.0f)));
	TestEqual(TEXT("모르는 값 - 팔레트를 안 섞는다"), Fallback.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBBloodDisplayShowsBloodTest, "VB.Unit.Combat.BloodDisplay.ShowsBlood",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBBloodDisplayShowsBloodTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("보통 - 피를 낸다"), UVBGC_MeleeImpact::ShowsBlood(EVBBloodDisplay::Normal));
	TestTrue(TEXT("다른 색 - 피 모양은 낸다"), UVBGC_MeleeImpact::ShowsBlood(EVBBloodDisplay::Alternate));
	TestFalse(TEXT("끄기 - 피를 안 낸다"), UVBGC_MeleeImpact::ShowsBlood(EVBBloodDisplay::Off));
	TestTrue(TEXT("모르는 값 - 보통처럼 낸다"), UVBGC_MeleeImpact::ShowsBlood(static_cast<EVBBloodDisplay>(200)));
	return true;
}

// 피를 내는 이펙트 다섯이 끄기 스위치(User.Blood Spawn)를 들고 있나. 저작 스크립트로 시스템을 다시 지으면 스위치가 빠질 수 있고,
// 그때 큐는 끄기에서 이펙트째 건너뛴다(섬광도 사라진다) - 그 회귀를 여기서 잡는다. 없는 이름은 false 여야 검사가 살아 있다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBBloodDisplaySwitchAssetsTest, "VB.Unit.Combat.BloodDisplay.SwitchOnAssets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBBloodDisplaySwitchAssetsTest::RunTest(const FString& Parameters)
{
	const FName Switch = GetDefault<UVBGC_MeleeImpact>()->BloodSpawnParameter;
	for (const TCHAR* Path : { TEXT("/Game/VB/VFX/Combat/NS_VB_Impact_Flesh.NS_VB_Impact_Flesh"),
		TEXT("/Game/VB/VFX/Combat/NS_VB_Impact_Heavy.NS_VB_Impact_Heavy"),
		TEXT("/Game/VB/VFX/Combat/NS_VB_Impact_Blunt.NS_VB_Impact_Blunt"),
		TEXT("/Game/VB/VFX/Combat/NS_VB_Impact_BluntHeavy.NS_VB_Impact_BluntHeavy"),
		TEXT("/Game/VB/VFX/Combat/NS_VB_BleedBurst.NS_VB_BleedBurst") })
	{
		const UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, Path);
		if (!TestNotNull(FString::Printf(TEXT("%s 로드"), Path), System))
		{
			continue;
		}

		TestTrue(FString::Printf(TEXT("%s 에 끄기 스위치"), Path), VBNiagaraParameters::HasUserFloat(*System, Switch));
		TestFalse(FString::Printf(TEXT("%s 에 없는 이름은 없다고 답한다"), Path),
			VBNiagaraParameters::HasUserFloat(*System, TEXT("User.VB Not A Parameter")));
	}

	return true;
}
#endif
