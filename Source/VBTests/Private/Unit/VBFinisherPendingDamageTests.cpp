#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameplayEffect.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 처형 창 중의 체력 피해. 창이 켜진 적도 일반 타격의 체력 피해와 반응을 받고, 처형 피해만 반응이 없다.
// 실제 피해 GE 를 허브(PostGameplayEffectExecute)에 넣어 잰다. 창을 켜는 타격의 반응 생략은 VB.Unit.Combat.PostureExecute 가 진짜 창 능력으로 잰다.
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherPendingDamageTest, "VB.Unit.Finisher.PendingDamage",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBFinisherPendingDamageTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
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

	ACharacter* Pawn = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("pawn"), Pawn))
	{
		return false;
	}

	UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Pawn);
	Pawn->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Pawn, Pawn);
	UVBHealthAttributeSet* Health = NewObject<UVBHealthAttributeSet>(Pawn);
	Health->InitMaxHealth(100.0f);
	Health->InitHealth(100.0f);
	ASC->AddAttributeSetSubobject(Health);

	// 일부러 손으로 만든 IncomingDamage modifier - UVBDamageExecution 을 건너뛰어 배율 없이 health set 만 본다.
	const TStrongObjectPtr<UGameplayEffect> Damage(NewObject<UGameplayEffect>(GetTransientPackage()));
	Damage->DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UVBHealthAttributeSet::GetIncomingDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(30.0f));
	Damage->Modifiers.Add(Modifier);

	// ActionTag 가 있으면 의료 행위 GA 처럼 스펙에 단다(UVBGA_MedicalAbilityBase::ApplyMedicalEffects 와 같은 자리).
	auto HitFromFull = [&](FGameplayTag ActionTag = FGameplayTag()) -> float
	{
		ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);
		FGameplayEffectSpec Spec(Damage.Get(), ASC->MakeEffectContext(), 1.0f);
		if (ActionTag.IsValid())
		{
			Spec.AddDynamicAssetTag(ActionTag);
		}

		ASC->ApplyGameplayEffectSpecToSelf(Spec);
		return ASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute());
	};

	TestEqual(TEXT("no finisher state: damage lands"), HitFromFull(), 70.0f);

	// 창이 켜져 있어도(누운 대기, 열림) 일반 피해가 들어간다 - 처형할 수 있는 적도 때리면 깎인다.
	int32 HitEvents = 0;
	const FDelegateHandle HitHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combat_Hit).AddLambda(
		[&HitEvents](const FGameplayEventData*) { ++HitEvents; });
	ON_SCOPE_EXIT { ASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combat_Hit).Remove(HitHandle); };

	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindowArmed);
	HitEvents = 0;
	TestEqual(TEXT("armed, window not open: normal damage lands"), HitFromFull(), 70.0f);
	TestEqual(TEXT("armed, window not open: normal damage reacts"), HitEvents, 1);

	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_Downed);
	TestEqual(TEXT("armed while lying down: normal damage lands"), HitFromFull(), 70.0f);
	ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Downed);

	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindow);
	TestEqual(TEXT("window open: normal damage lands"), HitFromFull(), 70.0f);
	TestEqual(TEXT("window open: execute damage lands"), HitFromFull(VBGameplayTags::Effect_Execute), 70.0f);
	TestEqual(TEXT("window open: purify damage lands"), HitFromFull(VBGameplayTags::Effect_Purify), 70.0f);
	// 의료 설정의 빈 행위 태그는 처형 피해로 안 읽힌다 - 설정 검증(UVBMedicalConfig::IsDataValid)이 막는 이유.
	TestFalse(TEXT("empty action tag is not finisher damage"), UVBAbilitySystemStatics::IsFinisherActionTag(FGameplayTag()));

	// 처형, 정화 피해는 피해만 들어가고 반응이 없다 - 처형 장면이 움직임을 정한다. 일반 피해가 대조군이다.
	HitEvents = 0;
	HitFromFull();
	TestEqual(TEXT("window open: normal damage reacts"), HitEvents, 1);
	HitFromFull(VBGameplayTags::Effect_Execute);
	HitFromFull(VBGameplayTags::Effect_Purify);
	TestEqual(TEXT("window open: execute and purify damage send no reaction"), HitEvents, 1);

	ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindow);
	ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindowArmed);
	TestEqual(TEXT("window closed again: normal damage lands"), HitFromFull(), 70.0f);
	return true;
}
#endif
