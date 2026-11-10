// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "GameplayEffect.h"
#include "AbilitySystem/Abilities/VBGA_Execute.h"
#include "AbilitySystem/Abilities/VBGA_FinisherWindow.h"
#include "AbilitySystem/Abilities/VBGA_Stagger.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Executions/VBDamageExecution.h"
#include "Character/VBGuardable.h"
#include "Data/VBFinisherWindowConfig.h"
#include "Data/VBMedicalConfig.h"
#include "Data/VBParryConfig.h"
#include "UI/Indicator/VBExecutePromptComponent.h"
#include "VBPostureExecuteTestTypes.generated.h"

// VB.Unit.Combat.PostureExecute 의 시험 대역: 추상 창, 경직 능력의 구체 클래스, 게이지 전용 효과와 쓸 수 없는(피해 계산을 단) 효과,
//   자기 패링 설정을 든 패링 액터, 대상을 시험이 정하는 프롬프트.

// 게이지 전용 효과(GE_ParryPoise 와 같은 모양): 즉시, IncomingPoiseDamage 에 SetByCaller Data.PoiseDamage 를 더하는 수정값 하나.
UCLASS(NotBlueprintable)
class UVBPostureTestPoiseEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UVBPostureTestPoiseEffect()
	{
		DurationPolicy = EGameplayEffectDurationType::Instant;
		FSetByCallerFloat Amount;
		Amount.DataTag = VBGameplayTags::Data_PoiseDamage;
		FGameplayModifierInfo Modifier;
		Modifier.Attribute = UVBCombatAttributeSet::GetIncomingPoiseDamageAttribute();
		Modifier.ModifierOp = EGameplayModOp::Additive;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Amount);
		Modifiers.Add(Modifier);
	}
};

// 쓸 수 없는 패링 효과: 게이지 수정값 뒤에 체력 피해(IncomingDamage) 수정값이 하나 더 있다.
UCLASS(NotBlueprintable)
class UVBPostureTestPoisePlusHealthEffect : public UVBPostureTestPoiseEffect
{
	GENERATED_BODY()

public:
	UVBPostureTestPoisePlusHealthEffect()
	{
		FGameplayModifierInfo Health;
		Health.Attribute = UVBHealthAttributeSet::GetIncomingDamageAttribute();
		Health.ModifierOp = EGameplayModOp::Additive;
		Health.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(15.0f));
		Modifiers.Add(Health);
	}
};

// 쓸 수 없는 패링 효과: 피해 계산을 단다 - 패링한 쪽의 BaseDamage 가 공격자 체력에 들어간다.
UCLASS(NotBlueprintable)
class UVBPostureTestDamagingPoiseEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UVBPostureTestDamagingPoiseEffect()
	{
		DurationPolicy = EGameplayEffectDurationType::Instant;
		FGameplayEffectExecutionDefinition Execution;
		Execution.CalculationClass = UVBDamageExecution::StaticClass();
		Executions.Add(Execution);
	}
};

// 처형 키 능력(Input.Execute)과 그 사거리 설정.
UCLASS(NotBlueprintable)
class UVBPostureTestExecute : public UVBGA_Execute
{
	GENERATED_BODY()

public:
	UVBPostureTestExecute()
	{
		MedicalConfig = CreateDefaultSubobject<UVBMedicalConfig>(TEXT("MedicalConfig"));
	}
};

// 게이지가 다 차면 여는 창(새 기본값).
UCLASS(NotBlueprintable)
class UVBPostureTestWindowOnPoise : public UVBGA_FinisherWindow
{
	GENERATED_BODY()

public:
	UVBPostureTestWindowOnPoise()
	{
		FinisherWindowConfig = CreateDefaultSubobject<UVBFinisherWindowConfig>(TEXT("WindowConfig"));
		FinisherWindowConfig->OpenOn = EVBFinisherWindowOpenOn::PoiseBreak;
	}
};

// 체력 문턱으로 여는 창(남겨 둔 옛 경로).
UCLASS(NotBlueprintable)
class UVBPostureTestWindowOnHealth : public UVBGA_FinisherWindow
{
	GENERATED_BODY()

public:
	UVBPostureTestWindowOnHealth()
	{
		FinisherWindowConfig = CreateDefaultSubobject<UVBFinisherWindowConfig>(TEXT("WindowConfig"));
		FinisherWindowConfig->OpenOn = EVBFinisherWindowOpenOn::HealthThreshold;
	}
};

// 게이지로 열고 대기 자세가 저작된 창 - 자세를 몇 번 트는지 센다(대기 자세 로그).
UCLASS(NotBlueprintable)
class UVBPostureTestWindowWithPose : public UVBGA_FinisherWindow
{
	GENERATED_BODY()

public:
	UVBPostureTestWindowWithPose()
	{
		FinisherWindowConfig = CreateDefaultSubobject<UVBFinisherWindowConfig>(TEXT("WindowConfig"));
		FinisherWindowConfig->OpenOn = EVBFinisherWindowOpenOn::PoiseBreak;
		FinisherWindowConfig->WaitMontage = CreateDefaultSubobject<UAnimMontage>(TEXT("WaitPose"));
	}
};

// 설정이 빈 창 - 부여 순간 크게 말하고 게이지 태그를 붙이지 않아야 한다.
UCLASS(NotBlueprintable)
class UVBPostureTestWindowNoConfig : public UVBGA_FinisherWindow
{
	GENERATED_BODY()
};

// 콘텐츠 없는 경직: 켜지고(시험이 센다) 미할당 경고와 함께 바로 끝난다.
UCLASS(NotBlueprintable)
class UVBPostureTestStagger : public UVBGA_Stagger
{
	GENERATED_BODY()
};

// 언제나 완벽하게 패링하는 액터. 패링 설정은 시험이 만든 것이라 클래스 디폴트는 건드리지 않는다.
UCLASS(NotBlueprintable, NotPlaceable)
class AVBPostureTestParrier : public ACharacter, public IVBGuardable
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TObjectPtr<UVBParryConfig> ParryConfig;

	virtual bool IsGuarding() const override { return true; }
	virtual float GetGuardElapsed() const override { return 0.0f; }
	virtual void PlayGuardAccept(EVBHitDirection) override {}
	virtual const UVBParryConfig* GetParryConfig() const override { return ParryConfig; }
	virtual void OpenRiposteWindow(float Duration, AActor* Attacker) override {}
	virtual void ApplyGuardStaminaDelta(float Delta) override {}
};

// 대상 질의만 바꾼 프롬프트. 시험하는 것은 등록 규칙이고 대상 규칙은 처형 능력의 것이다.
UCLASS(NotBlueprintable)
class UVBPostureTestPromptComponent : public UVBExecutePromptComponent
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<AActor> TestTarget;

	// 시험 월드는 BeginPlay 를 돌리지 않아 시작, 끝을 직접 부른다.
	void BeginPlayForTest() { BeginPlay(); }
	void EndPlayForTest() { EndPlay(EEndPlayReason::RemovedFromWorld); }

protected:
	virtual AActor* FindPromptTarget(APawn& Pawn) const override { return TestTarget.Get(); }
};
