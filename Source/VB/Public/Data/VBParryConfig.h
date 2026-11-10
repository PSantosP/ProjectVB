// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"
#include "GameplayTagContainer.h"
#include "Data/VBHitReactConfig.h" // EVBHitDirection - 받아넘김을 맞은 쪽으로 고른다
#include "VBParryConfig.generated.h"

class UAnimMontage;
class UChooserTable;
class UGameplayEffect;

/**
 * 한 무기의 가드 콘텐츠 한 벌(몽타주 + 섹션명).
 * 섹션명을 몽타주와 묶는다: 다른 무기 섹션명과 섞이면 Montage_SetNextSection 이 로그 없이 실패한다.
 */
USTRUCT(BlueprintType)
struct FVBGuardSet
{
	GENERATED_BODY()

	// 가드 진입과 유지 몽타주. 비어도 가드 판정은 성립한다(화면만 없다)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry")
	TObjectPtr<UAnimMontage> GuardMontage = nullptr;

	// 가드 자세로 저작한 Idle/Turn 선택기. 비어 있으면 기존 전신 가드를 유지한다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry")
	TObjectPtr<UChooserTable> GuardLocomotionChooser = nullptr;

	// 패링과 블록 성립 순간 재생하는 받아넘김(전신 DefaultSlot, 가드 몽타주를 덮는다). 블렌드인은 0.06s 로 저작한다(기본 0.25s 면 짧은 클립이 페이드인만 하다 끝난다)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry")
	TObjectPtr<UAnimMontage> ParryAcceptMontage = nullptr;

	// 오른쪽에서 온 공격의 받아넘김. 비면 ParryAcceptMontage
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry")
	TObjectPtr<UAnimMontage> ParryAcceptMontageRight = nullptr;

	// 가드 하체 선택표가 걷기까지 덮는가. false 면 멈춰 있을 때만 하체를 가드 자세로 둔다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry")
	bool bGuardLocomotionCoversMovement = false;

	// 움직이는 중에도 가드 하체를 쓰는가(순수)
	bool AllowsGuardLowerBody(bool bMoving) const { return !bMoving || bGuardLocomotionCoversMovement; }

	// 맞은 쪽에 맞는 받아넘김(순수)
	UAnimMontage* PickParryAccept(EVBHitDirection Side) const
	{
		return Side == EVBHitDirection::Right && ParryAcceptMontageRight ? ParryAcceptMontageRight.Get() : ParryAcceptMontage.Get();
	}

	// 가드 진입 섹션. 재생 직후 Loop 로 링크된다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry")
	FName GuardStartSection = FName("Default");

	// 가드 유지 섹션. 자기 자신으로 링크해 RMB 홀드 동안 반복한다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry")
	FName GuardLoopSection = FName("Loop");

	// 가드 해제 섹션(진입 때 물러난 만큼 복귀하는 루트모션 구간). next=None 이라 재생 후 자연 블렌드아웃
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry")
	FName GuardEndSection = FName("End");
};

/**
 * 무기와 무관한 가드/패링 전역 수치. 데미지 hub 가 읽으므로 피격자 AVBCharacter 가 소유한다.
 * 무기별 가드 콘텐츠(FVBGuardSet)는 UVBWeaponDefinition 에 있다.
 * 패링 적 경직은 히트리액트 이벤트(Event.Combat.Hit) 재사용으로 낸다.
 */
UCLASS(BlueprintType)
class VB_API UVBParryConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 패링 윈도우(초). 전역 값이라 무기별로 쪼개지 않는다(플레이어가 한 번 배워 쓰는 공정성 숫자)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry", meta=(ClampMin="0.0"))
	float ParryWindow = 0.15f;

	// 패링 성공 후 반격 입력을 받는 시간(초). 이 동안만 State.Combat.RiposteWindow 가 붙는다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry", meta=(ClampMin="0.0"))
	float RiposteWindow = 0.8f;

	// 패링 성립 순간 그 공격자로 락온할지(다른 대상에 락온 중이어도 바꾼다). 소비처는 AVBCharacter::OpenRiposteWindow 이고 서버에서 바로 건다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry")
	bool bAutoLockOnParry = true;

	// 패링 성공 프레임에 캐릭터와 카메라를 공격자 쪽으로 돌린다. 락온과는 별개다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry")
	bool bFaceAttackerOnParry = true;

	// 블록 1회당 소모 스태미나. 패링(무효화)에는 소모가 없다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry", meta=(ClampMin="0.0"))
	float StaminaCostOnBlock = 10.0f;

	// 패링 성공 보상 스태미나. 블록보다 크게 둔다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry", meta=(ClampMin="0.0"))
	float StaminaGainOnParry = 30.0f;

	// 블록 데미지 배수. 가드 중(윈도우 밖) 피격 시 곱한다. 70% 감소 = 0.30
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry", meta=(ClampMin="0.0", ClampMax="1.0"))
	float BlockDamageMultiplier = 0.30f;

	// 패링 성공 시 공격자에게 보내는 히트리액트 이벤트 크기(경직 payload)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry", meta=(ClampMin="0.0"))
	float StaggerHitReactMagnitude = 2.0f;

	// 패링이 공격자 기절 게이지에 쌓는 양. 공격 한 대보다 크게 둔다. 0 = 쌓지 않음
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry", meta=(ClampMin="0.0"))
	float ParryPoiseDamage = 40.0f;

	// 그 양을 싣는 즉시 효과(IncomingPoiseDamage += SetByCaller Data.PoiseDamage, Execution 없음)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry")
	TSubclassOf<UGameplayEffect> ParryPoiseEffect;

	// 패링 효과로 쓸 수 없는 이유, 쓸 수 있으면 빈 문자열. 피해 계산을 달면 패링한 쪽의 기본 피해가 공격자 체력에 들어간다
	static FString GetPoiseEffectProblem(TSubclassOf<UGameplayEffect> Effect);

	// Accept 재생 중 락아웃의 하한(초). 실제 락아웃 = max(Accept 길이, 이 값)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Parry", meta=(ClampMin="0.0"))
	float MinGuardAcceptLockout = 0.1f;

#if WITH_EDITOR
	// 패링 게이지 양이 있는데 그것을 싣는 효과가 비면 막는다(런타임에선 게이지가 안 차는 것으로만 보인다)
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
