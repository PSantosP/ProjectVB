// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AbilitySystem/VBHitImpact.h"
#include "VBRagdollConfig.generated.h"

class UPhysicsControlAsset;
class UPhysicsAsset;
class UAnimMontage;
class UChooserTable;

UENUM(BlueprintType)
enum class EVBRagdollPhysicsProfile : uint8
{
	None,
	PhysicalAnimation,
	Ragdoll,
	Kinematic
};

UENUM(BlueprintType)
enum class EVBRagdollInjuryState : uint8
{
	None,
	HeadFace,
	HeadBack,
	Stunned,
	BodyFront,
	Limp,
	Groin
};

USTRUCT(BlueprintType)
struct FVBRagdollStrengthBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Curve;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName ControlSet;
};

/** 골격별 Physics Control 계약. 기본 수치는 GASP 5.8 Mover_Ragdoll 그래프에서 확인했다. */
UCLASS(BlueprintType)
class VB_API UVBRagdollConfig : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Behaviors")
	EVBRagdollInjuryState InjuryState = EVBRagdollInjuryState::None;

	// 둘 다 명시해야 한다. 다른 골격의 PCA를 지정하고 부분 생성 성공을 정상으로 취급하지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Physics Control")
	TObjectPtr<UPhysicsControlAsset> PhysicsControlAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Physics Control")
	TObjectPtr<UPhysicsAsset> ExpectedPhysicsAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Physics Control")
	FName RagdollProfile = TEXT("Ragdoll");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Physics Control")
	FName PhysicalAnimationProfile = TEXT("PhysicalAnimation");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Physics Control")
	FName ConstraintProfile = NAME_None;

	// 물리 애니메이션 중 쓰는 제약 프로필(GASP SetPhysicsProfile 은 Free).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Physics Control")
	FName PhysicalAnimationConstraintProfile = TEXT("Free");

	// 물리 애니메이션 중에만 두 다리 사이 충돌을 끈다. 이 뿌리 아래 바디를 센다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Physics Control")
	FName LeftLegRoot = TEXT("thigh_l");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Physics Control")
	FName RightLegRoot = TEXT("thigh_r");

	// 평소 메시 물리 프로필. 기본은 키네마틱, 서 있을 때도 물리 애니메이션을 쓰는 캐릭터는 바꾼다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Physics Control")
	EVBRagdollPhysicsProfile DefaultPhysicsProfile = EVBRagdollPhysicsProfile::Kinematic;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Get Up")
	TObjectPtr<UChooserTable> RecoverableGetUpChooser;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Get Up", meta=(ClampMin="0"))
	float RollingGetUpSpeed = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Get Up", meta=(ClampMin="0"))
	float CapsuleRotationFreezeSeconds = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Get Up", meta=(ClampMin="0.01"))
	float NPCGetUpCheckSeconds = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Get Up", meta=(ClampMin="0"))
	float NPCGetUpSpeed = 10.0f;

	// 쓰러진 지 이 시간이 지나면 맞아도 밀지 않는다(피해, 이펙트는 그대로). 물리 바디가 멈춰야 기상 판정에서 일어난다.
	// 이 값 + NPCGetUpCheckSeconds + 기상 길이가 UVBFinisherWindowConfig::MaxReactionWaitSeconds 보다 짧아야 한다(검증기 없음).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Downed Hit", meta=(ClampMin="0", Units="s"))
	float DownedHitWindowSeconds = 2.5f;

	// 실효 등급별 수평 속도 충격(cm/s, 질량 무관). 맞은 뼈의 접점에 밀린 방향으로 준다. 없거나 0 인 등급은 안 민다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Downed Hit")
	TMap<EVBHitImpact, float> DownedHitVelocity = {
		{EVBHitImpact::Light, 150.0f}, {EVBHitImpact::Heavy, 300.0f}, {EVBHitImpact::Knockdown, 500.0f}, {EVBHitImpact::Launch, 500.0f}};

	// 위로 살짝(cm/s). 바닥 마찰을 이기기 위한 값이라 체격 배수를 곱하지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Downed Hit", meta=(ClampMin="0"))
	float DownedHitUpVelocity = 80.0f;

	// 이관 검증용으로 남긴 저작 데이터. 실제 선택은 Chooser 가 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Get Up")
	TArray<TObjectPtr<UAnimMontage>> RecoverableGetUpMontages;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Physics Control")
	TArray<FVBRagdollStrengthBinding> StrengthBindings = {
		{TEXT("Ragdoll_Strength_Arms"), TEXT("ParentSpace_Arms")},
		{TEXT("Ragdoll_Strength_Legs"), TEXT("ParentSpace_Legs")},
		{TEXT("Ragdoll_Strength_Torso"), TEXT("ParentSpace_Torso")},
		{TEXT("Ragdoll_Strength_Head"), TEXT("ParentSpace_Head")}};

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Behaviors")
	FName SpineBody = TEXT("spine_05");

	// 이 소켓의 회전이 impact blend space 의 좌표계다. 뼈 이름만 바꿔서는 같은 축이 되지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Behaviors")
	FName ImpactTraceSocket = TEXT("RagdollTrace");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Behaviors", meta=(ClampMin="0"))
	float ImpactTraceRadius = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Behaviors", meta=(ClampMin="0.001"))
	float ImpactPredictionSeconds = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Behaviors", meta=(ClampMin="0.001"))
	float ImpactPredictionFrequency = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Behaviors")
	TEnumAsByte<ECollisionChannel> ImpactTraceChannel = ECC_PhysicsBody;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Behaviors", meta=(ClampMin="0"))
	float GravityMultiplier = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Behaviors", meta=(ClampMax="0"))
	float GravityCutoffVerticalSpeed = -2500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Behaviors")
	FVector2D AutoRollSlopeDegrees = FVector2D(20.0, 30.0);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Behaviors", meta=(ClampMin="0"))
	float AutoRollIncreaseSpeed = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Behaviors", meta=(ClampMin="0"))
	float AutoRollDecreaseSpeed = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Behaviors", meta=(ClampMin="0"))
	float RollTorqueAcceleration = 100000.0f;
};
