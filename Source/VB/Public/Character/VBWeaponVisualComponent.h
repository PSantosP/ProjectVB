#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/VBWeaponVisualConfig.h"
#include "GameplayTagContainer.h"
#include "VBWeaponVisualComponent.generated.h"

class UAnimInstance;
class AVBCharacter;
class AVBSummonedWeapon;
class UVBCharacterAppearanceComponent;
class UVBWeaponStateComponent;
class UAbilitySystemComponent;
class UMeshComponent;

struct FVBTrailMeshSource
{
	TWeakObjectPtr<UMeshComponent> Mesh;
	FName FirstSocket;
	FName SecondSocket;
};

UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBWeaponVisualComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBWeaponVisualComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Weapons") TObjectPtr<UVBWeaponVisualConfig> VisualConfig;
	// 재생 성공한 인스턴스만 동기화한다. nullptr은 미재생 폴백이며 이전 전환도 취소한다.
	void BeginWeaponTransition(EVBWeaponType ToType, UAnimInstance* AnimInstance, UAnimMontage* Montage);
	// 무기 상태를 바꾸지 않고 외형을 숨기는 토큰을 발급한다.
	FGuid BeginPresentationHide();
	// 자신이 받은 토큰만 해제하며 현재 패시브 조건으로 외형을 복구한다.
	bool EndPresentationHide(FGuid Token);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// 패시브가 걸린 외형이 지금 보이나(순수) - 캐릭터의 ASC 가 가진 태그로 묻는다.
	static bool IsPartShown(const FVBWeaponVisualPart& Part, const FGameplayTagContainer& OwnedTags);
	// 판정이 고른 외형 파트나 보이는 메시에서 실제 궤적 소켓을 찾는다.
	bool ResolveTrailSource(int32 VisualPartIndex, FName FirstSocket, FName SecondSocket, FVBTrailMeshSource& OutSource) const;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBWeaponPresentationTestAccess;
#endif
	TSet<FGuid> PresentationHolds;
	void RetireVisualTrailSources();
	void DestroyVisuals();
	void CreateVisuals(EVBWeaponType Type);
	// 칼은 보이는 손에 붙인다 - 겉모습이 있으면 그 주 메시의 대응 소켓, 없으면 본체(UVBCharacterAppearanceComponent::ResolveAttachTarget).
	//   판정(UVBMeleeTraceComponent)은 본체 뼈를 그대로 읽는다.
	void AttachVisualToHand(AVBSummonedWeapon* Visual) const;
	// 메시 없는 부품이 모양을 가져올 지금 보이는 메시. 외형이 바뀌면 다시 묻는다.
	USkeletalMeshComponent* ResolveShapeBody() const;
	// 겉모습이 바뀌면 지금 든 칼을 새 손에 다시 붙인다. 무기 종류가 같으면 CreateVisuals 가 다시 안 불려 옛 손에 남는다(review-codex.md F3).
	void HandleAppearanceChanged(UVBCharacterAppearanceComponent* InAppearance);
	void ApplyVisual(EVBWeaponType Type, float NewProgress);
	void ResetTransition();
	bool UpdateTransition();
	// 패시브가 걸린 외형(주운 방패 등)의 보임을 지금 ASC 태그에 맞춘다 - 생성 직후와 그 태그가 바뀔 때.
	void RefreshGatedParts();
	void OnGatedTagChanged(const FGameplayTag Tag, int32 NewCount);
	void UnbindGatedTags();
	UAbilitySystemComponent* GetBodyAbilitySystem() const;
	// 지금 외형이 듣는 패시브 태그 구독.
	TArray<TPair<FGameplayTag, FDelegateHandle>> GatedTagHandles;
	TWeakObjectPtr<UAbilitySystemComponent> GatedTagASC;
	UPROPERTY(Transient) TObjectPtr<AVBCharacter> Character;
	UPROPERTY(Transient) TObjectPtr<UVBWeaponStateComponent> WeaponState;
	UPROPERTY(Transient) TArray<TObjectPtr<AVBSummonedWeapon>> Visuals;
	TWeakObjectPtr<UVBCharacterAppearanceComponent> Appearance;
	FDelegateHandle AppearanceChangedHandle;
	TWeakObjectPtr<UAnimInstance> TransitionAnimInstance;
	int32 TransitionInstanceId = INDEX_NONE;
	FVBWeaponVisualTiming TransitionTiming;
	EVBWeaponType TransitionFrom = EVBWeaponType::None;
	EVBWeaponType TransitionTo = EVBWeaponType::None;
	float TransitionStartProgress = 0.0f;
	float ReplicationGraceRemaining = 0.0f;
	EVBWeaponType VisibleType = EVBWeaponType::None;
	float Progress = 0.0f;
	float FormationDuration = 0.65f;
	float DissolveDuration = 0.25f;
};
