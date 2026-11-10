// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBMeleeHitPresentation.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Abilities/VBSwingHitContext.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBTargetLockComponent.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBCombatFeedbackProfile.h" // 강타 화면 충격을 효과 문맥 SourceObject(UObject*)로 싣는다 - 완전 타입 필요
#include "Data/VBSwingCombatProfile.h"
#include "Data/VBSwingImpactProfile.h" // SourceObject 로 싣고 스윙 축을 읽는다 - 완전 타입 필요
#include "GameFramework/Character.h"
#include "GameplayEffectTypes.h"
#include "VB/VB.h"

void VBMeleeHitPresentation::ReportHitToTargetLock(UVBTargetLockComponent* TargetLock, FVBSwingHitContext& Context, AActor* HitActor,
	bool bDealtDamage)
{
	// 자동 락온은 이 스윙이 처음 맞힌 적으로 한 번만 시도한다. 뒤 표적이 락을 훔치지 않게 시도 뒤 잠근다.
	if (!Context.bAutoLocked)
	{
		if (TargetLock)
		{
			TargetLock->TryAutoLockOnTarget(HitActor);
		}

		Context.bAutoLocked = true;
	}

	if (bDealtDamage && !Context.bCameraReported)
	{
		if (TargetLock)
		{
			TargetLock->NotifyMeleeHitForCamera(HitActor);
		}

		Context.bCameraReported = true;
	}
}

void VBMeleeHitPresentation::PlaySwingImpact(const UObject& Ability, const FVBSwingHitContext& Context, const UVBAttackConfig& Config,
	ACharacter& Avatar, UAbilitySystemComponent& OwnerASC)
{
	// 방향 쉐이크도 스윙당 한 번(대상마다 쏘면 겹쳐 세진다). 강타 화면 충격이 있으면 쉐이크 대신 그것을 낸다.
	const UVBSwingCombatProfile* CombatProfile = Context.CombatProfile.Get();
	const UVBCombatFeedbackProfile* ScreenFeedback = CombatProfile && CombatProfile->ScreenFeedback
		? CombatProfile->ScreenFeedback.Get() : Config.DefaultScreenFeedback.Get();
	const UVBSwingImpactProfile* Profile = Context.ImpactProfile.Get();
	if (Profile || ScreenFeedback)
	{
		// Context.Forward 를 쓰지 않는 이유: 그것은 AimAssist 로 꺾인 판정 방향이지 스윙 방향이 아니다.
		// 조준 보정이 내려베기를 옆으로 바꿔 버리면 안 된다. 밀림 방향의 근거는 아바타의 자세 하나뿐이다.
		const FVector WorldSwingDir = Profile
			? Avatar.GetActorQuat().RotateVector(Profile->GetNormalizedAxis()) : FVector::ZeroVector;

		// Normal 이 0 이면 받는 쪽이 월드 +X 로 민다 - 쉐이크는 끊고, 방향 없는 화면 충격은 낸다.
		if (ScreenFeedback || !WorldSwingDir.IsNearlyZero())
		{
			FGameplayCueParameters SwingCueParams;
			SwingCueParams.Normal = WorldSwingDir; // 복제되는 단위벡터. 서버 산출이라 전 클라가 같은 값을 본다
			SwingCueParams.Instigator = &Avatar; // GCN 의 로컬 플레이어 게이트 판정 근거
			SwingCueParams.SourceObject = Profile; // 쉐이크 클래스와 세기의 데이터 위치를 통째로 전달
			SwingCueParams.RawMagnitude = Config.SwingShakeScale; // 무기 스윙 무게. VBGC_SwingImpactShake 가 화면 반동과 쉐이크에 곱한다(0 은 안 보냄, 1 로 친다)
			if (ScreenFeedback)
			{
				// SourceObject 는 위에서 쉐이크 프로필이 쓰므로 효과 문맥의 SourceObject 에 싣는다(복제된다).
				FGameplayEffectContextHandle FeedbackContext = OwnerASC.MakeEffectContext();
				FeedbackContext.AddSourceObject(ScreenFeedback);
				SwingCueParams.EffectContext = FeedbackContext;
			}

			OwnerASC.ExecuteGameplayCue(VBGameplayTags::GameplayCue_Combat_SwingImpact, SwingCueParams);
		}
	}
	else
	{
		// Verbose 가 아니라 Warning 인 이유: BP 의 쉐이크 노드를 제거했으므로 미저작은 정상 상태가 아니라
		// "쉐이크가 통째로 사라진" 설정 오류다. 노티와 leaf 어느 쪽에도 프로필이 없다는 뜻이다.
		VB_LOG(Warning, "%s: SwingImpactProfile·ScreenFeedback 미할당(노티/leaf 모두) - 스윙 화면 반응 없음", *Ability.GetName());
	}
}
