// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBItemDefinition.generated.h"

class UAnimMontage;
class UTexture2D;
class UStaticMesh;
class UVBAppearancePiece;

/** 아이템 조각의 바탕. 일부 아이템만 갖는 성질(사용, 가격, 장비)을 클래스 파생 대신 조각으로 붙인다. */
USTRUCT(BlueprintType)
struct VB_API FVBItemFragment
{
	GENERATED_BODY()

	virtual ~FVBItemFragment() = default;
};

USTRUCT(BlueprintType, meta=(DisplayName="Acquisition Effects"))
struct VB_API FVBItemFragment_AcquireEffects : public FVBItemFragment
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item",
		meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> Effects;
};

/** 쓸 수 있는 아이템(회복약, 치료제). 조건이 참이면 공용 효과를 적용한다 - 아이템 전용 효과 코드는 없다. */
USTRUCT(BlueprintType, meta=(DisplayName="사용"))
struct VB_API FVBItemFragment_Usable : public FVBItemFragment
{
	GENERATED_BODY()

	// 전부 참일 때만 쓸 수 있다. 비면 늘 쓸 수 있다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;

	// 쓰면 차례대로 적용한다(서버). 부른 플레이어 = 쓴 사람.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item",
		meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> Effects;

	// 쓰면 하나 줄어드나. 꺼 두면 몇 번이고 쓰는 도구(예: 등불)다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item")
	bool bConsumeOnUse = true;

	// 사용할 때 재생할 몽타주. 비면 바로 쓴다. 있으면 표식(UVBAN_ItemUseMarker) 시각에 소모, 효과를 확정한다.
	//   표식 전에 끊기면 아무것도 소모하지 않는다. 표식은 정확히 하나여야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item")
	TObjectPtr<UAnimMontage> UseMontage;

	// 쓰는 동안 든 무기를 수납 동작 없이 숨겼다가 끝나면 되돌린다. 두 손을 쓰는 동작에 켠다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item")
	bool bSheatheWeaponDuringUse = false;
};

/**
 * 입을 수 있는 아이템(방어구, 장신구).
 * 공용 GE 하나(UVBEquipmentComponent::EquipmentEffect)에 이 지도의 값을 SetByCaller 로 실어 건다 - 장비마다 GE 를 만들지 않는다.
 */
USTRUCT(BlueprintType, meta=(DisplayName="장비"))
struct VB_API FVBItemFragment_Equippable : public FVBItemFragment
{
	GENERATED_BODY()

	// 들어갈 칸 종류(머리, 몸통, 팔, 장신구).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item", meta=(Categories="Equipment.Slot"))
	FGameplayTag SlotType;

	// 능력치 -> 더할 값(예: Data.Stat.MaxHealth -> 20). 없는 능력치는 0.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item", meta=(Categories="Data.Stat"))
	TMap<FGameplayTag, float> Stats;
};

/**
 * 입으면 겉모습이 바뀌는 장비. 조각의 외형 칸은 장비 칸에 대응하는 칸이어야 한다(UVBAppearanceSettings).
 * 멤버가 EditAnywhere 인 이유: 파이썬 저작 경로가 EditDefaultsOnly 를 거부한다.
 */
USTRUCT(BlueprintType, meta=(DisplayName="외형 조각"))
struct VB_API FVBItemFragment_Appearance : public FVBItemFragment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Item")
	TSoftObjectPtr<UVBAppearancePiece> Piece;

	// 일부러 겉모습이 없는 장비. 빠뜨린 조각과 구별하려고 표시한다(IsDataValid). 켜면 Piece 는 비운다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Item", meta=(DisplayName="겉모습 없음"))
	bool bNoAppearance = false;
};

/**
 * 사고팔 수 있는 아이템의 기준 값. 상점은 다르게 할 때만 줄에서 덮어쓴다.
 * 실제 값 계산은 VBShop::UnitPrice 하나가 맡는다.
 */
USTRUCT(BlueprintType, meta=(DisplayName="가격"))
struct VB_API FVBItemFragment_Price : public FVBItemFragment
{
	GENERATED_BODY()

	// 값 규칙을 걸기 전의 값(돈 아이템 개수).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item", meta=(ClampMin="1"))
	int32 BaseValue = 1;
};

/**
 * 아이템 하나의 정의(실행 중 불변). 자산 ID = (VBItem, ItemId 태그 이름).
 * 저장은 자산 경로가 아니라 ItemId 태그로 한다 - 자산을 옮겨도 옛 저장이 깨지지 않는다.
 */
UCLASS(BlueprintType, Const)
class VB_API UVBItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static FPrimaryAssetType GetItemAssetType();
	static FPrimaryAssetId MakeItemAssetId(FGameplayTag InItemId);

	// ItemId -> 정의(에셋 매니저 목록에서). 못 찾으면 nullptr. 저장 줄을 되찾는 곳은 모두 이것을 쓴다.
	static const UVBItemDefinition* FindById(const FGameplayTag& InItemId);

	// 저장, 조건, 퀘스트 목표가 이 아이템을 부르는 이름. 자산마다 하나, 겹치면 안 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item", meta=(Categories="Item"))
	FGameplayTag ItemId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item", meta=(MultiLine=true))
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item")
	TSoftObjectPtr<UTexture2D> Icon;

	// 땅에 놓였을 때의 모습. 인벤토리 아이템이 메시까지 끌어오지 않게 소프트로 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item")
	TSoftObjectPtr<UStaticMesh> WorldMesh;

	// 가질 수 있는 최대 수. 0 = 무제한(돈 등).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item", meta=(ClampMin="0"))
	int32 MaxCount = 99;

	// 인벤토리 화면에서 버릴 수 있나(발밑에 떨어진다). 잃으면 안 되는 돈, 열쇠는 끈다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item")
	bool bCanDiscard = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Item",
		meta=(BaseStruct="/Script/VB.VBItemFragment", ExcludeBaseStruct))
	TArray<FInstancedStruct> Fragments;

	// 이 종류의 조각(없으면 nullptr). 같은 종류가 둘이면 앞의 것.
	template <typename T>
	const T* FindFragment() const
	{
		for (const FInstancedStruct& Fragment : Fragments)
		{
			if (const T* Found = Fragment.GetPtr<T>())
			{
				return Found;
			}
		}

		return nullptr;
	}

	// 넣을 수 있는 수(지금 Current 개 가졌을 때 Requested 개를 더 넣으려 하면).
	int32 ClampToMax(int32 Current, int32 Requested) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
