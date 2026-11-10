// ProjectVB Copyright (c) 2025. All Rights Reserved.

// 타격 왜곡 데이터(UVBHitDistortionConfig)와 피격 플래시 컴포넌트의 왜곡 스폰.
// 음성 대조: ResolveProfile 이 늘 DefaultProfile -> Config.Tier 의 Heavy 줄, User 파라미터 검사를 빼기 -> Config.Validate,
//   접촉점 검사 전에 스폰 -> SpawnAtContact 의 "접촉 없음" 줄이 실패해야 한다.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/VBGameplayEffectContext.h"
#include "CharacterCommon/VBHitFlashComponent.h"
#include "Data/VBHitDistortionConfig.h"
#include "Data/VBHitFlashSettings.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Misc/ScopeExit.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "VBHitDistortionTestComponent.h"

namespace VBHitDistortionTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitDistortionTierTest, "VB.Unit.HitDistortion.Config.Tier", VBHitDistortionTests::Flags)
bool FVBHitDistortionTierTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UVBHitDistortionConfig> Config(NewObject<UVBHitDistortionConfig>());
	Config->DefaultProfile.Strength = 0.025f;
	FVBHitDistortionProfile Heavy;
	Heavy.Strength = 0.04f;
	Config->ByImpact.Add(EVBHitImpact::Heavy, Heavy);
	TestEqual(TEXT("a keyed tier gets its own profile"), Config->ResolveProfile(EVBHitImpact::Heavy).Strength, 0.04f);
	TestEqual(TEXT("a tier with no row gets the default"), Config->ResolveProfile(EVBHitImpact::Launch).Strength, 0.025f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitDistortionValidateTest, "VB.Unit.HitDistortion.Config.Validate", VBHitDistortionTests::Flags)
bool FVBHitDistortionValidateTest::RunTest(const FString& Parameters)
{
	FString Error;
	FVBHitDistortionProfile Off;
	TestTrue(TEXT("an empty profile is a switched-off tier"), UVBHitDistortionConfig::ValidateProfile(Off, Error));

	// gore burst 에는 왜곡의 User 파라미터가 없다 - 값이 말없이 버려질 것이다.
	UNiagaraSystem* Gore = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/VB/VFX/Combat/NS_VB_Impact_Flesh.NS_VB_Impact_Flesh"));
	if (!TestNotNull(TEXT("shipped gore system"), Gore))
	{
		return false;
	}

	FVBHitDistortionProfile Wrong;
	Wrong.System = Gore;
	TestFalse(TEXT("a system without the User parameters is refused"), UVBHitDistortionConfig::ValidateProfile(Wrong, Error));
	TestTrue(FString::Printf(TEXT("the error names the parameter (%s)"), *Error),
		Error.Contains(UVBHitDistortionConfig::StrengthParameter.ToString()));

	FVBHitDistortionProfile LatePeak = Wrong;
	LatePeak.PeakTime = 0.2f;
	LatePeak.Lifetime = 0.1f;
	TestFalse(TEXT("a peak after the end is refused"), UVBHitDistortionConfig::ValidateProfile(LatePeak, Error));
	TestTrue(FString::Printf(TEXT("the timing error comes first (%s)"), *Error), Error.Contains(TEXT("PeakTime")));
	return true;
}

// 왜곡은 플래시와 같은 접촉점에 하나 나고, 등급은 컨텍스트의 선언 등급을 따른다. 플래시 설정이 없어도 난다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitDistortionSpawnTest, "VB.Unit.HitDistortion.SpawnAtContact", VBHitDistortionTests::Flags)
bool FVBHitDistortionSpawnTest::RunTest(const FString& Parameters)
{
	UNiagaraSystem* LightSystem = LoadObject<UNiagaraSystem>(nullptr,
		TEXT("/Game/VB/VFX/Combat/NS_VB_Impact_Flesh.NS_VB_Impact_Flesh"));
	UNiagaraSystem* HeavySystem = LoadObject<UNiagaraSystem>(nullptr,
		TEXT("/Game/VB/VFX/Combat/NS_VB_ParryRipple.NS_VB_ParryRipple"));
	if (!TestNotNull(TEXT("light stand-in system"), LightSystem) || !TestNotNull(TEXT("heavy stand-in system"), HeavySystem))
	{
		return false;
	}

	const TStrongObjectPtr<UVBHitDistortionConfig> Config(NewObject<UVBHitDistortionConfig>());
	Config->DefaultProfile.System = LightSystem;
	FVBHitDistortionProfile Heavy;
	Heavy.System = HeavySystem;
	Config->ByImpact.Add(EVBHitImpact::Heavy, Heavy);
	UVBHitFlashSettings* Settings = GetMutableDefault<UVBHitFlashSettings>();
	const TSoftObjectPtr<UVBHitDistortionConfig> SavedConfig = Settings->DistortionConfig;
	Settings->DistortionConfig = Config.Get();

	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		Settings->DistortionConfig = SavedConfig;
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	ON_SCOPE_EXIT
	{
		Settings->DistortionConfig = SavedConfig;
		World->DestroyWorld(false);
		OwnedPackage->SetDirtyFlag(false);
	};
	World->InitializeActorsForPlay(FURL());
	ACharacter* Victim = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("victim"), Victim))
	{
		return false;
	}
	// Niagara 를 부르지 않고 스폰 요청만 기록한다 - nullrhi 프로세스는 component 를 만들지 않는다.
	UVBHitDistortionTestComponent* Flash = NewObject<UVBHitDistortionTestComponent>(Victim);
	Victim->AddInstanceComponent(Flash);
	Flash->RegisterComponent();

	FGameplayCueParameters NoContact;
	NoContact.EffectContext = FGameplayEffectContextHandle(new FVBGameplayEffectContext());
	Flash->TriggerFlash(NoContact);
	TestEqual(TEXT("no contact: no warp"), Flash->Requests.Num(), 0);

	const FVector Contact = Victim->GetActorLocation() + FVector(0.0, 0.0, 40.0);
	FHitResult Hit;
	Hit.bBlockingHit = true;
	Hit.ImpactPoint = Contact;
	Hit.Location = Contact;
	FVBGameplayEffectContext* HeavyContext = new FVBGameplayEffectContext();
	HeavyContext->SetDeclaredHitImpact(EVBHitImpact::Heavy);
	FGameplayCueParameters HeavyHit;
	HeavyHit.EffectContext = FGameplayEffectContextHandle(HeavyContext);
	HeavyHit.EffectContext.AddHitResult(Hit);
	Flash->TriggerFlash(HeavyHit);
	if (!TestEqual(TEXT("a contact spawns one warp"), Flash->Requests.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("the warp sits on the contact point"), Flash->Requests[0].Location.Equals(Contact, 1.0));
	TestTrue(TEXT("a heavy hit uses the heavy profile"), Flash->Requests[0].System == HeavySystem);
	return true;
}

#endif
