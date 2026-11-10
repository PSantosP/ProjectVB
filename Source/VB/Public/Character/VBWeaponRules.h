// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Types/VBWeaponStateTypes.h"
#include "VBWeaponRules.generated.h"

/**
 * 가진 무기를 읽고 쓰는 규칙. 담당 클래스가 UVBWeaponStateComponent 라 Character/ 에 둔다. 대상은 호출한 플레이어의 폰이다.
 * 대화, 퀘스트, 장면 보상용이다 - 결과에 따라야 하는 뒤 효과는 OnGrantedEffects 에 둔다.
 */

/** 부른 플레이어가 그 무기를 가졌나(bNegate 면 안 가졌나). */
USTRUCT(BlueprintType, meta=(DisplayName="무기 가짐"))
struct VB_API FVBCondition_HasWeapon : public FVBCondition
{
	GENERATED_BODY()

	// None 이면 아무 무기라도 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	EVBWeaponType Weapon = EVBWeaponType::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	bool bNegate = false;

	virtual bool Evaluate(const FVBRuleContext& Context) const override;
};

/** 부른 플레이어에게 무기를 준다(이미 가졌으면 그대로). 가졌으면(새로, 이미) OnGrantedEffects 를 잇고, 실패면 잇지 않고 문맥에 실패를 알린다. */
USTRUCT(BlueprintType, meta=(DisplayName="무기 주기"))
struct VB_API FVBEffect_GrantWeapon : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	EVBWeaponType Weapon = EVBWeaponType::Blade;

	// 무기를 가진 뒤에만 도는 효과(획득 사실, 경험치, 저장). 비면 주기만 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story", meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> OnGrantedEffects;

	virtual void Apply(const FVBRuleContext& Context) const override;
};

/** 부른 플레이어에게서 무기를 거둔다(손에 들었으면 먼저 넣는다). 없으면 아무 일도 없다. */
USTRUCT(BlueprintType, meta=(DisplayName="무기 거두기"))
struct VB_API FVBEffect_RevokeWeapon : public FVBEffect
{
	GENERATED_BODY()

	// 비우면(None) 저작 오류다 - 무엇을 거둘지 기본값으로 정하지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	EVBWeaponType Weapon = EVBWeaponType::None;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
