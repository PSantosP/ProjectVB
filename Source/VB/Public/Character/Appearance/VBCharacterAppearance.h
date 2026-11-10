#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBCharacterAppearance.generated.h"

class UAnimInstance;
class USkeletalMesh;
class UVBAppearancePiece;

/**
 * 플레이어 겉모습 하나. 본체 메시가 애니, 판정, 물리를 맡고, 고른 겉모습 액터가 그 아래 붙어 리타겟으로 따라 한다.
 * 리타겟 칸은 BP 가 리타겟을 들지 않는 겉모습(조립한 MetaHuman)에만 채운다 - 둘 다 있으면 IsDataValid 가 막는다.
 */
UCLASS(BlueprintType)
class VB_API UVBCharacterAppearance : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static FPrimaryAssetType GetAppearanceAssetType() { return FPrimaryAssetType(TEXT("VBAppearance")); }

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Appearance")
	FText DisplayName;

	// 겉모습 액터. 각 머신이 직접 만들므로 복제하지 않는 클래스여야 한다(IsDataValid).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Appearance")
	TSoftClassPtr<AActor> VisualActorClass;

	// 무기 부착, 소켓 질의에 쓰는 주 바디 스켈레탈 메시 컴포넌트 이름(BP 변수 이름 - Kellan "Body", Manny "Manny").
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Appearance")
	FName PrimaryBodyComponent;

	// 본체 소켓 -> 겉모습 주 바디 소켓. 여기 없는 이름은 같은 이름으로 찾고, 겉모습에 없으면 본체에 붙인다(경고).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Appearance")
	TMap<FName, FName> SocketAliases;

	// 겉모습 BP 가 리타겟을 안 가지고 있을 때만(조립한 MetaHuman). 주 바디 메시에 넣을 애님(보통 ABP_GenericRetarget).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Appearance|Retarget")
	TSoftClassPtr<UAnimInstance> RetargetAnimClass;

	// 위 애님이 고를 리타겟터 이름 - 주 바디 메시의 첫 컴포넌트 태그로 넣는다(ABP_GenericRetarget 의 규약). 예: RTG_UEFN_to_Metahuman_nrw.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Appearance|Retarget")
	FName RetargeterTag;

	// 칸 있는 캐릭터의 이름 - 조각이 이 이름으로 맞춘 메시를 찾는다. 비면 칸 없는 겉모습이다.
	//   아래 두 칸이 EditAnywhere 인 이유: 파이썬 저작 경로가 EditDefaultsOnly 를 거부한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Appearance|Slots")
	FName CharacterId;

	// 아무것도 입지 않았을 때의 조각(상의, 하의 필수 - 맨몸 메시는 목 아래가 비었다). 칸은 조각 자신의 AppearanceSlot 이 정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Appearance|Slots")
	TArray<TSoftObjectPtr<UVBAppearancePiece>> DefaultPieces;

	FName ResolveSocket(FName DriverSocket) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;

private:
	// 겉모습 클래스가 있고 복제하지 않으며 주 바디 메시를 가졌는지 본다.
	void ValidateVisualClass(bool bHasClass, bool bReplicates, const TArray<FName>& MeshNames,
		class FDataValidationContext& Context, EDataValidationResult& Result) const;
	// 리타겟 칸이 짝을 이루고 소유자가 하나인지 본다.
	void ValidateRetargetOwner(bool bBodyHasAnimOrTag, class FDataValidationContext& Context, EDataValidationResult& Result) const;
	// 외형 칸의 캐릭터 이름과 바디 메시를 보고 기본 조각 검사로 진행한다.
	void ValidateDefaultPieces(const UClass* VisualClass, class FDataValidationContext& Context, EDataValidationResult& Result) const;
	// 바디 메시의 섹션과 기본 조각이 이 바디에 맞는지 본다.
	void ValidateFittedPieces(const USkeletalMesh& Body, TFunctionRef<void(const FText&)> Fail) const;
#endif
};

// 겉모습 클래스 검증의 판정부 - 클래스 해석과 분리해 시험한다(VBCharacterAppearanceTests).
namespace VBAppearanceRules
{
	enum class EClassCheck : uint8
	{
		Ok,
		NoClass,
		Replicated,
		NoPrimaryBody,
	};

	VB_API EClassCheck CheckVisualClass(bool bHasClass, bool bClassReplicates,
		const TArray<FName>& SkeletalMeshComponentNames, FName PrimaryBody);

	enum class ERetargetCheck : uint8
	{
		Ok,
		HalfSet, // 애님과 태그 중 하나만 채움
		TwoOwners, // 데이터와 겉모습 BP 가 둘 다 리타겟을 들고 있음
	};

	// bDataAnim/bDataTag = 자산의 두 칸, bBodyHasAnimOrTag = 겉모습 BP 의 주 바디 메시가 애님이나 태그를 이미 가지고 있는가.
	VB_API ERetargetCheck CheckRetargetOwner(bool bDataAnim, bool bDataTag, bool bBodyHasAnimOrTag);
}
