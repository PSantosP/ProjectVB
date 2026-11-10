// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/Executions/VBDamageExecution.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameplayEffect.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 코드와 다섯 damage GE 자산(각각 UVBDamageExecution 경유)은 짝으로만 동작한다 - execution 이 없으면 배율이 조용히 빠지고,
//   옛 IncomingDamage modifier 가 남으면 두 번 맞고, BaseDamage modifier 가 없으면 안 맞는다. 정의를 읽고 실제로 적용도 해본다.
namespace VBDamageEffectAssetsTest
{
	struct FDamageEffect
	{
		const TCHAR* Path;
		bool bSetByCaller; // 호출자가 Effect.Damage.Physical 을 주는지, 자산의 고정값인지
		float Fixed; // 이전에 자산에서 잰 고정값, SetByCaller 면 안 쓴다
		bool bFinisherTag; // 처형 피해 - Effect.Execute 를 단다
	};

	// 옮기기 전에 자산에서 잰 값 - 옮긴 뒤에도 같아야 한다.
	const FDamageEffect Effects[] = {
		{ TEXT("/Game/VB/GAS/Effects/Damage/GE_PhysicalDamage.GE_PhysicalDamage_C"), true, 0.0f, false },
		{ TEXT("/Game/VB/GAS/Effects/Damage/GE_Damage_EnemyBasic.GE_Damage_EnemyBasic_C"), false, 15.0f, false },
		{ TEXT("/Game/VB/GAS/Effects/Bleed/GE_Bleed_Burst.GE_Bleed_Burst_C"), false, 40.0f, false },
		{ TEXT("/Game/VB/GAS/Effects/Execute/GE_ExecuteDamage.GE_ExecuteDamage_C"), false, 99999.0f, true },
		{ TEXT("/Game/VB/GAS/Effects/Purify/GE_PurifyDamage.GE_PurifyDamage_C"), false, 999999.0f, true },
	};

	// DefinitionProblems - damage GE 정의의 잘못을 모두 돌려준다. 비면 코드와의 짝이 맞는다.
	TArray<FString> DefinitionProblems(const UGameplayEffect& Def, const FDamageEffect& Effect)
	{
		TArray<FString> Problems;
		for (const FGameplayModifierInfo& Mod : Def.Modifiers)
		{
			if (Mod.Attribute == UVBHealthAttributeSet::GetIncomingDamageAttribute())
			{
				Problems.Add(TEXT("direct IncomingDamage modifier left (damage twice)"));
			}

			if (Mod.Attribute == UVBCombatAttributeSet::GetIncomingPoiseDamageAttribute())
			{
				Problems.Add(TEXT("direct IncomingPoiseDamage modifier left (stagger before health)"));
			}
		}

		if (Def.Executions.Num() != 1)
		{
			Problems.Add(FString::Printf(TEXT("%d executions, want 1"), Def.Executions.Num()));
			return Problems;
		}

		const FGameplayEffectExecutionDefinition& Exec = Def.Executions[0];
		if (Exec.CalculationClass != UVBDamageExecution::StaticClass())
		{
			Problems.Add(TEXT("execution is not UVBDamageExecution"));
		}

		if (Exec.CalculationModifiers.Num() != 1)
		{
			Problems.Add(FString::Printf(TEXT("%d calculation modifiers, want 1 (BaseDamage)"), Exec.CalculationModifiers.Num()));
			return Problems;
		}

		const FGameplayEffectExecutionScopedModifierInfo& BaseMod = Exec.CalculationModifiers[0];
		if (!(BaseMod.CapturedAttribute.AttributeToCapture == UVBCombatAttributeSet::GetBaseDamageAttribute()
			&& BaseMod.CapturedAttribute.AttributeSource == EGameplayEffectAttributeCaptureSource::Source && BaseMod.CapturedAttribute.bSnapshot))
		{
			Problems.Add(TEXT("calculation modifier is not on the attacker's BaseDamage snapshot"));
		}

		if (BaseMod.ModifierOp != EGameplayModOp::Additive)
		{
			Problems.Add(TEXT("calculation modifier is not additive"));
		}

		if (Effect.bSetByCaller)
		{
			if (BaseMod.ModifierMagnitude.GetMagnitudeCalculationType() != EGameplayEffectMagnitudeCalculation::SetByCaller
				|| BaseMod.ModifierMagnitude.GetSetByCallerFloat().DataTag != VBGameplayTags::Effect_Damage_Physical)
			{
				Problems.Add(TEXT("magnitude is not SetByCaller Effect.Damage.Physical"));
			}
		}
		else
		{
			// level 1 과 5 를 본다 - 고정값이 curve 로 바뀌면 안 된다.
			for (const float Level : { 1.0f, 5.0f })
			{
				float Value = -1.0f;
				if (!BaseMod.ModifierMagnitude.GetStaticMagnitudeIfPossible(Level, Value) || !FMath::IsNearlyEqual(Value, Effect.Fixed))
				{
					Problems.Add(FString::Printf(TEXT("fixed value at level %.0f is %.1f, want %.0f"), Level, Value, Effect.Fixed));
				}
			}
		}

		return Problems;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDamageEffectAssetsTest, "VB.Unit.Damage.EffectAssets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDamageEffectAssetsTest::RunTest(const FString& Parameters)
{
	using namespace VBDamageEffectAssetsTest;

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
	World->InitializeActorsForPlay(FURL());

	auto MakeBody = [World]()
	{
		ACharacter* Body = World->SpawnActor<ACharacter>();
		UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Body);
		Body->AddInstanceComponent(ASC);
		ASC->RegisterComponent();
		ASC->InitAbilityActorInfo(Body, Body);
		ASC->AddAttributeSetSubobject(NewObject<UVBHealthAttributeSet>(Body));
		ASC->AddAttributeSetSubobject(NewObject<UVBCombatAttributeSet>(Body));
		return ASC;
	};
	UAbilitySystemComponent* Attacker = MakeBody();
	UAbilitySystemComponent* Victim = MakeBody();

	for (const FDamageEffect& Effect : Effects)
	{
		const FString Name = FPaths::GetBaseFilename(Effect.Path);
		const UClass* Class = LoadClass<UGameplayEffect>(nullptr, Effect.Path);
		if (!TestNotNull(*FString::Printf(TEXT("%s: class"), *Name), Class))
		{
			continue;
		}

		const UGameplayEffect* Def = Class->GetDefaultObject<UGameplayEffect>();

		// 1. 정의 - UVBDamageExecution 하나, 직접 damage/stagger modifier 없음, BaseDamage calculation modifier 하나.
		for (const FString& Problem : DefinitionProblems(*Def, Effect))
		{
			AddError(FString::Printf(TEXT("%s: %s"), *Name, *Problem));
		}
		// 검사기가 실패할 수 있어야 한다 - BaseDamage modifier 를 뺀 사본과 옛 IncomingDamage modifier 를 남긴 사본을 메모리에서 잡는다.
		if (Def->Executions.Num() == 1)
		{
			UGameplayEffect* NoBase = DuplicateObject<UGameplayEffect>(Def, GetTransientPackage());
			NoBase->Executions[0].CalculationModifiers.Reset();
			TestTrue(*FString::Printf(TEXT("%s: checker catches a missing BaseDamage modifier"), *Name),
				DefinitionProblems(*NoBase, Effect).Num() > 0);
			UGameplayEffect* Doubled = DuplicateObject<UGameplayEffect>(Def, GetTransientPackage());
			FGameplayModifierInfo Old;
			Old.Attribute = UVBHealthAttributeSet::GetIncomingDamageAttribute();
			Old.ModifierOp = EGameplayModOp::Additive;
			Old.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(10.0f));
			Doubled->Modifiers.Add(Old);
			TestTrue(*FString::Printf(TEXT("%s: checker catches a leftover direct IncomingDamage modifier"), *Name),
				DefinitionProblems(*Doubled, Effect).Num() > 0);
		}

		// 2. 적용 - 받는 쪽 배율 x1, x2 를 999999 타격에도 안 비는 체력에서 잰다.
		for (const float Scale : { 1.0f, 2.0f })
		{
			Victim->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), 1.0e7f);
			Victim->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 1.0e7f);
			Victim->SetNumericAttributeBase(UVBCombatAttributeSet::GetDamageTakenScaleAttribute(), Scale);
			FGameplayEffectSpec Spec(Def, Attacker->MakeEffectContext(), 1.0f);
			const float Base = Effect.bSetByCaller ? 10.0f : Effect.Fixed;
			if (Effect.bSetByCaller)
			{
				Spec.SetSetByCallerMagnitude(VBGameplayTags::Effect_Damage_Physical, Base);
			}

			if (Effect.bFinisherTag)
			{
				Spec.AddDynamicAssetTag(VBGameplayTags::Effect_Execute);
			}

			Victim->ApplyGameplayEffectSpecToSelf(Spec);
			const float Lost = 1.0e7f - Victim->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute());
			TestEqual(*FString::Printf(TEXT("%s: x%.0f receiver scale"), *Name, Scale), Lost, Base * Scale,
				FMath::Max(0.01f, Base * Scale * 1.0e-6f));
		}

		// 3. 근접 GE 는 stagger 피해도 싣는다 - execution 이 체력 다음에 배율 없이 낸다.
		if (Effect.bSetByCaller)
		{
			Victim->SetNumericAttributeBase(UVBCombatAttributeSet::GetMaxPoiseAttribute(), 100.0f);
			Victim->SetNumericAttributeBase(UVBCombatAttributeSet::GetPoiseAttribute(), 100.0f);
			Victim->SetNumericAttributeBase(UVBCombatAttributeSet::GetDamageTakenScaleAttribute(), 2.0f);
			FGameplayEffectSpec Spec(Def, Attacker->MakeEffectContext(), 1.0f);
			Spec.SetSetByCallerMagnitude(VBGameplayTags::Effect_Damage_Physical, 10.0f);
			Spec.SetSetByCallerMagnitude(VBGameplayTags::Data_PoiseDamage, 7.0f);
			Victim->ApplyGameplayEffectSpecToSelf(Spec);
			TestEqual(*FString::Printf(TEXT("%s: stagger damage applied, not scaled"), *Name),
				Victim->GetNumericAttribute(UVBCombatAttributeSet::GetPoiseAttribute()), 93.0f, 0.01f);
			Victim->SetNumericAttributeBase(UVBCombatAttributeSet::GetDamageTakenScaleAttribute(), 1.0f);
		}
	}

	return true;
}
#endif
