// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/ProgressBar.h"
#include "GameplayEffect.h"
#include "Misc/ScopeExit.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/VBStatBonus.h"
#include "Player/VBPlayerState.h"
#include "UI/VBHealthBarWidget.h"
#include "Unit/VBUITestFixture.h"

// 상한이 무한 GE(장비, 스킬 트리, 강화)로 바뀔 때 체력이 비율을 지키는가.
//   원인: 비율 지키기가 PostGameplayEffectExecute(순간 GE 만)에 있어 무한 GE 의 상한 변화는 체력을 그대로 두었다. 지금은 PostAttributeChange.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMaxHealthChangeTest, "VB.Unit.Attributes.MaxHealthKeepsRatio",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMaxHealthChangeTest::RunTest(const FString& Parameters)
{
	UClass* StatEffect = LoadClass<UGameplayEffect>(nullptr, TEXT("/Game/VB/GAS/Effects/GE_StatBonus.GE_StatBonus_C"));
	if (!TestNotNull(TEXT("GE_StatBonus (the infinite stat effect gear uses)"), StatEffect))
	{
		return false;
	}

	const FGameplayTag MaxHealthTag = FGameplayTag::RequestGameplayTag(TEXT("Data.Stat.MaxHealth"));
	const FGameplayTag MaxShieldTag = FGameplayTag::RequestGameplayTag(TEXT("Data.Stat.MaxShield"));

	VBUITest::FRowFixture Fixture;
	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AVBPlayerState* State = Fixture.SpawnPlayerState(*this);
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(State);
	if (!TestNotNull(TEXT("player ASC"), ASC))
	{
		return false;
	}

	auto Health = [ASC] { return ASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()); };
	auto MaxHealth = [ASC] { return ASC->GetNumericAttribute(UVBHealthAttributeSet::GetMaxHealthAttribute()); };
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), 100.0f);
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxShieldAttribute(), 20.0f);
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetShieldAttribute(), 10.0f);

	// 실제 플레이어 바도 같이 본다 - 가득 찬 채 입으면 바가 덜 차 보이던 증상.
	UVBHealthBarWidget* Bar = Fixture.MakeRow<UVBHealthBarWidget>(*this, TEXT("/Game/VB/UI/WBP_HealthBar.WBP_HealthBar_C"));
	VBUITest::FSlateLife Life;
	if (Bar)
	{
		Life.Construct(*Bar);
		Bar->InitializeWithASC(ASC);
	}

	const UProgressBar* HealthProgress = Bar ? Cast<UProgressBar>(Bar->GetWidgetFromName(TEXT("HealthBar"))) : nullptr;

	// 가득 찬 채 상한 +10 -> 여전히 가득.
	const FActiveGameplayEffectHandle Helm = VBStatBonus::Apply(ASC, StatEffect, {{MaxHealthTag, 10.0f}}, TEXT("test helm"));
	if (!TestTrue(TEXT("infinite stat effect applied"), Helm.IsValid()))
	{
		return false;
	}

	TestEqual(TEXT("cap rises to 110"), MaxHealth(), 110.0f);
	TestEqual(TEXT("full health stays full (110)"), Health(), 110.0f);
	if (TestNotNull(TEXT("player bar progress"), HealthProgress))
	{
		TestEqual(TEXT("player bar is full after equipping at full health"), HealthProgress->GetPercent(), 1.0f, 0.001f);
	}

	// 반쯤 다친 채 벗으면 -> 비율 그대로, 상한 위로 남지 않는다.
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 55.0f);
	ASC->RemoveActiveGameplayEffect(Helm);
	TestEqual(TEXT("cap back to 100"), MaxHealth(), 100.0f);
	TestEqual(TEXT("half health stays half (50)"), Health(), 50.0f);

	// 가득 찬 채 벗어도 상한 위로 남지 않는다(종전: 110 이 상한 100 위에 남았다).
	const FActiveGameplayEffectHandle Again = VBStatBonus::Apply(ASC, StatEffect, {{MaxHealthTag, 10.0f}}, TEXT("test helm"));
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 110.0f);
	ASC->RemoveActiveGameplayEffect(Again);
	TestEqual(TEXT("full health after removing is the new cap, not above it"), Health(), 100.0f);

	// 보호막도 같은 규칙.
	const FActiveGameplayEffectHandle Ring = VBStatBonus::Apply(ASC, StatEffect, {{MaxShieldTag, 20.0f}}, TEXT("test ring"));
	TestEqual(TEXT("shield cap 40"), ASC->GetNumericAttribute(UVBHealthAttributeSet::GetMaxShieldAttribute()), 40.0f);
	TestEqual(TEXT("half shield stays half (20)"), ASC->GetNumericAttribute(UVBHealthAttributeSet::GetShieldAttribute()), 20.0f);
	ASC->RemoveActiveGameplayEffect(Ring);

	if (Bar)
	{
		Life.Destruct(*Bar);
	}

	return true;
}

#endif
