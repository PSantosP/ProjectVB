#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AbilitySystem/VBAttributeSubscription.h"
#include "GameplayEffectTypes.h"
#include "Messages/VBGameplayMessageSubsystem.h"
#include "CharacterCommon/VBCombatChargeCredit.h"
#include "Types/VBWeaponStateTypes.h"
#include "Engine/EngineTypes.h"
#include "VBCinematicSkillChargeComponent.generated.h"

class UVBCinematicSkillChargeRules;
class UVBWeaponStateComponent;
class UVBPawnExtensionComponent;
class UVBAbilitySystemComponent;
class UVBSaveParticipantSubsystem;
class ACharacter;
class APlayerState;
class APawn;
struct FOnAttributeChangeData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVBCinematicSkillGaugeStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVBCinematicSkillReadyChanged, bool, bReady, AActor*, Avatar);

// 정산된 전투 기여를 GAS 효과로 바꾼다. 수치 자원은 갖지 않는다.
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBCinematicSkillChargeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBCinematicSkillChargeComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|CinematicSkill|Charge")
	TObjectPtr<const UVBCinematicSkillChargeRules> Rules;
	UPROPERTY(BlueprintAssignable, Category="VB|CinematicSkill|Charge")
	FVBCinematicSkillGaugeStateChanged OnGaugeStateChanged;
	UPROPERTY(BlueprintAssignable, Category="VB|CinematicSkill|Charge")
	FVBCinematicSkillReadyChanged OnReadyChanged;
	bool IsUnlocked() const;
	UFUNCTION(BlueprintPure, Category="VB|CinematicSkill|Charge")
	bool IsReady() const { return ReadyAvatar.IsValid(); }
	EVBWeaponType GetRequiredWeapon() const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleCredit(FGameplayTag Channel, const FVBCombatChargeCredit& Credit);
	void HandleAttributeChanged(const FOnAttributeChangeData& Data);
	void HandleTagChanged(FGameplayTag Tag, int32 Count);
	void HandleAbilityChanged(const FGameplayAbilitySpec& Spec);
	FGameplayAbilitySpecHandle FindReadySkill(UAbilitySystemComponent& ASC) const;
	void HandleAbilityRemoved(FGameplayAbilitySpecHandle Handle);
	void HandleAvatarInitialized(UVBAbilitySystemComponent& ASC);
	void HandleAvatarUninitialized(UVBAbilitySystemComponent& ASC, bool bAvatarSession);
	void RefreshReady();
	void ReleaseReady();
	void DisconnectPawn();
	UFUNCTION()
	void HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn);
	UFUNCTION()
	void HandleWeaponChanged(EVBLocomotionState State, EVBWeaponType Weapon);
	UFUNCTION()
	void HandleMovementModeChanged(ACharacter* Character, EMovementMode PreviousMode, uint8 PreviousCustomMode);

	TWeakObjectPtr<UAbilitySystemComponent> SourceASC;
	TWeakObjectPtr<UVBWeaponStateComponent> Weapons;
	TWeakObjectPtr<ACharacter> MovementSource;
	TWeakObjectPtr<UVBPawnExtensionComponent> PawnExtension;
	TWeakObjectPtr<UVBSaveParticipantSubsystem> SaveRegistry;
	TWeakObjectPtr<AActor> ReadyAvatar;
	FVBAttributeSubscription CurrentSubscription;
	FVBAttributeSubscription MaximumSubscription;
	FVBMessageListenerHandle CreditListener;
	FDelegateHandle TagHandle;
	FDelegateHandle OwnedWeaponsHandle;
	FDelegateHandle AbilityChangedHandle;
	FDelegateHandle AbilitiesReplicatedHandle;
	FDelegateHandle AbilityRemovedHandle;
	FDelegateHandle AvatarInitializedHandle;
	FDelegateHandle AvatarUninitializedHandle;
	FDelegateHandle SaveHandle;
	FActiveGameplayEffectHandle ReadyEffect;
	FGameplayAbilitySpecHandle ReadySkill;
	TArray<FGuid> RecentCredits;
	bool bEnding = false;
	bool bRefreshingReady = false;
	bool bAvatarSuspended = false;
	// 듣는 쪽에 마지막으로 알린 해금 상태 - HUD 는 스킬의 무기를 가진 뒤에만 게이지를 보인다.
	bool bWasUnlocked = false;
	bool bReportedNoMaximum = false;
};
