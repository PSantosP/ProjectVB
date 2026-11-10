// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/VBWeaponStateTypes.h"
#include "Character/VBWeaponStateRules.h"
#include "Components/ActorComponent.h"
#include "Messages/VBGameplayMessageSubsystem.h" // FVBMessageListenerHandle - 값 멤버
#include "AbilitySystem/VBAbilitySet.h"           // FVBAbilitySetGrantedHandles - 값 멤버
#include "VBWeaponStateComponent.generated.h"

class AVBCharacter;
class UAbilitySystemComponent;
class UVBWeaponStateConfig;
class UVBFinisherConfig;
class UVBWeaponDefinition;
class UVBEnhancementComponent;
struct FVBEnhanceTarget;
struct FVBHitReceivedMessage;

DECLARE_MULTICAST_DELEGATE(FVBOnOwnedWeaponsChanged);

// 캐릭터가 지금 어떤 무기 상태인지의 서버 정본과 그 복제, 바꾸는 단일 입구를 맡는다.
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VB_API UVBWeaponStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBWeaponStateComponent();
	// 플레이어 캐릭터만 가진다. 없으면 무기가 없는 액터(적)다.
	static UVBWeaponStateComponent* FindFor(const AActor* Actor);
	
	FORCEINLINE EVBLocomotionState GetLocomotionState() const { return LocomotionState; }
	FORCEINLINE EVBWeaponType GetCurrentWeaponType() const { return CurrentWeaponType; }
	// Tab 재발도 무기: 선택 무기 -> 마지막 사용 무기 -> 기본(가진 것만, 없으면 가진 것 중 첫째 또는 None).
	EVBWeaponType GetResummonWeaponType() const;

	// 가진 무기 - 꺼내는 길 셋과 복원이 모두 ServerSummonWeapon 에서 묻는다. 시작값은 맵의 AVBStartingLoadout, 없으면 정의가 있는 무기 전부.
	bool IsWeaponOwned(EVBWeaponType InWeaponType) const;
	const TArray<EVBWeaponType>& GetOwnedWeapons() const { return OwnedWeapons; }

	// 서버에서만. 모르는 무기(정의 없음), None, 클라 호출은 Failed.
	EVBWeaponGrantResult GrantWeapon(EVBWeaponType InWeaponType);
	// 서버에서만. 가진 무기를 거둔다(손에 들었으면 먼저 넣는다). 가지고 있었으면 true.
	bool RevokeWeapon(EVBWeaponType InWeaponType);

	// 저장 복원용(서버). 목록을 통째로 바꾼다 - 모르는 무기는 버리고 경고한다.
	void SetOwnedWeapons(const TArray<EVBWeaponType>& InWeapons);

	// 서버에서는 바꾼 직후, 소유 클라에서는 복제가 왔을 때.
	FVBOnOwnedWeaponsChanged OnOwnedWeaponsChanged;
	// 술어 정본: 무기On = ArmedCombat 뿐 / 전투 = Unarmed 가 아닌 모든 상태 (상태2, 3).
	FORCEINLINE bool IsWeaponSummoned() const { return LocomotionState == EVBLocomotionState::ArmedCombat; }
	FORCEINLINE bool IsInCombat() const { return LocomotionState != EVBLocomotionState::Unarmed; }
	FORCEINLINE bool IsTempSheathed() const { return bIsTempSheathed; }

	// 그 무기의 정의. 맨손(None)이나 이 캐릭터가 모르는 무기는 nullptr.
	const UVBWeaponDefinition* FindDefinition(EVBWeaponType InWeaponType) const;
	// 선택된 무기(GetCurrentWeaponType - 칼집에 넣어도 전투 중이면 남는다)의 정의.
	const UVBWeaponDefinition* GetSelectedDefinition() const { return FindDefinition(CurrentWeaponType); }
	// 숫자 키 N(Input.WeaponSlot.N)에 놓인 가진 무기의 정의. 없으면 nullptr. 가진 무기 둘이면 앞의 것 + 경고.
	const UVBWeaponDefinition* FindDefinitionForSlot(int32 Slot) const;
	// 이 캐릭터가 아는 무기 정의들, 저작 순서 그대로(무기 휠이 이 순서로 보인다).
	const TArray<TObjectPtr<const UVBWeaponDefinition>>& GetWeaponDefinitions() const { return WeaponDefinitions; }

	// 이 상태에서 쓸 로코모션 MM 풀(현재 무기의 Armed/SheathedMMPool, None = 기본 풀).
	//   상태를 인자로 받는다 - 소비처가 앉기 리다이렉트 전의 값을 들고 있다.
	EVBWeaponType GetMMPoolForState(EVBLocomotionState InState) const;

	// 현재 무기의 처형 목록. 무기 미등록이면 nullptr, 등록됐지만 비었으면 빈 배열(경고가 다르다).
	const TArray<TObjectPtr<UVBFinisherConfig>>* GetFinisherConfigs() const;

	// 등록된 모든 무기의 처형 목록(처형 색인 미리 만들기용).
	void GetAllFinisherConfigs(TArray<const UVBFinisherConfig*>& OutConfigs) const;

	// 로코모션이 따를 상태 - 파쿠르 임시수납 중에는 무기가 없는 것으로 본다(애니와 속도가 같은 상태를 믿게 한 곳에서).
	FORCEINLINE EVBLocomotionState GetEffectiveLocomotionState() const
	{
		return bIsTempSheathed ? EVBLocomotionState::Unarmed : LocomotionState;
	}

	// 지금 써야 할 MM 풀 - 재생 클립과 이동 속도 프로파일을 함께 정한다(따로 정하면 보폭과 클립이 어긋난다).
	FORCEINLINE EVBWeaponType GetMMPool() const { return GetMMPoolForState(GetEffectiveLocomotionState()); }

	// 발도 - 무기On = 전투 자세라 ArmedCombat 으로 바로 간다(비전투+무기On 상태는 없다).
	UFUNCTION(BlueprintCallable, Category="VB|Weapon")
	void SummonWeapon(EVBWeaponType InWeaponType);

	// 납도 - 무기만 넣고 전투는 유지한다. 전투 이탈은 TryCombatExit 타이머.
	UFUNCTION(BlueprintCallable, Category="VB|Weapon")
	void SheatheWeapon();

	// 전투 자극(락온, 피격 등) - 발도 없이 전투 진입, 이미 전투면 타이머 리셋(서버에서만).
	UFUNCTION(BlueprintCallable, Category="VB|Weapon")
	void EnterCombat();
	
	// 전투 타이머 리셋(공격/피격마다 호출)
	void ResetCombatTimer();

	// 전투 이탈 타이머 영구 정지(사망) - 게임오버 중에도 타이머가 돌아 시체 손에서 수납 이펙트가 터지지 않게.
	void StopCombatTimer();
	
	// 파쿠르 임시수납 (VBGA_Traversal에서 호출)
	UFUNCTION(BlueprintCallable, Category="VB|Weapon")
	void TempSheatheForParkour();
	
	// 파쿠르 후 재소환(VBGA_Traversal 완료 콜백)
	UFUNCTION(BlueprintCallable, Category="VB|Weapon")
	void RestoreFromTempSheathe();
	
	// 그 무기를 꺼내 둔다(이미 손에 무기가 있으면 그대로) - 저장 복원이 부른다.
	void EnsureWeaponSummoned(EVBWeaponType InWeaponType);

	// 서버. 손이 비었으면 꺼낼 무기의 세트에서 이 입력의 능력이 지금 시작할 수 있을 때만 꺼낸다(막힌 누름이 무기만 꺼내지 않게).
	EVBPressDraw DrawForPress(const FGameplayTag& InputTag);

	// 아바타 회차의 시작, 끝(서버, Pawn 이 부른다). ASC 는 PlayerState 에 있어 Pawn 보다 오래 유지되므로 손에 든 무기의 세트를 다시 부여하거나 회수한다.
	void BeginAvatarSession();
	void EndAvatarSession();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
		FOnLocomotionStateChanged,
		EVBLocomotionState, NewState,
		EVBWeaponType, NewWeaponType);

	UPROPERTY(BlueprintAssignable, Category="VB|Weapon")
	FOnLocomotionStateChanged OnLocomotionStateChanged;

protected:
	// 가진 무기의 시작값 - BeginPlay 가 아닌 이유: 이어하기가 그 전에 저장을 적용한다.
	virtual void InitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	// 전투 진입/이탈/수납 타이밍 튜닝 DataAsset. 미할당 시 CDO 디폴트 fallback (GetWeaponStateConfig).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Weapon|Combat")
	TObjectPtr<UVBWeaponStateConfig> WeaponStateConfig;

	// WeaponStateConfig 안전 접근자 - 미할당이면 클래스 CDO 디폴트 반환(절대 null 아님).
	const UVBWeaponStateConfig* GetWeaponStateConfig() const;

	// 적 감지 (구조적 - 트레이스 채널은 튜닝값 아님). 락온 탐색은 UVBTargetLockConfig::SearchTraceChannel 을 따로 쓴다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Weapon|Combat")
	TEnumAsByte<ECollisionChannel> EnemyTraceChannel = ECC_Pawn;
	
	// 이 캐릭터가 알 수 있는 무기들. 무기 종류마다 하나 - 무기별 데이터는 전부 정의 안에 있다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Weapon|Data")
	TArray<TObjectPtr<const UVBWeaponDefinition>> WeaponDefinitions;

	// 무기 최초 소환 시 기본 타입 (이전 보유 무기가 없을 때 사용)
	UPROPERTY(EditDefaultsOnly, Category="VB|Weapon|Data")
	EVBWeaponType DefaultWeaponType = EVBWeaponType::Blade;

	// 마지막으로 발도한 무기 - 평상 복귀 뒤 Tab 재발도가 기본 무기로 새지 않게(서버에서만 읽어 비복제).
	EVBWeaponType LastUsedWeaponType = EVBWeaponType::None;
	
	// 무기 전환 연기 멀티캐스트(몽타주 + 소환 VFX, 코스메틱) - GA 를 안 거치는 전환 경로까지 한 곳에서 덮는다.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayWeaponChangeActing(EVBWeaponType FromType, EVBWeaponType ToType, bool bCombatStance);

	// Server RPC
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSummonWeapon(EVBWeaponType InWeaponType);
	
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSheatheWeapon();

	UPROPERTY(ReplicatedUsing=OnRep_LocomotionState)
	EVBLocomotionState LocomotionState = EVBLocomotionState::Unarmed;
	
	UPROPERTY(ReplicatedUsing=OnRep_LocomotionState)
	EVBWeaponType CurrentWeaponType = EVBWeaponType::None;

	// 가진 무기. 소유 클라에만 복제한다(무기 바퀴가 읽는다). 쓰기는 서버만.
	UPROPERTY(ReplicatedUsing=OnRep_OwnedWeapons)
	TArray<EVBWeaponType> OwnedWeapons;
	
	// 아래 셋은 GC, 복제 내부 상태. 외형은 원격 클라에서도 파쿠르 중 숨긴다.
	UPROPERTY(ReplicatedUsing=OnRep_LocomotionState)
	bool bIsTempSheathed = false;

	// 파쿠르 전 상태 기억 파쿠르 후 복원하기 위함
	UPROPERTY()
	EVBLocomotionState StateBeforeTempSheathe = EVBLocomotionState::Unarmed;

	// 파쿠르 전 무기 타입 기억 파쿠르 후 복원하기 위함
	UPROPERTY()
	EVBWeaponType WeaponTypeBeforeTempSheathe = EVBWeaponType::None;

private:
	friend struct FVBMPBaseWeaponAccess;
	// 권위 쪽 변경은 한 번 알린다. 클라에서는 복제 property 가 여러 번 적용될 수 있다.
	void ApplyLocalLocomotionState();

	// 무기가 손에서 바뀌거나 치워지면 가드를 끊는다(발도, 교체 / 납도 / 임시수납, 서버). 가드 GA 의 AssetTags 로 취소한다.
	void CancelGuardAbility();

	// 로코모션 상태 변경 시 클라이언트에서 델리게이트 브로드 캐스트
	UFUNCTION()
	void OnRep_LocomotionState();

	UFUNCTION()
	void OnRep_OwnedWeapons();
	
	// 상태 전환의 단일 진입점이다.
	void SetLocomotionState(EVBLocomotionState NewState);
	
	// 전투 이탈 타이머 (재)무장 - 재무장 지점이 다섯이라 지연의 출처를 한 곳에 둔다. 정지(StopCombatTimer)는 따로다.
	void ArmCombatExitTimer();

	// 타이머 콜백. 판단은 VBWeaponStateRules 가 하고, 떠나면 평상으로 돌아가 수납 연기를 낸다.
	void TryCombatExit();

	// 전투 이탈 판단에 넘길 사실을 세계에서 모은다.
	FVBCombatExitFacts GatherCombatExitFacts() const;

	// 서버. 이 캐릭터가 맞았으면 전투 자극이다(막은, 패링한 타격 포함) - 버스 Message.Combat.HitReceived 를 BeginPlay 에서 구독한다.
	void HandleHitReceived(FGameplayTag Channel, const FVBHitReceivedMessage& Hit);
	FVBMessageListenerHandle HitListener;
	
	// EnemyProximityRadius 안의 적 검사.
	bool AreEnemiesNearby() const;

	// 서버. 손에 든 무기의 능력 세트(장착 태그 효과 포함)를 준다 / 거둔다 - 주기는 앞 무기의 것을 먼저 거둔다(SourceObject = 무기 정의).
	void GiveWeaponSet(EVBWeaponType InWeaponType);
	void TakeWeaponSet();
	FVBAbilitySetGrantedHandles WeaponSetGrant;

	// 서버. 손에 든 무기의 강화 배수를 WeaponDamageScale 에 건다(앞의 것은 뺀다) - 세트와 함께 주고 거두며, 단계가 바뀌면 이것만 다시 건다.
	void ApplyWeaponEnhancement();
	// 이 컴포넌트를 가진 캐릭터의 ASC(PlayerState 의 것). 빙의 전이면 null.
	UAbilitySystemComponent* FindOwnerAbilitySystem() const;
	void RemoveWeaponEnhancement();
	void HandleEnhancementChanged(const FVBEnhanceTarget& Target);
	FActiveGameplayEffectHandle WeaponEnhancementHandle;
	TWeakObjectPtr<UVBEnhancementComponent> BoundEnhancement;
	FDelegateHandle EnhancementChangedHandle;

	// 전투 이탈 시도 타이머.
	FTimerHandle CombatExitTimerHandle;

	// 캐싱 (GC-only)
	UPROPERTY()
	TWeakObjectPtr<AVBCharacter> OwnerCharacter;
};
