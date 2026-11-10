// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBDodgeConfig.generated.h"

class UAnimMontage;

/** 회피 방향. 액터 로컬 기준(UE 규약 X=전방/Y=우측)으로 판정한다. 대각선 넷은 뒤에 붙였다(append-only). */
UENUM(BlueprintType)
enum class EVBDodgeDirection : uint8
{
	Forward		UMETA(DisplayName="Forward"),
	Backward	UMETA(DisplayName="Backward"),
	Left		UMETA(DisplayName="Left"),
	Right		UMETA(DisplayName="Right"),
	ForwardLeft		UMETA(DisplayName="Forward Left"),
	ForwardRight	UMETA(DisplayName="Forward Right"),
	BackwardLeft	UMETA(DisplayName="Backward Left"),
	BackwardRight	UMETA(DisplayName="Backward Right")
};

// 한 번의 회피 방향: 8방향 판정과, 대각선 클립이 없을 때 쓰는 4방향 판정(종전 규칙).
struct FVBDodgeHeading
{
	EVBDodgeDirection Eight = EVBDodgeDirection::Backward;
	EVBDodgeDirection Cardinal = EVBDodgeDirection::Backward;
};

/**
 * 한 세트(무기 또는 통일)의 방향별 회피 몽타주. 방향은 액터 로컬 기준으로 고른다.
 * 빈 슬롯은 무기세트 -> 통일세트 -> LaunchCharacter 안전망 순으로 내려간다.
 */
USTRUCT(BlueprintType)
struct FVBDodgeMontageSet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dodge")
	TObjectPtr<UAnimMontage> Forward = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dodge")
	TObjectPtr<UAnimMontage> Backward = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dodge")
	TObjectPtr<UAnimMontage> Left = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dodge")
	TObjectPtr<UAnimMontage> Right = nullptr;

	// 대각선. 비면 그 회피는 4방향 판정의 칸으로 간다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dodge")
	TObjectPtr<UAnimMontage> ForwardLeft = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dodge")
	TObjectPtr<UAnimMontage> ForwardRight = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dodge")
	TObjectPtr<UAnimMontage> BackwardLeft = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dodge")
	TObjectPtr<UAnimMontage> BackwardRight = nullptr;
};

// 회피, 구르기 끝에서 달리기로 이어지는 클립(앞, 앞왼, 앞오른). 비면 전환하지 않는다.
USTRUCT(BlueprintType)
struct FVBDodgeToRun
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dodge")
	TObjectPtr<UAnimMontage> Forward = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dodge")
	TObjectPtr<UAnimMontage> ForwardLeft = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dodge")
	TObjectPtr<UAnimMontage> ForwardRight = nullptr;

	// 회피 클립의 이 시각(초)에 넘길지 정한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dodge", meta=(ClampMin="0.0"))
	float ExitTime = 0.5f;

	// 그때 이동 입력이 회피 방향에서 이 각도(도) 안이면 전환한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dodge", meta=(ClampMin="0.0", ClampMax="180.0"))
	float MaxStickAngleDeg = 60.0f;

	// 이 회피 방향의 이어 달리기 클립. 앞쪽 셋만 있다.
	UAnimMontage* Pick(EVBDodgeDirection Eight) const
	{
		switch (Eight)
		{
		case EVBDodgeDirection::Forward: return Forward;
		case EVBDodgeDirection::ForwardLeft: return ForwardLeft;
		case EVBDodgeDirection::ForwardRight: return ForwardRight;
		default: return nullptr;
		}
	}
};

/**
 * VBGA_Dodge 가 쓰는 회피 시간/강도/몽타주 설정.
 * DodgeStrength 는 LaunchCharacter 에 그대로 가므로 과도하면 벽을 뚫을 수 있다.
 */
UCLASS()
class VB_API UVBDodgeConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VB|Combat")
	float DodgeDuration = 0.4f; // 무적시간

	// === 회피 중 공격 ===
	// 회피 시작부터 이 시각(초)까지는 공격이 롤을 끊지 못한다(State.Combat.DodgeCommit). 0 = 언제든 끊김.
	// 올리면 롤이 더 보이고 조작 반응은 늦어진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VB|Combat", meta=(ClampMin="0.0", ClampMax="2.0"))
	float AttackCancelSeconds = 0.45f;

	// 커밋 중 눌린 공격을 기억했다가 커밋이 풀리는 순간 내보내는 유예(초). 너무 오래된 입력은 버린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VB|Combat", meta=(ClampMin="0.0", ClampMax="2.0"))
	float AttackBufferSeconds = 0.5f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VB|Combat")
	float DodgeStrength = 1200.0f; // LaunchCharacter 속도
	// 회피 시 상방 부스트 - LaunchCharacter 속도에 합성(전방*DodgeStrength + 이 값)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VB|Combat")
	FVector DodgeVerticalBoost = FVector(0.0f, 0.0f, 100.0f);

	// 닷지 애니메이션 (클립 루트모션이 이동 담당)
	// 통일 닷지 - 맨손, 전용 세트 없는 무기가 쓰고, 전용 세트의 빈 방향을 메운다. 무기 전용 세트는 UVBWeaponDefinition 에 있다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dodge")
	FVBDodgeMontageSet DefaultDodgeSet;

	// 닷지 몽타주 재생속도. 1.0 = 클립 원래 템포. 올려도 루트모션 이동거리는 보존된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dodge", meta=(ClampMin="0.1"))
	float DodgeMontagePlayRate = 1.0f;

	// 무기 전용 세트(없으면 nullptr)와 방향(판정은 GA)으로 몽타주 해석. 폴백 = 무기세트 -> 통일세트 -> nullptr(호출부가 LaunchCharacter 안전망).
	UAnimMontage* ResolveDodgeMontage(const FVBDodgeMontageSet* WeaponSet, EVBDodgeDirection Direction) const;
	// 8방향 칸 -> 4방향 칸 순으로, 무기 세트 -> 통일 세트 순으로 찾는다. 대각선이 없는 세트는 종전과 같은 클립.
	UAnimMontage* ResolveDodgeMontage(const FVBDodgeMontageSet* WeaponSet, const FVBDodgeHeading& Heading) const;
	// 세트 하나 안에서만: 8방향 칸, 없으면 4방향 칸, 없으면 nullptr.
	static UAnimMontage* PickFromSet(const FVBDodgeMontageSet& Set, const FVBDodgeHeading& Heading);
};
