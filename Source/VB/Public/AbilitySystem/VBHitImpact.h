// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "VBHitImpact.generated.h"

// 피격 반응 등급 - 공격이 선언한 세기를 받는 쪽 ImpactResistance 만큼 깎은 실효 등급으로 반응 콘텐츠를 고른다. 순서가 곧 세기다.
// Attack.Answer.*(막기, 패링, 회피)와 다른 축이고, 공격 쪽과 받는 쪽이 같이 쓰므로 자산 헤더가 아니라 여기 둔다.
// 서수가 자산에 저장되고 uint8 로 직렬화되므로(FVBGameplayEffectContext::NetSerialize) 순서를 바꾸지 말고 끝에만 덧붙인다.
UENUM(BlueprintType)
enum class EVBHitImpact : uint8
{
	// 반응 없음(발광, 넉백만). 저항이 등급을 다 깎았을 때의 결과이고, 공격 자산에 직접 고르는 값은 아니다.
	None,
	// 플린치. 현행의 모든 피격이 이것이다 - 공격 자산 기본값.
	Light,
	// 무거운 피격(크게 비틀거림). 콘텐츠 = UVBHitReactConfig::HeavySet.
	Heavy,
	// 넘어짐 -> 누운 대기 -> 기상. 콘텐츠 = UVBHitReactConfig::KnockdownSet.
	Knockdown,
	// 공중에 뜸 -> 착지. 넘어짐 계열이라 받는 쪽이 못 받으면 넘어뜨림으로 내려간다.
	Launch
};

// 선언 등급을 받는 쪽 저항만큼 깎는다. 저항은 속성(float)이라 반올림한 정수 단계로 읽고, 0 아래는 0 으로 본다.
// None 은 깎아도 None 이다.
VB_API EVBHitImpact VBResolveHitImpact(EVBHitImpact Declared, float Resistance);

// 서수 -> 등급. 범위 밖과 0(None)이면 Light. 효과 컨텍스트가 선언 등급을 읽을 때 쓴다(FVBGameplayEffectContext::GetDeclaredHitImpact).
//   Light 로 떨어지는 이유: 등급을 안 싣는 피해(출혈 폭발, 비근접)가 종전처럼 플린치를 내야 회귀가 없다.
//   None 을 공격이 선언하는 경로는 없다 - None 은 저항이 깎은 결과로만 생긴다.
VB_API EVBHitImpact VBHitImpactFromMagnitude(float Magnitude);

// 등급 -> Combat.Impact.* 태그. None 도 자기 태그가 있다(태그 없음 = 옛 경로 = Light 와 갈라야 한다).
VB_API FGameplayTag VBHitImpactToTag(EVBHitImpact Impact);

// 이벤트 태그 컨테이너에서 실효 등급을 읽는다. Combat.Impact.* 가 하나도 없으면 Light(패링 되받기 같은 옛 경로의 현행 보존).
VB_API EVBHitImpact VBHitImpactFromTags(const FGameplayTagContainer& Tags);

// 로그용 이름.
VB_API const TCHAR* VBHitImpactName(EVBHitImpact Impact);

struct FGameplayEventData;

// 밀린 방향(월드 yaw, 도)을 반응 이벤트의 TargetData 에 싣는다 - 넉백, 클립 이동, 넘어지는 쪽이 한 방향이 되게 한다.
// EventMagnitude 는 피해량이라 겹치지 않으며, TargetData 는 ServerInitiated 활성화 때 소유 클라로도 넘어간다.
VB_API void VBSetPushYaw(FGameplayEventData& Event, float Yaw);
// 실려 있으면 true 와 요. 없으면(비근접 피해, 패링 되받기 등 옛 경로) false - 호출자는 방향에 기대는 처리를 건너뛴다.
VB_API bool VBGetPushYaw(const FGameplayEventData& Event, float& OutYaw);

// 밀려야 할 거리(cm)를 같은 밀림 값에 싣는다 - 반응 GA 가 root motion 클립 이동을 이 거리까지 늘린다. VBSetPushYaw 뒤에 부른다.
VB_API void VBSetPushDistance(FGameplayEventData& Event, float DistanceCm);
// 실려 있고 유한한 양수면 true. 아니면 OutDistanceCm 을 건드리지 않는다.
VB_API bool VBGetPushDistance(const FGameplayEventData& Event, float& OutDistanceCm);

// 지상 제동 거리와 명시적 띄우기의 비행 거리를 합산한다. 지상 마찰은 제외한 상한 추정이다.
VB_API float VBEstimateKnockbackDistance(float KnockStrength, float LaunchHeight, float GravityAbs, float BrakingDeceleration);

enum class EVBHitMovement : uint8
{
	None,
	Push,
	Launch
};

// 일반 밀치기는 수평 속도만, 띄우기는 높이에 맞는 수직 속도까지 지정한다.
struct FVBHitMovement
{
	EVBHitMovement Mode = EVBHitMovement::None;
	FVector Velocity = FVector::ZeroVector;
};
// 루트모션의 수평 이동 소유권을 보존하며 밀치기와 띄우기를 구분한다.
VB_API FVBHitMovement VBResolveHitMovement(const FVector& PushDir, float KnockStrength,
	bool bClipOwnsHorizontal, float LaunchHeight, float GravityAbs);
