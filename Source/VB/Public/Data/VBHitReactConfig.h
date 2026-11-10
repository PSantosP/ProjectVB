// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"
#include "UObject/ObjectKey.h"
#include "Types/VBWeaponStateTypes.h"
#include "VBHitReactConfig.generated.h"

class UAnimMontage;
class UGameplayEffect;

// 어디서 맞았나(피격자 기준). 4분면 - 앞, 뒤, 왼, 오른. 히트리액트 몽타주 택일의 키다.
UENUM(BlueprintType)
enum class EVBHitDirection : uint8
{
	Front,
	Back,
	Left,
	Right
};

// 공격자가 피격자 기준 어느 분면에 있나 - 피격과 경직이 같은 답을 보도록 홈을 하나로 둔다. 넉백 요가 아니라 맞은 위치를 본다
// Instigator 가 없으면 Front - 무방향 자산과 같은 결과라 새 실패 모드를 안 만든다.
VB_API EVBHitDirection VBComputeHitDirection(const AActor* Victim, const AActor* Instigator);

// 피격 이벤트의 Instigator 를 위치를 가진 액터로 해석한다 - 플레이어가 때리면 Instigator 가 PlayerState(위치 원점)라서다. 못 찾으면 null.
VB_API const AActor* VBResolveInstigatorBody(const AActor* Instigator);

// 맞는 캐릭터의 무기 스탠스. 무기 상태가 없는 캐릭터는 None이다.
VB_API EVBWeaponType VBResolveVictimStance(const AActor* Victim);

// 캐릭터 하나의 피격 클립 고르기 상태. 난수가 아닌 순번이라 서버와 소유 클라가 각자 골라도 같은 클립이 나온다(ServerInitiated).
// 번호가 아니라 직전에 튼 몽타주를 기억한다 - 번호면 방향을 번갈아 맞을 때 한 방향이 늘 같은 클립이 된다.
struct FVBHitReactPick
{
	int32 Sequence = 0;
	TObjectKey<UAnimMontage> LastMontage;
};

// 후보 NumOptions 개 중 하나. 직전 번호는 다시 고르지 않는다(후보가 둘 이상일 때). 같은 입력이면 같은 답.
VB_API int32 VBPickHitReactIndex(int32 NumOptions, int32 Sequence, int32 LastIndex);

// FVBHitReactSet - 한 스탠스의 히트리액트 묶음. 방향, 대체 클립을 더할 자리라 struct 로 둔다 (TMap 값 타입이 바뀌면 직렬화된 항목이 사라진다)
USTRUCT(BlueprintType)
struct VB_API FVBHitReactSet
{
	GENERATED_BODY()

	// 무방향 기본 히트리액트. 방향별 몽타주가 비면 이것으로 폴백한다(AM_HitReaction 이 여기 들어간다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	// 방향별 히트리액트 - 방향은 피격자 기준 공격자의 위치다. 비면 Montage 로 폴백한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> MontageFront = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> MontageBack = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> MontageLeft = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> MontageRight = nullptr;

	// 같은 칸의 대체 클립 - 기본 클립과 함께 후보가 된다. 방향 칸의 기본 클립이 비면 무방향(Montage + Alternates)으로 내려간다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TArray<TObjectPtr<UAnimMontage>> Alternates;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TArray<TObjectPtr<UAnimMontage>> FrontAlternates;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TArray<TObjectPtr<UAnimMontage>> BackAlternates;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TArray<TObjectPtr<UAnimMontage>> LeftAlternates;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TArray<TObjectPtr<UAnimMontage>> RightAlternates;

	// 방향 몽타주가 있으면 그것, 없으면 무방향 Montage. Pick 이 있으면 대체 클립까지 후보로 놓고 그것으로 고른다.
	UAnimMontage* ResolveByDirection(EVBHitDirection Direction, FVBHitReactPick* Pick = nullptr) const;

	// 이 세트의 어느 칸(대체 클립 포함)에든 들어 있는 몽타주인가.
	bool Contains(const UAnimMontage* InMontage) const;
	void FindProblems(TArray<FText>& OutProblems) const;
};

// 다운 한 번에 쓰는 클립 묶음(한 팩에서 온 넘어짐, 대기, 기상, 띄움). 묶음을 섞지 않아 자세가 이어진다.
USTRUCT(BlueprintType)
struct VB_API FVBKnockdownClips
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> Fall = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> DownIdle = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> GetUp = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> Launch = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> AirLoop = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> Land = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> LaunchGetUp = nullptr;

	UAnimMontage* GetUpFor(bool bAfterLaunch) const { return bAfterLaunch && LaunchGetUp ? LaunchGetUp.Get() : GetUp.Get(); }
	// 이 묶음만으로 땅 다운 / 띄움 다운을 끝까지 돌 수 있나(대기 클립은 둘 다 필수).
	bool CoversGround() const { return Fall && DownIdle && GetUp; }
	bool CoversLaunch() const { return Launch && AirLoop && Land && DownIdle && GetUp; }
	bool CoversPath(bool bLaunch) const { return bLaunch ? CoversLaunch() : CoversGround(); }
	bool IsEmpty() const { return !Fall && !DownIdle && !GetUp && !Launch && !AirLoop && !Land && !LaunchGetUp; }
	bool Contains(const UAnimMontage* Montage) const;
};

// FVBKnockdownSet - UVBGA_Knockdown 이 읽는 다운 콘텐츠와 수치. 맞은 쪽 반응의 홈이 하나여야 적 BP 가 자산 하나만 꽂는다
// IsAuthored() 는 넘어짐과 기상이 다 있어야 참이다 - 아니면 서 있는 경직으로 떨어져 못 일어나는 상태를 만들지 않는다
USTRUCT(BlueprintType)
struct VB_API FVBKnockdownSet
{
	GENERATED_BODY()

	// 넘어지는 클립(보스: Hit_To_Down, 앞으로 엎어진다). 루트모션 그대로.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> FallMontage = nullptr;

	// 엎드린 대기. 끝나면 GA 가 다시 튼다 - 자산에 루프 절을 파지 않는다.
	// 앞 몽타주가 빠지는 중에 틀리므로 즉시 무게 1 로 들어와야 한다 - Inertialization(권장, ABP 슬롯 뒤에 관성 노드 필요) 또는 블렌드인 0
	//   표준 블렌드면 두 무게 합이 1 아래로 내려가 서 있는 자세가 섞여 쓰러진 캐릭터가 뜬다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> DownIdleMontage = nullptr;

	// 엎드린 채 맞을 때의 방향별 반응. 방향 판정은 서 있을 때와 같은 함수(VBComputeHitDirection). 비면 반응 없음(발광만).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	FVBHitReactSet DownHitSet;

	// 일어나는 클립(보스: Down_To_Stand, 뒤로 물러나며 선다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> GetUpMontage = nullptr;

	// 엎드려 있는 시간(초). 넘어짐, 기상 클립 길이는 여기 안 든다 - 그 둘은 자산이 정한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float DownHoldSeconds = 4.5f;

	// 넘어지는 클립의 이 시각(초) 이후 피격은 넘어짐을 끊고 DownHitSet 으로 받는다. 음수 = 넘어지는 동안은 안 받는다.
	// 넘어짐 클립이 엎드린 대기 자세에 닿는 시각을 재서 넣는다 - 그 전에 끊으면 메시가 튄다. 끊긴 시간은 대기에 더해 다운 길이를 지킨다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	float FallHitAcceptSeconds = -1.0f;

	// 기상 뒤 이 시간 안의 기절 게이지 소진은 서 있는 경직(StaggerSet)이 된다. 0 = 매번 넘어진다.
	//   넘어뜨리는 타격(Event.Combat.KnockedDown)에는 걸리지 않는다 - 그것은 공격 등급이 이미 고른 반응이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float CooldownSeconds = 25.0f;

	// 기절 게이지 소진(PoiseBroken)이 넘어뜨리나. true = 보스 그로기, false = 서 있는 경직이고 넘어짐은 강타로만 (병사, 감염체)
	//   GA 를 둘로 나누지 않는다 - 한 이벤트에 GA 둘을 걸면 둘 다 돈다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	bool bFallOnPoiseBreak = true;

	// 넘어져 있는 동안 자기에게 거는 GE(Infinite). 받는 피해 배수(UVBCombatAttributeSet::DamageTakenScale)의 값은 이 자산이 든다.
	//   비면 배수 없음 - 그래도 다운은 성립한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TSubclassOf<UGameplayEffect> VulnerabilityEffect;

	// 엎드린 채 휘두르는 반격 GA 의 InputTag. 비면 반격 없음. 켜는 쪽은 UVBGA_Knockdown(BT 는 이 동안 닫혀 있다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(Categories="Input"))
	FGameplayTag DownAttackInputTag;

	// 표적이 이 거리(cm, 수평) 안이면 반격한다. 0 = 반격 없음. 반격 클립의 손끝 반경을 재서 채운다(이름을 믿지 않는다).
	//   애니 고유(배율 1) 단위다 - 읽는 자리(UVBGA_Knockdown::TickDownAttackWatch)가 보폭 배율(VBStrideScale::Resolve)을 곱한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float DownAttackTriggerRadius = 0.0f;

	// 반격과 반격 사이 최소 간격(초). 0 이면 연속 반격으로 대기가 사라진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float DownAttackIntervalSeconds = 2.0f;

	// 반격 감시 주기(초). 이 간격으로 표적 거리를 본다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.01"))
	float DownAttackWatchSeconds = 0.2f;

	// 착지 감시 주기(초). 착지 클립이 늦게 들어가면 바닥에서 공중 자세가 보여 반격 감시보다 촘촘하다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.01"))
	float AirWatchSeconds = 0.05f;

	// 이 시간(초) 안에 안 뜨면 띄움이 없었던 것으로 본다(공격의 띄움 높이 0). 연출 값이 아니라 안전장치다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float AirTakeoffGraceSeconds = 0.25f;

	// 반격이 저작돼 있으면 true.
	bool HasDownAttack() const { return DownAttackInputTag.IsValid() && DownAttackTriggerRadius > 0.0f; }

	// 띄우는 타격(Launch)의 공중 흐름: LaunchMontage -> AirLoopMontage -> LandMontage -> 엎드린 대기 -> 기상. 셋 중 하나라도 비면 FallMontage 로 간다.
	//   띄우는 높이는 공격 쪽이 정하고 이 자산은 동작 클립만 가진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> LaunchMontage = nullptr;

	// 체공 반복. 끝나면 GA 가 다시 튼다 - DownIdleMontage 와 같은 이유로 자산에 루프 절을 파지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> AirLoopMontage = nullptr;

	/** 공중 내려찍기 적중부터 바닥에 눕기까지의 반응 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> AirSlamMontage = nullptr;

	// 땅에 닿아 엎드린 자세까지. 끝나면 엎드린 대기로 간다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> LandMontage = nullptr;

	// 떠 있는 동안 맞을 때의 방향별 반응. 끝나면 체공 반복으로 돌아온다. 비면 반응 없음(발광만). 더 띄우지는 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	FVBHitReactSet AirHitSet;

	// 착지 감지가 안 와도 이 시간(초)이 지나면 착지로 전환한다 - 어딘가 걸린 캐릭터가 공중 자세로 남지 않게.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.1"))
	float MaxAirSeconds = 2.5f;

	// 띄워져 떨어진 뒤의 빠른 기상(Fighter Get_Up). 띄움 세 클립(IsLaunchAuthored)이 있을 때만 쓰이고, 비면 GetUpMontage 와 DownHoldSeconds.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> LaunchGetUpMontage = nullptr;

	// LaunchGetUpMontage 가 있을 때 띄워진 뒤 엎드려 있는 시간(초).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float LaunchDownHoldSeconds = 0.5f;

	// 고른 묶음 기준의 엎드림 시간 - 그 묶음에 빠른 기상이 있을 때만 짧은 시간.
	float HoldSecondsFor(const FVBKnockdownClips& Clips, bool bAfterLaunch) const
	{
		return bAfterLaunch && Clips.LaunchGetUp ? LaunchDownHoldSeconds : DownHoldSeconds;
	}

	// 엎드린 채 회피로 빠져나오는 기상(플레이어). 비면 빠져나올 수 없다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> EscapeGetUpMontage = nullptr;

	// 엎드린 뒤 이 시간(초)이 지나야 빠져나올 수 있다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float MinHoldSeconds = 0.4f;

	// 지금 빠져나올 수 있나(순수). 경계값도 통과하도록 float 정밀도로 비교한다 - double 0.4 는 float 0.4 보다 작다.
	bool CanEscape(bool bHolding, double HeldSeconds) const
	{
		return bHolding && EscapeGetUpMontage && static_cast<float>(HeldSeconds) >= MinHoldSeconds;
	}
	// 떠 있는 동안 맞은 한 대가 남은 체공에 더하는 시간(초). 0 = 늘리지 않는다(공중 콤보가 없던 동작).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float AirHitExtendSeconds = 0.0f;

	// 늘려도 넘지 않는 체공 총시간(초). MaxAirSeconds 보다 작으면 MaxAirSeconds 가 뚜껑이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float MaxAirTotalSeconds = 0.0f;

	// 떠 있는 동안 맞으면 위로 이 속도(cm/s)까지는 받친다 - 공중 연타 동안 떨어지지 않게. 0 = 받치지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float AirHitHoldUpSpeed = 0.0f;

	// 공중 피격 한 대 뒤의 체공 한도.
	float ExtendAirBudget(float Budget) const
	{
		return FMath::Min(Budget + AirHitExtendSeconds, FMath::Max(MaxAirSeconds, MaxAirTotalSeconds));
	}

	// 다른 팩의 클립 묶음. 다운마다 그 길(땅 / 띄움)을 끝까지 덮는 묶음 중 하나를 고른다(위의 클립 칸이 0번). 비면 위의 칸만.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TArray<FVBKnockdownClips> ClipVariants;

	// 위의 클립 칸을 묶음 하나로(0번 묶음).
	FVBKnockdownClips OwnClips() const;
	// 이 길을 끝까지 덮는 묶음 목록. 위의 칸과 다른 묶음이 같은 규칙(CoversPath)을 따른다.
	TArray<FVBKnockdownClips> ClipsFor(bool bLaunch) const;
	bool HasPath(bool bLaunch) const;

	bool IsAuthored() const { return HasPath(false); }
	bool IsLaunchAuthored() const { return HasPath(true); }

	// 저작 문제 - 반쯤 채운 묶음은 어느 길에서도 조용히 빠지므로 저장 때 잡는다.
	void FindProblems(TArray<FText>& OutProblems) const;
};

// UVBHitReactConfig - 피격자 스탠스 -> 히트리액트 묶음 데이터 홈. 맞은 쪽의 반응이라 공격자가 아닌 피격자가 소유한다
// GameplayCueNotify_Static 은 CDO 로 돌아 상태를 못 가지므로 데이터는 외부 DataAsset 이다
// 미등록 스탠스, 미할당 몽타주는 DefaultSet 으로 폴백한다 (무기 상태가 없는 적은 늘 None)
UCLASS(BlueprintType)
class VB_API UVBHitReactConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 스탠스 미등록/미할당 시 항상 이것. AM_HitReaction 이 여기 들어간다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	FVBHitReactSet DefaultSet;

	// 피격자의 현재 스탠스별 오버라이드. 미등록 = DefaultSet 폴백.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TMap<EVBWeaponType, FVBHitReactSet> StanceOverrides;

	// 경직(Poise break) 반응 콘텐츠. 자산을 따로 가르면 적 BP 가 둘을 꽂다가 한쪽이 빠지므로 여기 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	FVBHitReactSet StaggerSet;

	// 무거운 피격의 기본 묶음. 비면 호출부가 경고 후 가벼운 반응을 고른다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	FVBHitReactSet HeavySet;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TMap<EVBWeaponType, FVBHitReactSet> HeavyStanceOverrides;

	// 가산 플린치 - 전신 피격 반응을 재생하지 못하는 구간(넘어지는 중, 일어나는 중)에서 지금 동작 위에 상체만 움찔하게 얹는다
	//   전부 가산 몽타주여야 하고(아니면 UVBGA_Knockdown::PlayFlinch 가 거절), ABP 에 DefaultSlot 뒤 가산 슬롯 노드가 있어야 한다. 비면 발광만.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	FVBHitReactSet FlinchSet;

	// 히트리액트 몽타주 재생 속도. 경직도 이 값을 쓴다 - 노브를 따로 두면 두 값이 따로 흘러간다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	float MontagePlayRate = 1.0f;

	// 루트모션 피격 클립의 수평 이동을 넉백 거리까지 늘릴 때의 상한 배율. 너무 늘리면 발이 미끄러져 보인다. 1 = 늘리지 않음.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="1.0", ClampMax="6.0"))
	float MaxRootMotionPushScale = 3.0f;

	// 밀린 방향과 피격 클립 방향이 이 각도(도)보다 어긋나면 방향은 그대로 두고 거리만 늘린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0", ClampMax="180.0"))
	float MaxRedirectDegrees = 90.0f;

	// 그로기(다운) 콘텐츠와 수치. 비워 두면(IsAuthored false) UVBGA_Knockdown 이 StaggerSet 의 서 있는 경직으로 떨어진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	FVBKnockdownSet KnockdownSet;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TMap<EVBWeaponType, FVBKnockdownSet> KnockdownStanceOverrides;

	// 저작된 스탠스 묶음을 고르고, 없거나 비었으면 기본 묶음을 돌려준다.
	const FVBKnockdownSet* FindKnockdownSet(EVBWeaponType Stance) const;

	// 이 다운이 스스로 트는 몽타주면 true. 고른 클립 묶음, 내려찍기, 빠져나오기, 다운 피격, 플린치를 본다.
	bool IsDownReaction(const FVBKnockdownSet& Set, const FVBKnockdownClips& Clips, const UAnimMontage* Montage) const;

	// 피격자 스탠스로 히트리액트 몽타주 해석. 미등록/미할당이면 DefaultSet.Montage,
	// 그것도 없으면 nullptr -> 호출부가 재생을 스킵한다(크래시 대신 무동작).
	UAnimMontage* ResolveMontage(EVBWeaponType Stance) const;

	// 스탠스 + 방향. 스탠스 세트(없으면 DefaultSet)에서 방향 몽타주, 없으면 그 세트의 무방향 Montage, 그것도 없으면 DefaultSet 순.
	// Pick 이 있으면 그 세트의 대체 클립까지 후보로 놓고 고른다(FVBHitReactSet::ResolveByDirection).
	UAnimMontage* ResolveMontage(EVBWeaponType Stance, EVBHitDirection Direction, FVBHitReactPick* Pick = nullptr) const;

	// 경직 몽타주 해석. 미할당이면 nullptr - 히트리액트로 폴백하면 미저작이 가벼운 플린치로 숨는다.
	//   StaggerSet 은 단일 세트라 스탠스 인자를 받지 않는다 - 시그니처는 할 수 있는 것만 말한다.
	UAnimMontage* ResolveStaggerMontage() const;

	// 방향까지 본 경직 몽타주. 방향 슬롯이 비면 무방향 Montage 로 떨어진다.
	//   방향을 모르는 호출부가 Front 를 지어내지 않도록 인자 없는 형제를 남긴다.
	UAnimMontage* ResolveStaggerMontage(EVBHitDirection Direction) const;

	// 무거운 피격 몽타주. 방향 슬롯이 비면 HeavySet 의 무방향 Montage, 그것도 없으면 nullptr -
	//   가벼운 세트로의 폴백은 호출자(UVBGA_HitReact)가 경고와 함께 한다. 여기서 폴백하면 미저작이 조용해진다.
	UAnimMontage* ResolveHeavyMontage(EVBWeaponType Stance, EVBHitDirection Direction, FVBHitReactPick* Pick = nullptr) const;

	// 저작 문제 - 하나에 한 줄.
	TArray<FText> FindProblems() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
