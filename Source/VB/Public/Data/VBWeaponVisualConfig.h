#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Types/VBWeaponStateTypes.h"
#include "VBWeaponVisualConfig.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UNiagaraSystem;
class UAnimMontage;

// 몽타주 재생 위치(초)로 평가하는 구간. 프레임 생략/재생 속도 변화에도 동기화된다.
USTRUCT(BlueprintType)
struct FVBWeaponVisualTiming
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Weapon|Timing", meta=(ClampMin="0")) float DissolveStart = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Weapon|Timing", meta=(ClampMin="0")) float DissolveEnd = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Weapon|Timing", meta=(ClampMin="0")) float FormationStart = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Weapon|Timing", meta=(ClampMin="0")) float FormationEnd = 0.0f;

	bool IsValid(float MontageLength) const
	{
		return FMath::IsFinite(DissolveStart) && FMath::IsFinite(DissolveEnd)
			&& FMath::IsFinite(FormationStart) && FMath::IsFinite(FormationEnd)
			&& DissolveStart >= 0.0f && DissolveEnd >= DissolveStart
			&& FormationStart >= DissolveEnd && FormationEnd >= FormationStart
			&& FormationEnd <= MontageLength;
	}

	static float EvaluateWindow(float Position, float Start, float End)
	{
		if (Position < Start)
		{
			return 0.0f;
		}

		if (End <= Start)
		{
			return 1.0f;
		}

		return FMath::Clamp((Position - Start) / (End - Start), 0.0f, 1.0f);
	}

	float GetOutgoingProgress(float Position) const { return 1.0f - EvaluateWindow(Position, DissolveStart, DissolveEnd); }
	float GetIncomingProgress(float Position) const { return EvaluateWindow(Position, FormationStart, FormationEnd); }
};


// 한 손에 붙는 외형. Fighter는 메시 없는 항목을 양손에 하나씩 둔다.
USTRUCT(BlueprintType)
struct FVBWeaponVisualPart
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UMaterialInterface> Material;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UNiagaraSystem> FormationEffect;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UNiagaraSystem> IdleEffect;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Socket = TEXT("hand_r");
	// 이 패시브(Passive.*)를 얻었을 때만 보인다 - 식판 방패 = Passive.Parry, 가드를 여는 것과 같은 한 사실. 비면 늘 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Categories="Passive")) FGameplayTag RequiredPassive;
	// 꺼내기, 거두기 이펙트가 모양을 따올 메시를 받는 Niagara User 파라미터 이름. 비면 모양 없이 소켓에서 터진다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName ShapeMeshParameter;
	// 메시 없는 부품(Fighter 주먹)은 바디 메시의 이 뼈들만 가져온다(손, 아래팔). 메시 전체는 쓰지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> ShapeBones;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FTransform AttachmentTransform;
	// 메시 로컬 공간에서 손잡이 -> 칼끝. 길이와 시작점은 메시 bounds에서 얻는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector RevealAxis = FVector::UpVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor Color = FLinearColor(0.10f, 0.65f, 0.85f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector FormationScale = FVector(0.10f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector IdleScale = FVector(0.15f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector IdleOffset = FVector::ZeroVector;
	// Niagara별 모양 설정. 키는 User.Radius처럼 전체 파라미터 이름을 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TMap<FName, float> IdleFloatParameters;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TMap<FName, bool> IdleBoolParameters;
	// 형성 효과 위치 = 진행 * Scale - Bias. 경계가 메시 양끝을 조금 넘어가야 끝까지 드러난다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float RevealFrontScale = 1.10f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float RevealFrontBias = 0.05f;
	// 형성 효과의 주색, 보조색 밝기 배율.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float FormationColorScale = 1.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float FormationSecondaryColorScale = 0.6f;
	// 대기 효과 밝기. 무기 메시가 있을 때 / 맨손일 때.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float IdleColorStrengthWithMesh = 0.20f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float IdleColorStrengthBareHand = 0.35f;
	// 대기 효과 보조색 밝기. 주색 밝기에 곱한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float IdleSecondaryColorScale = 0.45f;
	// PreviewSummon 이 실체화를 끝내는 시간(초).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.01")) float PreviewDuration = 1.5f;
};

// 이펙트가 모양을 따올 곳(순수 판정).
enum class EVBWeaponShapeSource : uint8 { None, WeaponMesh, BodyBones };
VB_API EVBWeaponShapeSource VBResolveWeaponShapeSource(const FVBWeaponVisualPart& Part);

USTRUCT(BlueprintType)
struct FVBWeaponVisualStyle
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVBWeaponVisualPart> Parts;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.05")) float FormationDuration = 0.65f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.05")) float DissolveDuration = 0.25f;
};

UCLASS(BlueprintType)
class VB_API UVBWeaponVisualConfig : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Weapons")
	TMap<EVBWeaponType, FVBWeaponVisualStyle> Styles;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Weapons")
	TMap<TObjectPtr<UAnimMontage>, FVBWeaponVisualTiming> MontageTimings;
	// RPC가 프로퍼티 복제보다 먼저 도착한 경우의 코스메틱 유예. 이후 복제 상태로 수렴한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Weapons", meta=(ClampMin="0"))
	float ReplicationGracePeriod = 0.5f;

	// 저작 문제 - 하나에 한 줄. 모양 파라미터를 이펙트가 안 받으면 Niagara 가 말없이 무시하므로 저장 때 잡는다.
	TArray<FText> FindProblems() const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
