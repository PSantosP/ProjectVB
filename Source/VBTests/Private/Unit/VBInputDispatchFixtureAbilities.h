#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBAttackActivationData.h"
#include "VBTestAbilityTags.h"
#include "VBInputDispatchFixtureAbilities.generated.h"

// 입력 분배 시험용 능력. 켜지면 스스로 끝나지 않고 남아 '켜진 능력이 입력을 받는가' 를 볼 수 있다.
UCLASS(NotBlueprintable)
class UVBInputFixtureAbility : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	// 하위 클래스가 이름을 주지 않으면 어떤 실제 능력도 대신하지 않는다(UVBGameplayAbility 는 빈 identity 를 거절한다)
	UVBInputFixtureAbility() { SetAssetTags(FGameplayTagContainer(VBTestAbilityTags::AnyAbility)); }

	int32 PressedWhileActive = 0;
	int32 ReleasedWhileActive = 0;

protected:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
		const FGameplayAbilityActivationInfo, const FGameplayEventData*) override {}
	virtual void InputPressed(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override
	{
		Super::InputPressed(Handle, ActorInfo, ActivationInfo);
		++PressedWhileActive;
	}

	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override
	{
		Super::InputReleased(Handle, ActorInfo, ActivationInfo);
		++ReleasedWhileActive;
	}
};

// 켜져 있는 동안 회피 입력을 가져가는 능력(엎드린 몸의 빠져나오기 자리). 자기 입력은 정화.
UCLASS(NotBlueprintable)
class UVBInputFixtureClaimer : public UVBInputFixtureAbility
{
	GENERATED_BODY()
public:
	UVBInputFixtureClaimer()
	{
		InputTag = VBGameplayTags::Input_Interact;
		ClaimedInputTags.AddTag(VBGameplayTags::Input_Dodge);
	}
};

// 활성화 전 준비를 하는 능력 - 분배가 그 이벤트 데이터로 켜는지 본다.
UCLASS(NotBlueprintable)
class UVBInputFixturePrepared : public UVBInputFixtureAbility
{
	GENERATED_BODY()
public:
	UVBInputFixturePrepared() { InputTag = VBGameplayTags::Input_Purify; }
	bool bReceivedPayload = false;
	FGameplayTag ReceivedEventTag;
	int32 ReceivedPress = INDEX_NONE;
	int32 ReceivedVariant = INDEX_NONE;

	virtual bool PrepareActivation(const FVBActivationRequest& Request, FGameplayEventData& OutPayload) const override
	{
		FVBAttackActivationData* Data = new FVBAttackActivationData();
		Data->PressSerial = Request.PressSerial;
		Data->VariantIndex = 3;
		OutPayload.TargetData.Add(Data);
		return true;
	}

protected:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
		const FGameplayAbilityActivationInfo, const FGameplayEventData* TriggerEventData) override
	{
		bReceivedPayload = TriggerEventData != nullptr;
		if (TriggerEventData)
		{
			ReceivedEventTag = TriggerEventData->EventTag;
		}

		if (const FVBAttackActivationData* Data = FVBAttackActivationData::Find(TriggerEventData))
		{
			ReceivedPress = Data->PressSerial;
			ReceivedVariant = Data->VariantIndex;
		}
	}
};

// 능력 자체가 토글(웅크리기 자리) - 다시 누르면 꺼지고 놓음은 무시한다.
UCLASS(NotBlueprintable)
class UVBInputFixtureToggle : public UVBInputFixtureAbility
{
	GENERATED_BODY()
public:
	UVBInputFixtureToggle()
	{
		InputTag = VBGameplayTags::Input_Crouch;
		bIsToggleAbility = true;
	}
};

// 경공격 자리 - 가드, 반격 창이 있으면 막힌다.
UCLASS(NotBlueprintable)
class UVBInputFixtureLight : public UVBInputFixtureAbility
{
	GENERATED_BODY()
public:
	UVBInputFixtureLight()
	{
		InputTag = VBGameplayTags::Input_Attack_Light;
		ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Guarding);
		ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_RiposteWindow);
	}
};

// 반격 자리 - 반격 창이 있어야 켜진다.
UCLASS(NotBlueprintable)
class UVBInputFixtureRiposte : public UVBInputFixtureAbility
{
	GENERATED_BODY()
public:
	UVBInputFixtureRiposte()
	{
		InputTag = VBGameplayTags::Input_Attack_Light;
		ActivationRequiredTags.AddTag(VBGameplayTags::State_Combat_RiposteWindow);
	}
};

// 홀드 입력 자리(가드). 놓으면 끝나는 정책을 가드, 질주처럼 스스로 켠다(기본은 끝나지 않음).
UCLASS(NotBlueprintable)
class UVBInputFixtureHold : public UVBInputFixtureAbility
{
	GENERATED_BODY()
public:
	UVBInputFixtureHold()
	{
		InputTag = VBGameplayTags::Input_Combat_Guard;
		bCancelOnInputRelease = true;
	}
};

// 놓음 정책을 적지 않은 누름 능력 자리(회피 입력). 기본값이 '놓아도 계속' 인지 본다.
UCLASS(NotBlueprintable)
class UVBInputFixtureDefaultRelease : public UVBInputFixtureAbility
{
	GENERATED_BODY()
public:
	UVBInputFixtureDefaultRelease() { InputTag = VBGameplayTags::Input_Dodge; }
};
