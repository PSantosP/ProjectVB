// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "Data/VBMedicalConfig.h"
#include "VBGA_MedicalAbilityBase.generated.h"

/**
 * 의료 행위(정화/처형) GA 의 베이스. 처형 창이 열린 대상에 즉사 피해 + 명성 변화를 준다.
 * 파생(Purify/Execute)은 명성 방향만 다르다. GE 적용은 서버에서만 한다.
 */
UCLASS(Abstract)
class VB_API UVBGA_MedicalAbilityBase : public UVBGameplayAbility
{
	GENERATED_BODY()
public:
	// 대상이 없으면 활성화하지 않는다. CommitAbility 보다 먼저 불려 헛발이 쿨다운을 태우지 않는다.
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	// 지금 의료 행위를 걸 수 있는 대상(락온 우선, 아니면 전방 원뿔). 없거나 설정이 비면 nullptr. 부작용 없음.
	AActor* FindMedicalTarget(const FGameplayAbilityActorInfo* ActorInfo) const;
	bool HasMedicalConfig() const { return MedicalConfig != nullptr; }

protected:
	virtual void ExecuteAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	UPROPERTY(EditDefaultsOnly, Category="VB|Medical")
	TObjectPtr<UVBMedicalConfig> MedicalConfig;

	void PerformMedicalTrace(const FGameplayAbilityActorInfo* ActorInfo);

	// 이미 정해진 대상에 의료 효과(피해 + 명성)를 적용한다. 권위 가드는 이 함수가 든다.
	// 처형 시작 때 잡은 대상에 그대로 적용해야 끝날 때 다른 적이 죽지 않는다.
	void ApplyMedicalEffects(const FGameplayAbilityActorInfo* ActorInfo, AActor* Target);
};
