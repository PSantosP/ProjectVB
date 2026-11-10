// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "Engine/EngineTypes.h" // ECollisionChannel (HitTraceChannel) - transitive 의존 제거, VBTraversalConfig.h 와 동일 패턴
#include "Engine/DataAsset.h"
#include "AbilitySystem/VBHitImpact.h" // EVBHitImpact (HitImpact)
#include "VBAttackConfig.generated.h"

class UAnimMontage;
class UVBWeaponStyleBehavior;
class UVBSwingImpactProfile;

// FVBStackFullEffect - 스택형 추가효과와 한도 도달 시 터질 효과들의 짝.
// 임계값은 스택형 GE 의 StackLimitCount 한 곳에만 둔다 - 여기 또 두면 자산과 코드가 다른 한도를 믿는다.
USTRUCT(BlueprintType)
struct FVBStackFullEffect
{
	GENERATED_BODY()

	// 쌓이는 쪽(예: GE_Bleed_Blade). OnHitExtraEffects 에도 들어 있어야 실제로 쌓인다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TSubclassOf<UGameplayEffect> StackingEffect;

	// 한도 도달 시 대상에게 적용할 것들(예: GE_Bleed_Burst, GE_Stagger). 순서대로 적용된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TArray<TSubclassOf<UGameplayEffect>> BurstEffects;
};

// 변형이 어느 쪽을 향해 휘두르는가(공격자 기준). Any = 방향 무관(종전 변형 전부).
// UVBGA_EnemyMeleeAttack::SelectVariantIndex 가 표적의 좌우로 먼저 거른 뒤 거리 대역을 본다.
UENUM(BlueprintType)
enum class EVBAttackSide : uint8
{
	Any,
	Left,
	Right
};

// 이 변형을 어느 상황의 시작 콤보로 쓰나(UVBComboSelectionPolicy 가 읽는다). 0번 변형(AttackMontage)은 Neutral 고정이다.
UENUM(BlueprintType)
enum class EVBComboRole : uint8
{
	Neutral,
	Close,
	Far,
	Sprint,
	// 자동으로 고르지 않는다 - 명시적 트리거용으로 몽타주 데이터만 남긴다.
	Manual
};

// 이 변형을 시작 콤보로 고를 수 있는 표적 거리(공격자-표적 수평 중심, cm). 첫 공격이 닿는 선택 예산이다 - 밖이면 후보가 아니다.
USTRUCT(BlueprintType)
struct FVBComboTargetReach
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0", Units="cm"))
	float MinDistance = 0.0f;

	// 0 = 상한 없음.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0", Units="cm"))
	float MaxDistance = 0.0f;

	bool Contains(float DistanceCm) const
	{
		return DistanceCm >= MinDistance && (MaxDistance <= 0.0f || DistanceCm <= MaxDistance);
	}
};

// 선택 정책이 보는 변형 한 개(몽타주는 모른다).
struct FVBComboCandidate
{
	EVBComboRole Role = EVBComboRole::Neutral;
	FVBComboTargetReach Reach;
};

// FVBAttackMontageVariant - 한 공격의 동등한 변형 하나. 고르는 키가 없어 TMap 이 아니라 배열 원소다.
USTRUCT(BlueprintType)
struct FVBAttackMontageVariant
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	EVBComboRole Role = EVBComboRole::Neutral;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	FVBComboTargetReach TargetReach;

	// 이 변형이 휘두르는 쪽. Any 가 아닌 변형이 하나라도 있으면 표적의 좌우(공격자 로컬 Y 부호, VBComputeHitDirection 과 같은 규약)와
	//   맞는 변형만 후보가 된다. 어느 클립이 어느 쪽인지는 손 위치를 재서 채운다 - 이름을 믿지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	EVBAttackSide Side = EVBAttackSide::Any;

	// GA 의 MontagePlayRate 에 곱해진다. 1.0 = 클립 원래 템포.
	// 변형끼리 클립 길이가 달라 후딜이 들쭉날쭉해지는 것을 맞춘다 - root motion 은 속도만 바뀌고 이동거리는 보존된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.1"))
	float PlayRateScale = 1.0f;

	// config 의 DamageMultiplier 에 곱해진다. 1.0 = 그대로.
	// 트레이스 노티가 둘인 2타 변형은 같은 적을 두 번 때리므로 이 값(예: 0.5)으로 총 데미지를 1타 변형과 맞춘다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float DamageScale = 1.0f;

	// 이 변형의 멈추는 거리(cm). 음수 = 자산의 WarpApproachOffset 그대로.
	// 멈추는 거리는 그 클립의 손 뻗음이 정하는데 변형끼리 다르다. 해석은 UVBAttackConfig::GetVariantApproachOffset 한 곳이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="-1.0"))
	float WarpApproachOffsetOverride = -1.0f;

	// 이 변형의 콤보 타별 멈추는 거리(cm, 보폭 배율 적용 전). 칸이 없거나 음수면 설정의 공통 ComboApproachOffsets 로 내려간다.
	// 같은 번째 타라도 변형마다 손 뻗음이 달라 공통 값으로는 안 닿는다. 해석은 UVBAttackConfig::ResolveComboApproachOffset.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="-1"))
	TArray<float> ComboApproachOffsets;
};

/**
 * 공격 어빌리티 설정 DataAsset
 * 공격 유형별 수치를 에디터에서 관리
 */
UCLASS(BlueprintType)
class VB_API UVBAttackConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UVBAttackConfig();

	// 판정 사거리(cm, 애니 고유 = 배율 1 단위). 읽을 때는 GetScaledAttackRange - 공격자 보폭 배율을 곱한다(AttackRadius, 멈추는 거리도 같다).
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	float AttackRange = 200.0f;

	// 플레이어가 이 공격에 무엇으로 답할 수 있나(Attack.Answer.Parryable / DodgeOnly). 공격마다 답이 달라 아키타입이 아니라 여기 둔다.
	// 예고 큐(색, 소리)와 데미지 허브(스펙 동적 태그)가 같은 값을 읽어 예고와 판정이 어긋나지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(Categories="Attack.Answer"))
	FGameplayTag AttackAnswer;
	
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	float AttackRadius = 50.0f;

	// 적중 판정 trace 채널. 채널 분리, 투과 판정 변경을 데이터로 하기 위해 둔다(기본 ECC_Pawn).
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TEnumAsByte<ECollisionChannel> HitTraceChannel = ECC_Pawn;

	// 이 무기 공격의 템포 배율. GA 의 MontagePlayRate 에 곱해진다. 1.0 = 클립 원래 속도.
	// GA 단위 재생속도는 전 무기 공유라 "이 무기만 느리게"를 여기서 준다. root motion 거리와 노티 타이밍은 함께 스케일된다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0.1"))
	float MontagePlayRateScale = 1.0f;

	// 데미지
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	float DamageMultiplier = 1.0f;
	
	// 히트스톱 길이의 유일한 홈. 0 은 배선 오류로 보고 VBGC_HitStop 이 거부한다 - 흘리면 감속이 복구되지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|GameFeel", meta=(ClampMin="0.001"))
	float HitStopDuration = 0.04f;
	
	UPROPERTY(EditDefaultsOnly, Category="VB|GameFeel", meta=(ClampMin="0.001"))
	float HitStopTimeDilation = 0.01f;

	// 참 = 공격자와 맞은 적만 CustomTimeDilation 으로 멈칫한다(세계, 카메라는 계속). 거짓 = 전역 시간 배율로 세계 전체가 멈춘다.
	UPROPERTY(EditDefaultsOnly, Category="VB|GameFeel")
	bool bHitStopCombatantsOnly = true;

	// 피격자를 공격자보다 이만큼(초) 더 멈춘다. 벨트스크롤 실측(펀처 6/8프레임)의 비대칭. 0 = 같게.
	UPROPERTY(EditDefaultsOnly, Category="VB|GameFeel", meta=(ClampMin="0.0", ClampMax="0.2"))
	float HitStopVictimExtraSeconds = 0.02f;
	
	// GE 참조
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TSubclassOf<UGameplayEffect> DamageEffectClass;
	
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	FGameplayTag GameplayCueTag;
	
	// 콤보
	UPROPERTY(EditDefaultsOnly, Category="VB|Combo")
	int32 MaxComboCount = 1;
	
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TArray<FName> ComboSectionNames;

	// === 캔슬 문법 - 무기 무게마다 버퍼, 캔슬 시점이 달라 무기 leaf 에 둔다 ===
	// 콤보 입력 버퍼(초). 창이 열리기 전 이 시간 안에 들어온 입력을 창이 열릴 때 소비한다. 0 = 버퍼 없음.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0.0", ClampMax="2.0"))
	float ComboInputBufferSeconds = 0.5f;

	// 섹션 점프 때 늦은 입력의 포즈 차이를 잇는다. 0이면 즉시 전환한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0.0", ClampMax="0.3", Units="s"))
	float ComboSectionBlendSeconds = 0.0f;

	// 참 = 창 열림과 닿는 프레임 중 늦은 쪽에서 저장 입력을 소비해 다음 타로 넘어간다. 거짓 = 창이 닫힐 때 넘어간다.
	// 닿음을 조건에 넣는 이유: 창이 닿기 직전에 열리면 그 타의 닿는 프레임이 건너뛰어진다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	bool bChainAtWindowOpen = true;

	// AI 자동 콤보. true 면 콤보 창이 열릴 때 입력이 있었던 것으로 친다 - 버튼이 없는 적의 입력 자리다.
	//   잇는 규칙은 플레이어와 같은 코드가 돈다. 플레이어 자산은 false.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat|Combo")
	bool bAutoChainCombo = false;

	// 닿은 뒤 다음 타로 넘어가기까지의 여운(초). 0 이면 닿는 프레임 직후. 스윙의 팔로스루가 조금 보이게 하는 값.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0.0", ClampMax="0.5"))
	float ChainDelayAfterHitSeconds = 0.08f;

	// 참 = 창이 닫혔는데 다음 타가 없으면 몽타주를 멈추고 어빌리티를 끝낸다(회복 캔슬). 굳는 시간이 창 닫힘까지로 준다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	bool bCancelRecoveryAtWindowClose = true;

	// 참 = 콤보 창 안에서 스틱을 새로 밀면 그 자리에서 회복을 끊는다. 맞을 때부터 밀고 있던 스틱은 놓았다가 새로 밀어야 한다.
	//   저장된 다음 타가 있으면 공격이 이긴다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	bool bMoveCancelsRecoveryInWindow = false;

	// 참 = 위의 창 안 끊기를 맞을 때부터 밀고 있던 스틱으로도 허용한다(비교용 변형, 기본 거짓).
	//   개발 빌드는 콘솔 vb.Combat.WindowMoveCut(-1 자산 / 0 새로 민 것만 / 1 밀고 있던 것도)로 바꿔 본다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	bool bMoveCutAcceptsHeldStick = false;

	// 회복 캔슬 때 몽타주 블렌드아웃 시간(초).
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RecoveryBlendOutSeconds = 0.15f;

	// 회피 오프셋 유예(초). 회피가 끊은 콤보를 이 시간 안에 같은 무기로 이어 받으면 다음 타부터 시작한다. 0 = 끔.
	// 피격, 가드, 완료로 끝난 콤보는 기억하지 않는다 - 「맞을 때만 끊긴다」.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0.0", ClampMax="5.0"))
	float DodgeOffsetGraceSeconds = 1.0f;

	// === 예고 (적 공격 한정) - 기본은 아키타입(UVBEnemyConfig)이 정하고 이 둘은 이 공격만의 예외다 ===
	// < 0 = 아키타입 값. 0 = 이 공격은 예고 없음.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="-1.0"))
	float TelegraphDurationOverride = -1.0f;

	// 비면 아키타입의 TelegraphMontage. 길이는 위 값이 정하고 이 클립은 연출이다(아키타입 쪽과 같은 규약).
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> TelegraphMontage;

	// === 무기 무브셋 ===
	// 이 무브셋에서 재생할 공격 몽타주. UVBWeaponDefinition 이 이 자산을 가리키고, 없으면 GA 의 AnimMontage 를 쓴다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TObjectPtr<UAnimMontage> AttackMontage;

	// AttackMontage(0번 변형, 역할 Neutral 고정)를 시작 콤보로 고를 수 있는 표적 거리. 변형의 TargetReach 와 같은 뜻.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	FVBComboTargetReach AttackMontageTargetReach;

	// 이 공격의 추가 변형. 0번 변형은 AttackMontage 다 - 배열로 옮기면 CoreRedirects 가 못 잇어 값이 조용히 사라진다.
	// 콤보 공격(MaxComboCount>1)의 변형은 섹션명이 같아야 한다 - 섹션 전이는 이름으로 한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TArray<FVBAttackMontageVariant> MontageVariants;

	// 유효 변형 개수(AttackMontage 포함, null 원소 제외). 0 이면 재생할 몽타주가 없다.
	int32 GetMontageVariantCount() const;

	// 유효 변형마다 역할, 거리 자격(인덱스는 ResolveMontageVariant 와 같은 규칙).
	TArray<FVBComboCandidate> GetComboCandidates() const;

	// 새 콤보를 무엇으로 시작할지 고르는 정책. 비면 변형 선택은 종전 규칙(GA 의 SelectVariantIndex)이다.
	UPROPERTY(EditDefaultsOnly, Instanced, Category="VB|Combat")
	TObjectPtr<class UVBComboSelectionPolicy> ComboSelection;

	// 커서 값으로 변형을 고른다. 내부에서 개수로 나머지연산하므로 호출부가 범위를 안 지켜도 된다.
	// 반환 Montage 가 null 이면 재생할 것이 없다(호출부가 즉시-Trace 폴백).
	FVBAttackMontageVariant ResolveMontageVariant(int32 VariantIndex) const;

	// 변형의 멈추는 거리(cm, [0, 사거리] 로 잘라서). 변형 override 가 음수면 WarpApproachOffset. 인덱스는 ResolveMontageVariant 와 같은 규칙.
	//   계획(UVBEnemyCombatComponent::PlanNextAttack), 스윙 선택(SelectVariantIndex), 워프(ComputeApproachOffset)가 같은 값을 보게 하는 한 홈.
	// BodyScale = 공격자의 보폭 배율(VBStrideScale::Resolve). 거리 필드는 애니 고유(배율 1) 단위라 여기서 곱한다.
	float GetVariantApproachOffset(int32 VariantIndex, float BodyScale) const;

	// 판정 사거리, 반경(cm)에 공격자의 보폭 배율을 곱한 값. 곱하는 자리를 한 곳에 모아 판정과 멈추는 거리가 어긋나지 않게 한다.
	float GetScaledAttackRange(float BodyScale) const { return AttackRange * BodyScale; }
	float GetScaledAttackRadius(float BodyScale) const { return AttackRadius * BodyScale; }

	// 적중 시 대상에 추가 적용할 GE (출혈/스태거 등 스타일 고유 효과, 데이터 기반).
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TArray<TSubclassOf<UGameplayEffect>> OnHitExtraEffects;

	// 스택형 효과가 한도에 도달했을 때 한 번 터뜨릴 것들(OnHitExtraEffects 는 맞을 때마다).
	// 엔진 OverflowEffects 는 UE 5.8 에서 한도 도달 여부를 보지 않아 첫 타격부터 터지므로 판정을 우리 쪽에 둔다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TArray<FVBStackFullEffect> StackFullEffects;

	// 이 공격 재생 동안 시전자에게 슈퍼아머를 줄지. 공격 GA 는 무기 공유라 무기별 구분을 데이터 플래그로 한다.
	// State.Combat.SuperArmor 태그가 UVBGA_HitReact 활성화를 막는다 - 데미지, VFX, 사운드는 그대로 들어간다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	bool bSuperArmorDuringAttack = false;

	// 참 = Poise 를 비우는 타격에 이 아머가 깨진다. 끝까지 나가야 하는 무브(엎드려 휘두르기)는 거짓으로 둔다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(EditCondition="bSuperArmorDuringAttack"))
	bool bArmorBreakable = false;

	// 스타일 고유 동작 전략(옵션). null 이면 순수 데이터 처리(기본 SphereTrace). 데이터로 표현 못 하는 메커닉만 둔다.
	UPROPERTY(EditDefaultsOnly, Instanced, Category="VB|Combat")
	TObjectPtr<UVBWeaponStyleBehavior> StyleBehavior;

	// 접근 motion warping 이 적 중심 앞에서 멈출 거리(cm, 배율 1 단위). 런타임에 [0, AttackRange] 로 자른다.
	// GetVariantApproachOffset 이 보폭 배율을 곱한다. 몽타주 SkewWarp 의 bWarpTranslation=1 일 때만 효과가 있다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	float WarpApproachOffset = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Combat",
		meta=(ClampMin="-1",
			ToolTip="Per combo swing stopping distance in unscaled cm. Missing or negative entries preserve the variant/default distance."))
	TArray<float> ComboApproachOffsets;

	// 콤보 타의 멈추는 거리: 변형의 타별 값(VariantOffsets) -> 위 공통 타별 값 -> FallbackOffset(변형/자산 멈춤, 이미 배율 곱함).
	//   칸이 없거나 음수, NaN 이면 다음 층으로 내려간다. 타별 값은 배율 전이라 여기서 BodyScale 을 곱한다.
	float ResolveComboApproachOffset(const TArray<float>& VariantOffsets, int32 SwingIndex, float FallbackOffset, float BodyScale) const;

	// 접근 워프 최소 standoff(cm). 목적지를 늘 살짝 적 쪽에 두어 ToSyncPoint 가 0 벡터가 되지 않게 한다. 0 = 여유 없음.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	float MinWarpStandoff = 20.0f;

	// 접근 워프 sync point 가 공격자보다 대상 쪽으로 앞서야 하는 최소 거리(cm). 겹치면 SkewWarp Facing 이 0 벡터를 받아
	//   월드 0도로 돌므로, 덜 앞서면 매 프레임 "공격자 + 대상 방향 x 이 값" 에 붙잡는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0.1"))
	float WarpSyncMinLead = 2.0f;

	// 멀리서 시작한 스윙(적까지 거리 > AttackRange)의 접근 오프셋(cm). 0 = WarpApproachOffset 그대로.
	// 먼 시작만 더 가까이 세워 붙은 티가 나게 한다 - 가까이서 시작한 스윙의 간격은 건드리지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float FarStartApproachOffset = 110.0f;

	// 피격 대상의 수평 밀치기 속도(cm/s). 0이면 수평 속도를 바꾸지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	float KnockbackStrength = 0.0f;

	// 기존 자산의 상방 밀치기를 검출하기 위한 값. 이동에는 쓰지 않으며 반드시 0이어야 한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Deprecated", AdvancedDisplay)
	float KnockbackZ = 0.0f;

	// 이 공격이 대상 Poise 에서 깎는 양. 0 = 경직 파이프라인을 타지 않는다. 무브의 무게라 때리는 쪽이 갖는다.
	// 데미지 스펙에 SetByCaller(Data.PoiseDamage)로 실어 보내고, 버티는 양(MaxPoise)은 피격자 쪽 GE_InitStats 가 정한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float PoiseDamage = 0.0f;

	// 이 공격의 반응 등급. 받는 쪽 ImpactResistance 만큼 깎인 실효 등급이 반응 콘텐츠를 고른다.
	// 피해량에서 유도하지 않고 공격마다 직접 고른다. 한 스윙만 다르게 하려면 UVBSwingCombatProfile 이 덮는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	EVBHitImpact HitImpact = EVBHitImpact::Light;

	// 실효 등급이 Launch일 때 띄우는 높이(cm).
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0.0", Units="cm"))
	float LaunchHeight = 0.0f;

	// 판정 트레이스를 대상 쪽으로 틀어 주는 최대 각도(도). 0 = 보정 없음(순수 정면).
	// 정면 스윕은 대상이 조금만 벗어나도 빗나간다. 반경을 키우면 옆 적까지 쓸리므로 겨눈 대상 하나만 각도로 보정한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0.0", ClampMax="90.0"))
	float AimAssistMaxAngle = 35.0f;

	// 이 공격의 기본 임팩트 쉐이크 프로필. 트레이스 노티에 프로필이 없을 때만 쓰는 2순위 폴백이다.
	// 정면 밀림 방향조차 C++ 리터럴이 아니라 데이터로 두기 위해 있다. 둘 다 비면 쉐이크 없이 GA 가 Warning 을 남긴다.
	UPROPERTY(EditDefaultsOnly, Category="VB|GameFeel")
	TObjectPtr<UVBSwingImpactProfile> DefaultSwingImpactProfile;

	// 이 공격의 기본 화면 충격. 스윙 전투 프로필(노티)의 ScreenFeedback 이 없을 때만 쓰는 2순위다 - 위 쉐이크 프로필과 같은 우선순위.
	//   있으면 스윙 쉐이크 대신 이 프로필의 화면 층이 스윙당 한 번 난다. 모든 타가 무거운 무기, 반격을 여기 둔다.
	UPROPERTY(EditDefaultsOnly, Category="VB|GameFeel")
	TObjectPtr<const class UVBCombatFeedbackProfile> DefaultScreenFeedback;

	// 이 무기 스윙의 화면 흔들림 무게. 스윙 쉐이크, 화면 반동 세기에 곱한다(방향은 스윙 방향 자산 DA_SwingImpact_* 의 몫).
	//   방향 자산은 무기들이 함께 쓰므로 세기는 무기 쪽에 둔다. 0.05 미만은 받지 않는다(큐가 0 을 "안 실음" 으로 읽는다).
	UPROPERTY(EditDefaultsOnly, Category="VB|GameFeel", meta=(ClampMin="0.05"))
	float SwingShakeScale = 1.0f;

#if WITH_EDITOR
	// 저장 시 Manual 행 옆에서 돌아가며 고를 수 없는 시작 대역을 알린다(UVBComboSelectionPolicy::FindLoneBandsBesideManual).
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
