// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "VBWeaponStateTypes.generated.h"

// 로코모션 상태. "비전투+무기On" 은 없다 - 무기를 꺼냈다 = 전투 자세다.
// 옛 이름 ArmedExploration 은 DefaultEngine.ini 의 EnumRedirects 가 잇는다(없으면 Unarmed 로 떨어진다).
UENUM(BlueprintType)
enum class EVBLocomotionState : uint8
{
	Unarmed UMETA(DisplayName="Unarmed"), // 평상(비전투, 무기 없음) - MM 로코모션
	CombatSheathed UMETA(DisplayName="Combat Sheathed"), // 전투, 무기Off(납도) - 맨손 MM 풀
	ArmedCombat UMETA(DisplayName="Armed Combat"), // 전투, 무기On - 무기별 Strafe BS
};

// 무기 타입 열거형
UENUM(BlueprintType)
enum class EVBWeaponType : uint8
{
	None UMETA(DisplayName="None"),
	Blade UMETA(DisplayName="Blade"), // Scalpel 오라
	BigSword UMETA(DisplayName="Big Sword"), // 대형 Scalpel
	Fighter UMETA(DisplayName="Fighter"), // Melee 오라 (맨손 격투)
	Magic UMETA(DisplayName="Magic"), // 마법 오라 (원거리 투사체). append-only
	OneHandSword UMETA(DisplayName="One-Hand Sword"), // 한손검. append-only
};

class UAnimMontage;
class UNiagaraSystem;
class UVBFinisherConfig;

// 무기 하나의 꺼내기, 넣기, 바꾸기 데이터. 무기 정의(UVBWeaponDefinition)가 하나씩 든다.
USTRUCT(BlueprintType)
struct FVBWeaponSummonData
{
	GENERATED_BODY()

	// 발도 상태에서 쓸 MM 풀을 가진 무기. 보통 자기 자신, 전용 풀이 없으면 None(기본 풀).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Weapon")
	EVBWeaponType ArmedMMPool = EVBWeaponType::None;

	// 납도 상태에서 쓸 MM 풀을 가진 무기. None = 기본 풀.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Weapon")
	EVBWeaponType SheathedMMPool = EVBWeaponType::None;

	// 이 무기로 걸 수 있는 처형들. 방향이 맞는 것 중 랜덤으로 고른다. 비어 있어도 정상이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Weapon")
	TArray<TObjectPtr<UVBFinisherConfig>> FinisherConfigs;

	// 무기 소환 시 재생할 모션
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Weapon")
	TObjectPtr<UAnimMontage> SummonMontage = nullptr;

	// 무기 수납 시 재생할 모션 (칼 넣기 등)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Weapon")
	TObjectPtr<UAnimMontage> SheatheMontage = nullptr;

	// 이전 무기별 전환 몽타주(탐험 스탠스). 키가 없으면 SummonVFX 만 낸다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Weapon")
	TMap<EVBWeaponType, TObjectPtr<UAnimMontage>> ChangeFromMontages;

	// 전투 스탠스용 전환 몽타주. 비어 있으면 ChangeFromMontages 를 쓴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Weapon")
	TMap<EVBWeaponType, TObjectPtr<UAnimMontage>> ChangeFromMontagesCombat;

	// 소환/전환/수납 때 내는 나이아가라
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Weapon")
	TObjectPtr<UNiagaraSystem> SummonVFX = nullptr;

	// SummonVFX 를 붙일 소켓.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Weapon")
	FName SummonVFXSocket = FName("hand_r");
};
