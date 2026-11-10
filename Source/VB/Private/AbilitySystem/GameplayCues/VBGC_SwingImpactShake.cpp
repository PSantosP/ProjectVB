// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/GameplayCues/VBGC_SwingImpactShake.h"

#include "Camera/CameraShakeBase.h"      // TSubclassOf<UCameraShakeBase> 완전 타입
#include "Camera/CameraTypes.h"          // ECameraShakePlaySpace
#include "Camera/PlayerCameraManager.h"  // StartCameraShake
#include "Data/VBSwingImpactProfile.h"
#include "AbilitySystem/GameplayCues/VBGC_CombatFeedback.h" // 강타 화면 충격의 실행 경로 하나
#include "Data/VBCombatFeedbackProfile.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "VB/VB.h"

// 생성자에서 GameplayCueTag 를 세우지 않는다. 태그는 BP 자식의 Class Defaults 가 소유한다.
// 네이티브 부모가 태그를 들면 BP 자식의 GameplayCueName 이 None 이 되어 큐 세트 등록에서 조용히 빠진다.
UVBGC_SwingImpactShake::UVBGC_SwingImpactShake()
{
}

namespace
{
	// 무기의 스윙 무게(RawMagnitude). 0 이하는 안 실은 것으로 읽어 1(무기 쪽 하한이 0.05 라 섞이지 않는다).
	float SwingScale(const FGameplayCueParameters& Parameters)
	{
		return FMath::IsFinite(Parameters.RawMagnitude) && Parameters.RawMagnitude > 0.0f ? Parameters.RawMagnitude : 1.0f;
	}
}

bool UVBGC_SwingImpactShake::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	// 큐는 모든 머신에서 돈다. 흔들 카메라는 휘두른 본인 것 하나뿐이다(리슨 서버 이중 발동도 여기서 막힌다).
	if (!Parameters.IsInstigatorLocallyControlledPlayer(MyTarget))
	{
		return false;
	}

	// 강타면 쉐이크 대신 효과 문맥 SourceObject 의 전투 연출 화면 층을 쓴다. 한 스윙에 둘을 같이 내지 않는다.
	if (const UVBCombatFeedbackProfile* Feedback = Cast<UVBCombatFeedbackProfile>(Parameters.EffectContext.GetSourceObject()))
	{
		FVBCombatFeedbackEvent Event;
		Event.Profile = Feedback;
		Event.Recipient = Cast<APawn>(Parameters.GetInstigator());
		if (!Event.Recipient)
		{
			Event.Recipient = Cast<APawn>(Parameters.GetEffectCauser());
		}

		Event.Direction = Parameters.Normal;
		Event.ScreenScale = SwingScale(Parameters);
		// 이 큐는 스윙당 한 번이라 접점이 없다. 접촉별 월드 층은 접촉 큐(VBGC_MeleeImpact)의 몫이다.
		Event.bWorldEffects = false;
		UVBGC_CombatFeedback::PlayFeedback(MyTarget ? MyTarget->GetWorld() : nullptr, Event, TEXT("SwingImpact"));
		return true;
	}

	// 쉐이크 자산과 세기의 데이터 홈. GA 가 SourceObject 로 통째로 실어 보냈다.
	const UVBSwingImpactProfile* Profile = Cast<UVBSwingImpactProfile>(Parameters.SourceObject.Get());
	if (!Profile || !Profile->ShakeClass)
	{
		// Verbose 인 이유: 쉐이크 자산만 비운 프로필은 유효한 저작 선택일 수 있고 매 타격 경고는 로그를 덮는다.
		VB_LOG(Verbose, "VBGC_SwingImpactShake: 프로필/ShakeClass 없음 - 쉐이크 스킵");
		return false;
	}

	// 흔들 카메라는 instigator 폰의 컨트롤러 것이다. GetPlayerController(0) 은 분할 화면에서 '나' 라는 보장이 없다.
	APawn* InstigatorPawn = Cast<APawn>(Parameters.GetInstigator());
	APlayerController* PC = InstigatorPawn ? Cast<APlayerController>(InstigatorPawn->GetController()) : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return false;
	}

	// UserDefined PlaySpace 로 쉐이크의 X 축을 스윙 방향(Normal.Rotation())에 맞춘다. 수치는 전부 쉐이크 자산에 있다.
	PC->PlayerCameraManager->StartCameraShake(
		Profile->ShakeClass,
		Profile->ShakeScale * SwingScale(Parameters),
		ECameraShakePlaySpace::UserDefined,
		Parameters.Normal.Rotation());

	return true;
}
